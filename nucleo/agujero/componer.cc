# include "componer.h"

# include "forma_agujero.h"

# include "pintura_disco.h"

# include <string>

# include <vector>

# include "cbuffer_agujero.h"

# include "gpu.h"

using namespace Diligent ;

namespace shd {

namespace {

constexpr const char * componer_hlsl = R"(
StructuredBuffer<float4> mapa;
StructuredBuffer<float4> mapa_bajo;
StructuredBuffer<uint> indice_bordes;
StructuredBuffer<float4> bordes;
StructuredBuffer<float> temperatura;

static const uint SIN_BORDE = 0xFFFFFFFFu;
RWStructuredBuffer<uint> acumulado;

// La densidad del disco en la celda c (bilineal; el ángulo da la vuelta, el
// radio se queda dentro)
float temperatura_celda(int2 c) {
  c.x = ((c.x % REJILLA_ANCHO) + REJILLA_ANCHO) % REJILLA_ANCHO;
  c.y = clamp(c.y, 0, REJILLA_ALTO - 1);
  return temperatura[uint(c.y * REJILLA_ANCHO + c.x)];
}

float temperatura_en(float2 q) {
  float2 b = floor(q);
  float2 f = q - b;
  int2 c = int2(b);
  return lerp(lerp(temperatura_celda(c), temperatura_celda(c + int2(1, 0)), f.x), lerp(temperatura_celda(c + int2(0, 1)), temperatura_celda(c + int2(1, 1)), f.x), f.y);
}

// El color de un cruce con el disco (r, φ̃) y lo que tapa (alfa) : su
// temperatura, pintada (ver pintura_disco.h)
float3 emision(float2 rf, out float alfa) {
  float r = rf.x;
  float u = (log(r) - disco.x) / (disco.y - disco.x);
  // el azimut de la cámara gira el disco (los rayos se trazaron con 0)
  float t = temperatura_en(float2((rf.y + orbita.x) / TAU * float(REJILLA_ANCHO) - 0.5, u * float(REJILLA_ALTO) - 0.5));
  float3 color = pintar_temperatura(t);
  // opaco, con los bordes (la ISCO y el exterior) suaves
  alfa = disco.z * smoothstep(0.0, 0.02, u) * (1.0 - smoothstep(0.985, 1.0, u));
  return color * disco.w;
}

// El color de un rayo (sus datos del mapa) : los cruces con el disco, del
// más cercano a la cámara al más lejano, de 0 a 1, como se ve; y cuánto del
// cielo de detrás se ve por él (0 si cae al agujero o el disco lo tapa)
float3 color_de(float4 m0, float4 m1, out float cielo) {
  float3 color = float3(0.0, 0.0, 0.0);
  float pasa = 1.0;
  float alfa;
  float3 e;
  if (m0.x > 0.0) { e = emision(m0.xy, alfa); color += pasa * alfa * e; pasa *= 1.0 - alfa; }
  if (m0.z > 0.0) { e = emision(m0.zw, alfa); color += pasa * alfa * e; pasa *= 1.0 - alfa; }
  if (m1.x > 0.0) { e = emision(m1.xy, alfa); color += pasa * alfa * e; pasa *= 1.0 - alfa; }
  cielo = m1.z > -1.0 ? pasa : 0.0;
  return color;
}

// Un mapa en el punto q (en sus píxeles, con el centro del píxel i en i),
// interpolado : cada cruce entre los 4 vecinos si los 4 lo tienen, y el cielo
// si los 4 escapan (los ángulos, sin saltar por la vuelta); si no, el vecino
// más cercano. Dentro de una forma, suave; en sus bordes están los subrayos
float angulo_cerca(float x, float ref) { return x - TAU * round((x - ref) / TAU); }

float2 mezcla(float2 a, float2 b, float2 c, float2 d, float2 f) {
  b.y = angulo_cerca(b.y, a.y);
  c.y = angulo_cerca(c.y, a.y);
  d.y = angulo_cerca(d.y, a.y);
  return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

#define INTERPOLAR(nombre, buf) \
void nombre(float2 q, int cols, int filas, out float4 m0, out float4 m1) { \
  float2 b = floor(q); \
  float2 f = q - b; \
  int2 c0 = clamp(int2(b), int2(0, 0), int2(cols - 1, filas - 1)); \
  int2 c1 = clamp(int2(b) + 1, int2(0, 0), int2(cols - 1, filas - 1)); \
  uint ia = uint(c0.y * cols + c0.x), ib = uint(c0.y * cols + c1.x), ic = uint(c1.y * cols + c0.x), id = uint(c1.y * cols + c1.x); \
  float4 a0 = buf[2u * ia], a1 = buf[2u * ia + 1u]; \
  float4 b0 = buf[2u * ib], b1 = buf[2u * ib + 1u]; \
  float4 g0 = buf[2u * ic], g1 = buf[2u * ic + 1u]; \
  float4 d0 = buf[2u * id], d1 = buf[2u * id + 1u]; \
  bool x1 = f.x >= 0.5, y1 = f.y >= 0.5; \
  m0 = y1 ? (x1 ? d0 : g0) : (x1 ? b0 : a0); \
  m1 = y1 ? (x1 ? d1 : g1) : (x1 ? b1 : a1); \
  if (a0.x > 0.0 && b0.x > 0.0 && g0.x > 0.0 && d0.x > 0.0) m0.xy = mezcla(a0.xy, b0.xy, g0.xy, d0.xy, f); \
  if (a0.z > 0.0 && b0.z > 0.0 && g0.z > 0.0 && d0.z > 0.0) m0.zw = mezcla(a0.zw, b0.zw, g0.zw, d0.zw, f); \
  if (a1.x > 0.0 && b1.x > 0.0 && g1.x > 0.0 && d1.x > 0.0) m1.xy = mezcla(a1.xy, b1.xy, g1.xy, d1.xy, f); \
  if (a1.z > -1.0 && b1.z > -1.0 && g1.z > -1.0 && d1.z > -1.0) m1.zw = mezcla(a1.zw, b1.zw, g1.zw, d1.zw, f); \
}

INTERPOLAR(del_alto, mapa)
INTERPOLAR(del_bajo, mapa_bajo)

// La dirección del cielo (unitaria; el azimut de la cámara la gira) en el
// punto q del mapa que se usa, si el rayo escapa
bool cielo_en(float2 q, bool alto, int cols, int filas, out float3 d) {
  float4 m0, m1;
  if (alto) del_alto(q, cols, filas, m0, m1); else del_bajo(q, cols, filas, m0, m1);
  float th = m1.z, ph = m1.w + orbita.x;
  d = float3(sin(th) * cos(ph), sin(th) * sin(ph), cos(th));
  return m1.z > -1.0;
}

// El cielo del píxel : su dirección en el centro y cuánto cambia hacia el
// píxel de al lado y el de abajo (la lente), y el ángulo sólido del píxel
// visto desde la cámara (para el aumento)
float3 cielo_del_pixel(uint2 px, bool alto, uint r, int cols, int filas) {
  float2 q = (float2(px) + 0.5) / float(r) - 0.5;
  float3 d, dx, dy;
  if (!cielo_en(q, alto, cols, filas, d) || !cielo_en(q + float2(1.0 / float(r), 0.0), alto, cols, filas, dx) || !cielo_en(q + float2(0.0, 1.0 / float(r)), alto, cols, filas, dy)) return float3(0.0, 0.0, 0.0);
  float2 s = float2((2.0 * (float(px.x) + 0.5) / float(tam.x) - 1.0) * arriba.w, 1.0 - 2.0 * (float(px.y) + 0.5) / float(tam.y)) * derecha.w;
  float2 ds = float2(2.0 * arriba.w / float(tam.x), 2.0 / float(tam.y)) * derecha.w;
  float k = 1.0 + dot(s, s);
  return color_del_cielo(d, dx - d, dy - d, ds.x * ds.y / (k * sqrt(k)));
}

// Cada píxel, con 4 submuestras (en los cuartos del píxel) : el borde de la
// esfera negra y de las bandas del fluido, suaves y sin escalera
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= tam.x || id.y >= tam.y) return;
  // las filas ya listas del mapa alto; el resto, del bajo
  bool alto = float(id.y) < orbita.z;
  uint r = alto ? 1u : uint(orbita.y);
  int cols = int((tam.x + r - 1u) / r);
  int filas = int((tam.y + r - 1u) / r);
  // ¿es borde del mapa que se usa aquí? (entonces, sus subrayos)
  uint e = SIN_BORDE;
  if ((alto && orbita.w == 2.0) || (!alto && orbita.w == 1.0)) e = indice_bordes[(id.y / r) * uint(cols) + id.x / r];
  float3 suma = float3(0.0, 0.0, 0.0);
  float cielo = 0.0, se_ve;
  if (e != SIN_BORDE) {
    // sus 4 subrayos (en el mapa bajo, la entrada de este píxel en su bloque)
    e += (id.x % r) + r * (id.y % r);
    for (uint s = 0u; s < 4u; ++s) { suma += color_de(bordes[(e * 4u + s) * 2u], bordes[(e * 4u + s) * 2u + 1u], se_ve); cielo += se_ve; }
  } else {
    for (uint s = 0u; s < 4u; ++s) {
      float2 q = (float2(id.xy) + float2(0.25 + 0.5 * float(s & 1u), 0.25 + 0.5 * float(s >> 1))) / float(r) - 0.5;
      float4 m0, m1;
      if (alto) del_alto(q, cols, filas, m0, m1); else del_bajo(q, cols, filas, m0, m1);
      suma += color_de(m0, m1, se_ve);
      cielo += se_ve;
    }
  }
  suma *= 0.25;
  // detrás, el cielo, en la parte del píxel por la que se ve
  if (cielo > 0.0) suma += 0.25 * cielo * cielo_del_pixel(id.xy, alto, r, cols, filas);
  // la luz que da ese color tras el tono (1 − e^−luz·exposición) : el
  // disco se ve con los colores de la gama tal cual
  float3 luz_pixel = -log(1.0 - min(suma, 0.996)) / luz.w;
  uint k = (id.y * tam.x + id.x) * 3u;
  acumulado[k] = uint(luz_pixel.r * 256.0 + 0.5);
  acumulado[k + 1u] = uint(luz_pixel.g * 256.0 + 0.5);
  acumulado[k + 2u] = uint(luz_pixel.b * 256.0 + 0.5);
}
)" ;

}  // namespace

