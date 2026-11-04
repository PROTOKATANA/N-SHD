# include "gota_sueltos.h"

# include "gota_comun.h"

# include "gota_liquido_hlsl.h"

# include "pipeline_comun.h"

using namespace Diligent ;

namespace shd :: nodos {

namespace {

// Una celda de los canales por bit, en palabras de 32 : la rejilla entera son 8 KB, y cabe en
// la memoria compartida de un solo grupo, que la recorre hasta el final sin lanzar más veces

static_assert(lado_canal % 32 == 0) ;

constexpr Uint32 palabras_fila = lado_canal / 32 ;

constexpr Uint64 palabras = static_cast<Uint64>(lado_canal) * palabras_fila ;

constexpr const char * sueltos_hlsl = R"(
static const uint PALABRAS = M / 32u;           // palabras de cada fila
static const uint TODAS = M * PALABRAS;
static const float NUCLEO = 0.6;                // radios : la gota a menos de esto del centro es el cuerpo
static const uint VUELTAS = 64u;                // tope de vueltas para unir : una gota normal acaba en 2 o 3
)" ;

// Qué celdas tienen líquido, con el borde a la mitad de lo que pinta la pintura, de dos
// maneras : en su centro, para unir (mirando más, un punto a un píxel del borde se unía al
// cuerpo por el hueco), y en su centro o en cuatro puntos más, para saber dónde hay algo (en
// el centro solo, una franja más fina que la celda se quedaba sin ver, y se pintaba). Y
// cuáles son del cuerpo. Un grupo hace 8 filas de una palabra

constexpr const char * marcar_hlsl = R"(
RWStructuredBuffer<uint> bits;   // TODAS del líquido en el centro, TODAS del cuerpo y TODAS de algo de líquido
groupshared uint b_centro[8];
groupshared uint b_cuerpo[8];
groupshared uint b_algo[8];
// la gota y el canal en p
float2 liquido_en(float2 p) {
  float2 gc = celda_canal(p);
  return float2(forma_gota(p, campo_en(celda_de(p)), 1e-3), forma_canal(p, gc, canal_en(gc).x, 1e-5));
}
[numthreads(32, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 t : SV_GroupThreadID, uint3 g : SV_GroupID) {
  if (t.x == 0u) { b_centro[t.y] = 0u; b_cuerpo[t.y] = 0u; b_algo[t.y] = 0u; }
  GroupMemoryBarrierWithGroupSync();
  float2 p = centro_celda(int2(id.xy));
  float2 l = liquido_en(p);
  float centro = max(l.x, l.y);
  float algo = centro;
  // Los otros cuatro puntos, solo donde el centro no tiene líquido : si lo tiene, ya hay. Medido
  // (banco del visor, arrastre a 1280 × 800) : de 0,161 a 0,139 ms, el mismo resultado. Saltar
  // también lo lejano con una cota a ojo daba 0,118 pero cambiaba 7 píxeles, y con una cota
  // exacta, 0,144 : las dos, descartadas (RAYO-LEGO.md)
  if (centro <= 0.5) {
    [unroll] for (uint k = 0u; k < 4u; ++k) {
      float2 m = liquido_en(p + (float2(k & 1u, k >> 1) - 0.5) * 0.5 * CELDA_C);
      algo = max(algo, max(m.x, m.y));
    }
  }
  uint bit = 1u << t.x;
  if (centro > 0.5) InterlockedOr(b_centro[t.y], bit);
  if (l.x > 0.5 && length(p) < NUCLEO) InterlockedOr(b_cuerpo[t.y], bit);
  if (algo > 0.5) InterlockedOr(b_algo[t.y], bit);
  GroupMemoryBarrierWithGroupSync();
  if (t.x == 0u) {
    uint k = id.y * PALABRAS + g.x;
    bits[k] = b_centro[t.y];
    bits[TODAS + k] = b_cuerpo[t.y];
    bits[2u * TODAS + k] = b_algo[t.y];
  }
}
)" ;

// Lo unido al cuerpo, de líquido en líquido, con las ocho vecinas. Un solo grupo, un hilo por
// fila. Cada vuelta : en horizontal, cada fila toma lo de las de arriba y abajo y lo lleva
// hasta el final de cada tramo de líquido de la fila; en vertical, cada columna de palabras
// lo lleva de arriba abajo y de abajo arriba. Hasta que una vuelta no cambia nada

constexpr const char * unir_hlsl = R"(
StructuredBuffer<uint> bits;
RWStructuredBuffer<uint> unido;
groupshared uint g_liq[TODAS];
groupshared uint g_unido[TODAS];
groupshared uint g_cambio[2];
groupshared uint g_cuerpo;

// La palabra w de la fila r, y 0 fuera de la rejilla (en HLSL «?:» evalúa las dos partes : el
// índice va siempre dentro); con sus celdas de al lado, también las de las palabras vecinas; y
// con las de las filas de arriba y abajo. De lo unido y, al final, de lo suelto
#define VECINDAD(PALABRA, ENSANCHAR, ALREDEDOR, DE) \
uint PALABRA(int r, int w) { \
  bool dentro = r >= 0 && r < int(M) && w >= 0 && w < int(PALABRAS); \
  uint v = DE[uint(clamp(r, 0, int(M) - 1)) * PALABRAS + uint(clamp(w, 0, int(PALABRAS) - 1))]; \
  return dentro ? v : 0u; \
} \
uint ENSANCHAR(int r, int w) { \
  uint v = PALABRA(r, w); \
  return v | (v << 1) | (v >> 1) | (PALABRA(r, w - 1) >> 31) | (PALABRA(r, w + 1) << 31); \
} \
uint ALREDEDOR(int r, int w) { return ENSANCHAR(r, w) | ENSANCHAR(r - 1, w) | ENSANCHAR(r + 1, w); }
VECINDAD(palabra, ensanchar, alrededor, g_unido)
VECINDAD(palabra_suelta, ensanchar_suelta, alrededor_suelto, g_liq)

// lo que se llega de `g` hacia los bits altos, o hacia los bajos, sin salir de `p` (Kogge-Stone)
uint llenar_arriba(uint g, uint p) {
  g |= p & (g << 1); p &= p << 1;
  g |= p & (g << 2); p &= p << 2;
  g |= p & (g << 4); p &= p << 4;
  g |= p & (g << 8); p &= p << 8;
  g |= p & (g << 16);
  return g;
}
uint llenar_abajo(uint g, uint p) {
  g |= p & (g >> 1); p &= p >> 1;
  g |= p & (g >> 2); p &= p >> 2;
  g |= p & (g >> 4); p &= p >> 4;
  g |= p & (g >> 8); p &= p >> 8;
  g |= p & (g >> 16);
  return g;
}

[numthreads(HILOS_U, 1, 1)]
void main(uint3 t : SV_GroupThreadID) {
  int r = int(t.x);
  if (r == 0) { g_cuerpo = 0u; g_cambio[0] = 0u; g_cambio[1] = 0u; }
  GroupMemoryBarrierWithGroupSync();
  uint hay = 0u;
  for (uint w = 0u; w < PALABRAS; ++w) {
    uint k = uint(r) * PALABRAS + w;
    uint liq = bits[k];
    uint cuerpo = bits[TODAS + k] & liq;
    g_liq[k] = liq;
    g_unido[k] = cuerpo;
    hay |= cuerpo;
  }
  if (hay != 0u) InterlockedOr(g_cuerpo, 1u);
  GroupMemoryBarrierWithGroupSync();
  // sin cuerpo, todo se queda : mejor un trozo de más que una gota que desaparece
  if (g_cuerpo == 0u) {
    for (uint w = 0u; w < PALABRAS; ++w) { uint k = uint(r) * PALABRAS + w; g_unido[k] = g_liq[k]; }
  }
  GroupMemoryBarrierWithGroupSync();
  [loop] for (uint vuelta = 0u; vuelta < VUELTAS; ++vuelta) {
    uint cambio = 0u;
    uint nueva[PALABRAS];
    for (uint w = 0u; w < PALABRAS; ++w) {
      nueva[w] = (palabra(r, int(w)) | ensanchar(r - 1, int(w)) | ensanchar(r + 1, int(w))) & g_liq[uint(r) * PALABRAS + w];
    }
    uint lleva = 0u;
    for (uint w = 0u; w < PALABRAS; ++w) {
      uint m = g_liq[uint(r) * PALABRAS + w];
      nueva[w] = llenar_arriba(nueva[w] | (lleva & m), m);
      lleva = nueva[w] >> 31;
    }
    lleva = 0u;
    for (int w = int(PALABRAS) - 1; w >= 0; --w) {
      uint m = g_liq[uint(r) * PALABRAS + uint(w)];
      nueva[w] = llenar_abajo(nueva[w] | ((lleva << 31) & m), m);
      lleva = nueva[w] & 1u;
    }
    // todas han leído las filas de al lado
    GroupMemoryBarrierWithGroupSync();
    if (r == 0) g_cambio[(vuelta + 1u) & 1u] = 0u;
    for (uint w = 0u; w < PALABRAS; ++w) {
      uint k = uint(r) * PALABRAS + w;
      cambio |= nueva[w] ^ g_unido[k];
      g_unido[k] = nueva[w];
    }
    GroupMemoryBarrierWithGroupSync();
    if (r < int(PALABRAS)) {
      uint antes = 0u;
      for (uint y = 0u; y < M; ++y) {
        uint k = y * PALABRAS + uint(r);
        uint v = g_unido[k] | (antes & g_liq[k]);
        cambio |= v ^ g_unido[k];
        g_unido[k] = v;
        antes = v;
      }
      antes = 0u;
      for (int y = int(M) - 1; y >= 0; --y) {
        uint k = uint(y) * PALABRAS + uint(r);
        uint v = g_unido[k] | (antes & g_liq[k]);
        cambio |= v ^ g_unido[k];
        g_unido[k] = v;
        antes = v;
      }
    }
    if (cambio != 0u) InterlockedOr(g_cambio[vuelta & 1u], 1u);
    GroupMemoryBarrierWithGroupSync();
    if (g_cambio[vuelta & 1u] == 0u) break;
  }
  // lo suelto, en lo que era el líquido
  for (uint w = 0u; w < PALABRAS; ++w) { uint k = uint(r) * PALABRAS + w; g_liq[k] &= ~g_unido[k]; }
  GroupMemoryBarrierWithGroupSync();
  // Lo unido, y detrás donde hay algo de líquido : la pintura decide cada píxel por esas celdas
  // de alrededor. Una celda con algo de líquido pero no en su centro (el borde, una franja
  // fina) es del cuerpo si lo toca y no toca nada suelto
  for (uint w = 0u; w < PALABRAS; ++w) {
    uint k = uint(r) * PALABRAS + w;
    uint centro = g_unido[k] | g_liq[k];
    uint algo = bits[2u * TODAS + k] | centro;
    uint borde = algo & ~centro & alrededor(r, int(w)) & ~alrededor_suelto(r, int(w));
    unido[k] = g_unido[k] | borde;
    unido[TODAS + k] = algo;
  }
}
)" ;

}  // namespace