bool Componer :: crear(const Gpu & gpu , IBuffer * uniforme , const CamposDisco & campos , const CampoCielo & cielo , std :: string & error) {

	gpu_ = gpu ;

	campos_ = &campos ;

	cielo_ = &cielo ;

	ancho_ = alto_ = 0 ;

	atado_a_ = nullptr ;

	componer_ = gpu_kernel(gpu , { "Agujero componer" , campos.definiciones() + agujero_hlsl + cielo.definiciones() + pintura_hlsl(forma_agujero.pintura) + componer_hlsl , { { "Agujero" , uniforme } } , 1 , 1 } , error) ;

	return static_cast<bool>(componer_.pipeline) ;

}

bool Componer :: ajustar(std :: uint32_t ancho , std :: uint32_t alto , std :: string & error) {

	if (acumulado_ && ancho == ancho_ && alto == alto_) { return true ; }

	// el acumulado, solo si no cabe, y con holgura (la mitad más) : como los
	// mapas de rayos, para que redimensionar no reserve en cada paso. Se
	// escribe entero en cada fotograma, al tamaño del cbuffer

	const Uint64 pixeles = static_cast<Uint64>(ancho) * alto ;

	if (!acumulado_ || pixeles > capacidad_) {

		const Uint64 capacidad = pixeles * 3 / 2 ;

		acumulado_ = gpu_campo(gpu_ , { capacidad * 3 , 4 , "Agujero acumulado" }) ;

		if (!acumulado_) { return gpu_fallo(error , "OUT_OF_GPU_MEMORY: acumulado") ; }

		capacidad_ = capacidad ;

		atado_a_ = nullptr ;

	}

	ancho_ = ancho ;

	alto_ = alto ;

	componer_.grupos_x = (ancho + 15) / 16 ;

	componer_.grupos_y = (alto + 15) / 16 ;

	return true ;

}

void Componer :: dibujar(IDeviceContext * ctx , IBuffer * mapa , IBuffer * bajo , IBuffer * indice_bordes , IBuffer * bordes) {

	if (!mapa || !bajo || !indice_bordes || !bordes || !acumulado_) { return ; }

	// otro mapa (otra ventana) : se vuelve a atar

	if (mapa != atado_a_ || bajo != atado_bajo_ || indice_bordes != atado_indice_) {

		for (int k = 0 ; k < 2 ; k++) {

			std :: vector<Recurso> recursos{

				{ "mapa" , gpu_lectura(mapa) } ,

				{ "mapa_bajo" , gpu_lectura(bajo) } ,

				{ "indice_bordes" , gpu_lectura(indice_bordes) } ,

				{ "bordes" , gpu_lectura(bordes) } ,

				{ "temperatura" , gpu_lectura(campos_->temperatura(k)) } ,

				{ "acumulado" , gpu_escritura(acumulado_) }

			} ;

			// los del cielo, con los nombres que él declara

			for (const Recurso & r : cielo_->recursos()) { recursos.push_back(r) ; }

			componer_b_[k] = gpu_atar(componer_ , recursos) ;

		}

		atado_a_ = mapa ;

		atado_bajo_ = bajo ;

		atado_indice_ = indice_bordes ;

	}

	gpu_lanzar(ctx , componer_ , componer_b_[campos_->actual()]) ;

}

}  // namespace shd