bool SueltosGota :: crear(

	const Gpu & gpu ,

	IBuffer * uniforme ,

	const std :: string & hlsl ,

	const Lecturas & lecturas ,

	std :: string & error

){

	gpu_ = gpu ;

	bits_ = gpu_campo(gpu_ , { palabras * 3 , 4 , "Nodos gota sueltos" }) ;

	unido_ = gpu_campo(gpu_ , { palabras * 2 , 4 , "Nodos gota unido" }) ;

	if (!bits_ || !unido_) { return gpu_fallo(error , "No se pudieron crear los campos de los trozos sueltos") ; }

	const std :: vector<Recurso> fijos{ { "Gota" , uniforme } } ;

	marcar_ = gpu_kernel(gpu_ , { "Nodos gota sueltos marcar" , hlsl + liquido_gota_hlsl + sueltos_hlsl + marcar_hlsl , fijos , palabras_fila , lado_canal / 8 } , error) ;

	unir_ = gpu_kernel(gpu_ , { "Nodos gota sueltos unir" , "#define HILOS_U " + std :: to_string(lado_canal) + "\n" + hlsl + sueltos_hlsl + unir_hlsl , fijos , 1 , 1 } , error) ;

	if (!marcar_.pipeline || !unir_.pipeline) { return false ; }

	marcar_b_ = gpu_atar(marcar_ , {

		{ "campo" , gpu_lectura(lecturas.campo) } ,

		{ "canal_masc" , gpu_lectura(lecturas.mascara) } ,

		{ "canal_alc" , gpu_lectura(lecturas.alcance) } ,

		{ "canal_cer" , gpu_lectura(lecturas.cerca) } ,

		{ "canal_cal" , gpu_lectura(lecturas.calor) } ,

		{ "bits" , gpu_escritura(bits_) }

	}) ;

	unir_b_ = gpu_atar(unir_ , { { "bits" , gpu_lectura(bits_) } , { "unido" , gpu_escritura(unido_) } }) ;

	return true ;

}

std :: vector<std :: string> SueltosGota :: tramos() { return { "gota sueltos marcar" , "gota sueltos unir" } ; }

void SueltosGota :: avanzar(IDeviceContext * ctx , Medidor * banco , std :: size_t primera) {

	{

		Tramo t(banco , primera , Etapa :: subir , gpu_.device , ctx) ;

		gpu_lanzar(ctx , marcar_ , marcar_b_ , { bits_ }) ;

	}

	Tramo t(banco , primera + 1 , Etapa :: subir , gpu_.device , ctx) ;

	gpu_lanzar(ctx , unir_ , unir_b_ , { unido_ }) ;

}

}  // namespace shd :: nodos
