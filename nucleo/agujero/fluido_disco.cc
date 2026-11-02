# include "fluido_disco.h"

# include "numero_hlsl.h"

# include "gpu.h"

# include <algorithm>

# include <array>

# include <cmath>

# include "shd/src/agujero/puente.rs.h"

# include "forma_agujero.h"

using namespace Diligent ;

namespace shd {

namespace {

constexpr const FluidoForma & forma = forma_agujero.fluido ;

constexpr Uint32 ancho = forma.ancho ;

constexpr Uint32 alto = forma.alto ;

// Jacobi : VUELTAS iteraciones por lanzamiento, en bloques de LADO × LADO
// (ver jacobi_hlsl). Un número par de lanzamientos : la presión acaba en su
// primer buffer

constexpr Uint32 lado_jacobi = 16 ;

constexpr Uint32 vueltas_jacobi = 3 ;

static_assert(ancho % lado_jacobi == 0 && alto % lado_jacobi == 0 , "Jacobi va en bloques enteros") ;

static_assert(forma.jacobi % vueltas_jacobi == 0 && (forma.jacobi / vueltas_jacobi) % 2 == 0 , "Jacobi : lanzamientos enteros y en número par") ;

// El cbuffer «Fluido»

struct FluidoGpu {

	float paso[4] ;  // dt (s) , t (s) , 0 , 0

	float viento[4] ;  // velocidad (celdas/s) , escala (celdas) , relajar (1/s) , cambio

	float hacia_patron[4] ;  // la temperatura tiende a su patrón : relajar (1/s) , escala del patrón (celdas) , deriva , 0

	float kepler[4] ;  // el giro del disco : escala (celdas/s) , ln r de la primera fila , ln r por fila , a

	float vortice[4] ;  // como el vórtice de nodos : lo que cae hacia el ojo (fracción del giro) , el refuerzo de los remolinos , la temperatura del borde y la del ojo

	float agitar[4] ;  // el doble clic : la velocidad de los chorros (celdas/s) , lo rápido que el fluido los toma (1/s) , lo agitado , cuántos dobles clics

} ;

// Lo común a los kernels : la rejilla (el ángulo da la vuelta, el radio
// no), el cbuffer, el azar, un ruido suave que da la vuelta al disco sin
// costura (su red tiene un número entero de celdas por vuelta) y el
// muestreo bilineal de un campo. Los buffers no se pueden
// pasar a funciones en el HLSL que compila glslang : MUESTREADOR escribe una
// función por campo

constexpr const char * comun_hlsl = R"(
cbuffer Fluido {
  float4 paso;      // dt (s), t (s), 0, 0
  float4 viento;    // velocidad (celdas/s), escala (celdas), relajar (1/s), cambio
  float4 hacia_patron;  // la temperatura tiende a su patrón: relajar (1/s), escala del patrón (celdas), deriva, 0
  float4 kepler;    // el giro del disco: escala (celdas/s), ln r de la primera fila, ln r por fila, a
  float4 vortice;   // lo que cae hacia el ojo (fracción del giro), el refuerzo de los remolinos, temperatura del borde y del ojo
  float4 agitar;    // el doble clic: velocidad de los chorros (celdas/s), lo rápido que se toman (1/s), lo agitado, cuántos dobles clics
};

static const float TAU = 6.2831853;

static const float PI = 3.1415927;

uint mezclar(uint x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

float azar(uint x) { return float(mezclar(x) >> 8) * (1.0 / 16777216.0); }

uint indice(int2 c) { return uint(c.y) * uint(ANCHO) + uint(c.x); }

// el ángulo da la vuelta; el radio se queda en la rejilla
int2 envolver(int2 c) { return int2((c.x % ANCHO + ANCHO) % ANCHO, clamp(c.y, 0, ALTO - 1)); }

float2 gradiente(int2 c, uint semilla, int periodo) {
  uint x = uint((c.x % periodo + periodo) % periodo);
  float a = azar((x * 73856093u) ^ (uint(c.y) * 19349663u) ^ (semilla * 83492791u)) * TAU;
  return float2(cos(a), sin(a));
}

// Ruido de gradiente, más o menos entre -1 y 1; `periodo`: celdas de su red
// por vuelta
float ruido(float2 p, uint semilla, int periodo) {
  float2 b = floor(p);
  int2 i = int2(b);
  float2 f = p - b;
  float2 u = f * f * (3.0 - 2.0 * f);
  float a = dot(gradiente(i, semilla, periodo), f);
  float c = dot(gradiente(i + int2(1, 0), semilla, periodo), f - float2(1.0, 0.0));
  float d = dot(gradiente(i + int2(0, 1), semilla, periodo), f - float2(0.0, 1.0));
  float e = dot(gradiente(i + int2(1, 1), semilla, periodo), f - float2(1.0, 1.0));
  return lerp(lerp(a, c, u.x), lerp(d, e, u.x), u.y);
}

// El giro del disco en la fila de c : la órbita circular de Kerr, Ω = 1 /
// (r^{3/2} + a), en celdas por segundo a lo largo del ángulo (las columnas).
// Las filas van en ln r : la rejilla es conforme y los remolinos siguen
// redondos; el giro diferencial los estira en vetas
float2 kepler_en(float2 c) {
  float r = exp(kepler.y + (c.y + 0.5) * kepler.z);
  float giro = kepler.x / (pow(r, 1.5) + kepler.w);
  // y cae hacia el ojo (las filas bajan) mientras gira: una espiral, como el
  // vórtice de nodos
  return float2(giro, -vortice.x * giro);
}

// El patrón al que tiende la temperatura en la celda c : su perfil del ojo
// al borde, con manchas de dos escalas de ruido
float patron(int2 c) {
  int periodo = int(round(float(ANCHO) / hacia_patron.y));
  float2 q = (float2(c) + 0.5) / hacia_patron.y;
  // el patrón gira con el disco (cada fila, con su órbita)
  float t = paso.y * hacia_patron.z - kepler_en(float2(c)).x * paso.y / hacia_patron.y;
  float n = ruido(q + float2(t, 0.0), 7u, periodo) + 0.5 * ruido(2.0 * q + float2(2.0 * t, 0.0), 8u, 2 * periodo);
  // la temperatura : fría en el borde, caliente junto al ojo (como el
  // vórtice : el fluido entra frío y se calienta al caer), con manchas
  float u = (float(c.y) + 0.5) / float(ALTO);
  return saturate(lerp(vortice.w, vortice.z, pow(u, PERFIL)) + 0.35 * n);
}

#define MUESTREADOR(nombre, campo, T) \
T nombre(float2 p) { \
  float2 b = floor(p); \
  float2 f = p - b; \
  int2 c = int2(b); \
  T v00 = campo[indice(envolver(c))]; \
  T v10 = campo[indice(envolver(c + int2(1, 0)))]; \
  T v01 = campo[indice(envolver(c + int2(0, 1)))]; \
  T v11 = campo[indice(envolver(c + int2(1, 1)))]; \
  return lerp(lerp(v00, v10, f.x), lerp(v01, v11, f.x), f.y); \
}
)" ;

// Advección de la velocidad, que tiende al viento : el rotacional de un ruido
// que cambia despacio (remolinos sin fuentes ni sumideros). El borde
// interior (la ISCO) y el exterior son pared

constexpr const char * advectar_hlsl = R"(
StructuredBuffer<float2> vel_a;
RWStructuredBuffer<float2> vel_b;
MUESTREADOR(vel_en, vel_a, float2)

// dos escalas que se deslizan en sentidos opuestos: el viento cambia
float potencial(float2 q, float t) {
  int periodo = int(round(float(ANCHO) / viento.y));
  return ruido(q + float2(t, 0.3 * t), 1u, periodo) + 0.5 * ruido(2.0 * q - float2(1.7 * t, 0.0), 2u, 2 * periodo);
}

float2 viento_en(int2 c) {
  float2 q = (float2(c) + 0.5) / viento.y;
  float t = paso.y * viento.w;
  float e = 0.02;
  float dx = potencial(q + float2(e, 0.0), t) - potencial(q - float2(e, 0.0), t);
  float dy = potencial(q + float2(0.0, e), t) - potencial(q - float2(0.0, e), t);
  return float2(dy, -dx) / (2.0 * e) * 0.5 * viento.x;
}

// la vorticidad de la velocidad del paso anterior en la celda c
float rotacional(int2 c) {
  float2 dx = vel_a[indice(envolver(c + int2(1, 0)))] - vel_a[indice(envolver(c - int2(1, 0)))];
  float2 dy = vel_a[indice(envolver(c + int2(0, 1)))] - vel_a[indice(envolver(c - int2(0, 1)))];
  return 0.5 * (dx.y - dy.x);
}

// Los chorros del doble clic : como el viento, pero más pequeños, más
// rápidos y otros en cada doble clic. Sin escala (a 1 celda/s de pico)
float2 chorros_en(int2 c) {
  int periodo = int(ANCHO / ESCALA_CHORROS);
  float2 q = (float2(c) + 0.5) / float(ESCALA_CHORROS);
  float t = paso.y * CAMBIO_CHORROS;
  uint s = 5u + 2u * uint(agitar.w);
  float e = 0.02;
  float dx = ruido(q + float2(e, 0.0) + float2(t, -0.6 * t), s, periodo) - ruido(q - float2(e, 0.0) + float2(t, -0.6 * t), s, periodo);
  float dy = ruido(q + float2(0.0, e) + float2(t, -0.6 * t), s, periodo) - ruido(q - float2(0.0, e) + float2(t, -0.6 * t), s, periodo);
  float2 a = float2(dy, -dx) / (2.0 * e);
  dx = ruido(2.0 * q + float2(e, 0.0) - float2(1.3 * t, 0.0), s + 1u, 2 * periodo) - ruido(2.0 * q - float2(e, 0.0) - float2(1.3 * t, 0.0), s + 1u, 2 * periodo);
  dy = ruido(2.0 * q + float2(0.0, e) - float2(1.3 * t, 0.0), s + 1u, 2 * periodo) - ruido(2.0 * q - float2(0.0, e) - float2(1.3 * t, 0.0), s + 1u, 2 * periodo);
  return (a + 0.5 * float2(dy, -dx) / (2.0 * e)) * 0.5;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  float dt = paso.x;
  float2 v = vel_a[indice(c)];
  float2 nv = vel_en(float2(c) - (v + kepler_en(float2(c))) * dt);
  // tiende al viento; agitado, además a los chorros (y los toma más deprisa)
  float2 objetivo = viento_en(c);
  if (agitar.z > 0.0) objetivo += agitar.x * chorros_en(c);
  nv += (objetivo - nv) * (1.0 - exp(-(viento.z + agitar.y) * dt));
  // vorticity confinement (como el fluido de nodos): refuerza los
  // remolinos pequeños que la advección iría borrando (con la vorticidad del
  // paso anterior)
  float2 grad = 0.5 * float2(abs(rotacional(c + int2(1, 0))) - abs(rotacional(c - int2(1, 0))), abs(rotacional(c + int2(0, 1))) - abs(rotacional(c - int2(0, 1))));
  nv += vortice.y * dt * float2(grad.y, -grad.x) / (length(grad) + 1e-5) * rotacional(c);
  if (c.y == 0 || c.y == ALTO - 1) nv.y = 0.0;
  vel_b[indice(c)] = nv;
}
)" ;

// La divergencia (diferencias centradas); más allá de los bordes no hay flujo

constexpr const char * divergir_hlsl = R"(
StructuredBuffer<float2> vel;
RWStructuredBuffer<float> div;

float2 v_en(int2 c) {
  if (c.y < 0 || c.y >= ALTO) return float2(0.0, 0.0);
  return vel[indice(envolver(c))];
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  div[indice(c)] = 0.5 * (v_en(c + int2(1, 0)).x - v_en(c - int2(1, 0)).x + v_en(c + int2(0, 1)).y - v_en(c - int2(0, 1)).y);
}
)" ;

// Jacobi de la presión (∇²p = div); en los bordes interior y exterior, la
// presión no cambia al cruzarlos (Neumann). VUELTAS iteraciones por
// lanzamiento : cada grupo carga su bloque de LADO × LADO celdas con un halo
// de VUELTAS en memoria compartida, itera ahí (el halo se va gastando) y
// escribe solo el centro. Cada celda hace la misma suma en el mismo orden que
// una iteración suelta : el resultado es el mismo, con muchas menos pasadas
// por la memoria

constexpr const char * jacobi_hlsl = R"(
StructuredBuffer<float> p_a;
StructuredBuffer<float> div;
RWStructuredBuffer<float> p_b;

#define BLOQUE (LADO + 2 * VUELTAS)
groupshared float presion[2][BLOQUE * BLOQUE];
groupshared float divergencia[BLOQUE * BLOQUE];

[numthreads(LADO, LADO, 1)]
void main(uint3 grupo : SV_GroupID, uint3 local : SV_GroupThreadID, uint hilo : SV_GroupIndex) {
  int2 origen = int2(grupo.xy) * LADO - VUELTAS;
  for (int j = int(hilo); j < BLOQUE * BLOQUE; j += LADO * LADO) {
    int2 c = origen + int2(j % BLOQUE, j / BLOQUE);
    if (c.y >= 0 && c.y < ALTO) {
      uint i = indice(envolver(c));
      presion[0][j] = p_a[i];
      divergencia[j] = div[i];
    }
  }
  GroupMemoryBarrierWithGroupSync();
  // cada vuelta, de presion[a] a presion[b], en las celdas a v del borde del
  // bloque (las de más fuera ya no hacen falta)
  [unroll] for (int v = 1; v <= VUELTAS; ++v) {
    int a = (v - 1) & 1, b = v & 1, lado = BLOQUE - 2 * v;
    for (int j = int(hilo); j < lado * lado; j += LADO * LADO) {
      int2 t = int2(v + j % lado, v + j / lado);
      int2 c = origen + t;
      if (c.y < 0 || c.y >= ALTO) continue;
      int k = t.y * BLOQUE + t.x;
      float pc = presion[a][k];
      float abajo = c.y - 1 < 0 ? pc : presion[a][k - BLOQUE];
      float arriba = c.y + 1 >= ALTO ? pc : presion[a][k + BLOQUE];
      presion[b][k] = (presion[a][k - 1] + presion[a][k + 1] + abajo + arriba - divergencia[k]) * 0.25;
    }
    GroupMemoryBarrierWithGroupSync();
  }
  int2 c = int2(grupo.xy) * LADO + int2(local.xy);
  p_b[indice(c)] = presion[VUELTAS & 1][(int(local.y) + VUELTAS) * BLOQUE + int(local.x) + VUELTAS];
}
)" ;

// La proyección : quitar el gradiente de la presión deja el flujo sin
// divergencia (incompresible)

constexpr const char * proyectar_hlsl = R"(
StructuredBuffer<float> presion;
RWStructuredBuffer<float2> vel;

float p_en(int2 c, float centro) {
  if (c.y < 0 || c.y >= ALTO) return centro;
  return presion[indice(envolver(c))];
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  uint i = indice(c);
  float pc = presion[i];
  float2 gradiente = 0.5 * float2(p_en(c + int2(1, 0), pc) - p_en(c - int2(1, 0), pc), p_en(c + int2(0, 1), pc) - p_en(c - int2(0, 1), pc));
  float2 v = vel[i] - gradiente;
  if (c.y == 0 || c.y == ALTO - 1) v.y = 0.0;
  vel[i] = v;
}
)" ;

// La temperatura : se mueve con el flujo (y el giro), y tiende a su patrón;
// agitado, más despacio

constexpr const char * transportar_hlsl = R"(
StructuredBuffer<float2> vel;
StructuredBuffer<float> temperatura_a;
RWStructuredBuffer<float> temperatura_b;
MUESTREADOR(temperatura_en, temperatura_a, float)

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  uint i = indice(c);
  float dt = paso.x;
  float2 v = vel[i];
  float k = temperatura_en(float2(c) - (v + kepler_en(float2(c))) * dt);
  // agitado, la temperatura vuelve más despacio a sus bandas : se ve revuelta
  temperatura_b[i] = saturate(k + (patron(c) - k) * (1.0 - exp(-hacia_patron.x / (1.0 + agitar.z) * dt)));
}
)" ;

// Al arrancar, la temperatura ya es su patrón : el disco está desde el
// primer fotograma

constexpr const char * iniciar_hlsl = R"(
RWStructuredBuffer<float> temperatura_0;
RWStructuredBuffer<float> temperatura_1;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  uint i = indice(c);
  float k = patron(c);
  temperatura_0[i] = k;
  temperatura_1[i] = k;
}
)" ;

// El giro del disco (ver kepler_en) : el borde interior (la ISCO) da la
// vuelta en periodo_interior s; las filas, del logaritmo de la ISCO al del
// borde exterior

std :: array<float , 4> kepler() {

	const double a = forma_agujero.kerr.giro , r_in = agujero :: isco(a) ;

	const double ln_in = std :: log(r_in) , ln_out = std :: log(forma_agujero.kerr.r_out) ;

	const double omega_in = 1 / (std :: pow(r_in , 1.5) + a) ;

	return { static_cast<float>(ancho / (forma_agujero.disco.periodo_interior * omega_in)) , static_cast<float>(ln_in) , static_cast<float>((ln_out - ln_in) / alto) , static_cast<float>(a) } ;

}

}  // namespace

std :: string FluidoDisco :: definiciones() const {

	return "#define REJILLA_ANCHO " + std :: to_string(ancho) + "\n#define REJILLA_ALTO " + std :: to_string(alto) + "\n" ;

}

bool FluidoDisco :: crear(const Gpu & gpu , std :: string & error) {

	gpu_ = gpu ;

	actual_ = 0 ;

	iniciado_ = false ;

	if (!crear_campos(error) || !crear_kernels(error)) { return false ; }

	atar() ;

	return true ;

}

bool FluidoDisco :: crear_campos(std :: string & error) {

	BufferDesc desc ;

	desc.Name = "Agujero fluido uniforms" ;

	desc.Size = sizeof(FluidoGpu) ;

	desc.Usage = USAGE_DEFAULT ;

	desc.BindFlags = BIND_UNIFORM_BUFFER ;

	desc.ImmediateContextMask = gpu_.mascara ;

	gpu_.device->CreateBuffer(desc , nullptr , &uniforme_) ;

	const Uint64 celdas = static_cast<Uint64>(ancho) * alto ;

	for (int k = 0 ; k < 2 ; k++) {

		velocidad_[k] = gpu_campo(gpu_ , { celdas , 8 , "Agujero fluido velocidad" }) ;

		temperatura_[k] = gpu_campo(gpu_ , { celdas , 4 , "Agujero fluido temperatura" }) ;

		presion_[k] = gpu_campo(gpu_ , { celdas , 4 , "Agujero fluido presion" }) ;

	}

	divergencia_ = gpu_campo(gpu_ , { celdas , 4 , "Agujero fluido divergencia" }) ;

	const bool bien = uniforme_ && velocidad_[0] && velocidad_[1] && temperatura_[0] && temperatura_[1] && presion_[0] && presion_[1] && divergencia_ ;

	return bien || gpu_fallo(error , "OUT_OF_GPU_MEMORY: el fluido del disco") ;

}

bool FluidoDisco :: crear_kernels(std :: string & error) {

	// con numero_hlsl : to_string de un float pone coma decimal con una
	// configuración regional en español, y el HLSL no compila

	const std :: string cambio = numero_hlsl(forma_agujero.agitacion.cambio) , perfil = numero_hlsl(forma_agujero.disco.perfil) ;

	const std :: string base = "#define ANCHO " + std :: to_string(ancho) + "\n#define ALTO " + std :: to_string(alto) + "\n#define ESCALA_CHORROS " + std :: to_string(forma_agujero.agitacion.escala) + "\n#define CAMBIO_CHORROS " + cambio + "\n#define PERFIL " + perfil + "\n" + comun_hlsl ;

	const std :: vector<Recurso> fijos{ { "Fluido" , uniforme_ } } ;

	const Uint32 gx = ancho / 8 , gy = alto / 8 ;

	advectar_ = gpu_kernel(gpu_ , { "Agujero fluido advectar" , base + advectar_hlsl , fijos , gx , gy } , error) ;

	if (!advectar_.pipeline) { return false ; }

	divergir_ = gpu_kernel(gpu_ , { "Agujero fluido divergencia" , base + divergir_hlsl , fijos , gx , gy } , error) ;

	if (!divergir_.pipeline) { return false ; }

	const std :: string bloque = "#define LADO " + std :: to_string(lado_jacobi) + "\n#define VUELTAS " + std :: to_string(vueltas_jacobi) + "\n" ;

	jacobi_ = gpu_kernel(gpu_ , { "Agujero fluido jacobi" , base + bloque + jacobi_hlsl , fijos , ancho / lado_jacobi , alto / lado_jacobi } , error) ;

	if (!jacobi_.pipeline) { return false ; }

	proyectar_ = gpu_kernel(gpu_ , { "Agujero fluido proyectar" , base + proyectar_hlsl , fijos , gx , gy } , error) ;

	if (!proyectar_.pipeline) { return false ; }

	transportar_ = gpu_kernel(gpu_ , { "Agujero fluido transportar" , base + transportar_hlsl , fijos , gx , gy } , error) ;

	if (!transportar_.pipeline) { return false ; }

	iniciar_ = gpu_kernel(gpu_ , { "Agujero fluido iniciar" , base + iniciar_hlsl , fijos , gx , gy } , error) ;

	return static_cast<bool>(iniciar_.pipeline) ;

}

// El paso de paridad k lee la velocidad y la temperatura de k y deja las
// nuevas en 1 − k; la presión de Jacobi va de 0 a 1 y vuelve (un número par de
// veces)

void FluidoDisco :: atar() {

	for (int k = 0 ; k < 2 ; k++) {

		const int o = 1 - k ;

		advectar_b_[k] = gpu_atar(advectar_ , { { "vel_a" , gpu_lectura(velocidad_[k]) } , { "vel_b" , gpu_escritura(velocidad_[o]) } }) ;

		divergir_b_[k] = gpu_atar(divergir_ , { { "vel" , gpu_lectura(velocidad_[o]) } , { "div" , gpu_escritura(divergencia_) } }) ;

		jacobi_b_[k] = gpu_atar(jacobi_ , { { "p_a" , gpu_lectura(presion_[k]) } , { "div" , gpu_lectura(divergencia_) } , { "p_b" , gpu_escritura(presion_[o]) } }) ;

		proyectar_b_[k] = gpu_atar(proyectar_ , { { "presion" , gpu_lectura(presion_[0]) } , { "vel" , gpu_escritura(velocidad_[o]) } }) ;

		transportar_b_[k] = gpu_atar(transportar_ , {

			{ "vel" , gpu_lectura(velocidad_[o]) } ,

			{ "temperatura_a" , gpu_lectura(temperatura_[k]) } ,

			{ "temperatura_b" , gpu_escritura(temperatura_[o]) }

		}) ;

	}

	iniciar_b_ = gpu_atar(iniciar_ , {

		{ "temperatura_0" , gpu_escritura(temperatura_[0]) } ,

		{ "temperatura_1" , gpu_escritura(temperatura_[1]) }

	}) ;

}

void FluidoDisco :: subir(IDeviceContext * ctx , float dt , float t) {

	FluidoGpu datos{

		{ dt , t , 0 , 0 } ,

		{ forma.viento , static_cast<float>(forma.escala) , forma.relajar , forma.cambio } ,

		{ forma.relajar_temperatura , static_cast<float>(forma.escala_patron) , forma.deriva , 0 } ,

		{ 0 , 0 , 0 , 0 } ,

		{ forma_agujero.disco.caida , forma_agujero.disco.confinamiento , forma_agujero.disco.temperatura_borde , forma_agujero.disco.temperatura_ojo } ,

		{ agitado_ * forma_agujero.agitacion.fuerza , agitado_ * forma_agujero.agitacion.agarre , agitado_ , static_cast<float>(golpes_ % 4096) }

	} ;

	const std :: array<float , 4> k = kepler() ;

	std :: copy(k.begin() , k.end() , datos.kepler) ;

	ctx->UpdateBuffer(uniforme_ , 0 , sizeof(datos) , &datos , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

}

void FluidoDisco :: avanzar(IDeviceContext * ctx , float dt , float t) {

	if (!iniciado_) {

		// la temperatura en su patrón, y luego unos segundos de flujo antes
		// del primer fotograma : arranca ya asentado (sin medir : no es un
		// paso de los de cada fotograma)

		const float pasito = std :: max(forma.paso_asentar , 1.0e-3f) ;

		const Uint32 pasos = static_cast<Uint32>(std :: max(forma.asentar , 0.0f) / pasito + 0.5f) ;

		subir(ctx , pasito , t - pasos * pasito) ;

		gpu_lanzar(ctx , iniciar_ , iniciar_b_) ;

		for (Uint32 n = pasos ; n > 0 ; n--) { paso(ctx , pasito , t - n * pasito , nullptr) ; }

		iniciado_ = true ;

	}

	paso(ctx , dt , t , banco_) ;

	// lista para que la lea quien pinta el disco

	StateTransitionDesc leer(temperatura_[actual_] , RESOURCE_STATE_UNKNOWN , RESOURCE_STATE_SHADER_RESOURCE , STATE_TRANSITION_FLAG_UPDATE_STATE) ;

	ctx->TransitionResourceStates(1 , &leer) ;

}

void FluidoDisco :: paso(IDeviceContext * ctx , float dt , float t , Medidor * banco) {

	subir(ctx , dt , t) ;

	const int k = actual_ ;

	const std :: size_t m = primera_medida_ ;

	{

		Tramo tramo(banco , m , Etapa :: dibujar , gpu_.device , ctx) ;

		gpu_lanzar(ctx , advectar_ , advectar_b_[k]) ;

	}

	{

		Tramo tramo(banco , m + 1 , Etapa :: dibujar , gpu_.device , ctx) ;

		gpu_lanzar(ctx , divergir_ , divergir_b_[k]) ;

	}

	{

		Tramo tramo(banco , m + 2 , Etapa :: dibujar , gpu_.device , ctx) ;

		for (Uint32 n = 0 ; n < forma.jacobi / vueltas_jacobi ; n++) { gpu_lanzar(ctx , jacobi_ , jacobi_b_[n % 2]) ; }

	}

	{

		Tramo tramo(banco , m + 3 , Etapa :: dibujar , gpu_.device , ctx) ;

		gpu_lanzar(ctx , proyectar_ , proyectar_b_[k]) ;

	}

	{

		Tramo tramo(banco , m + 4 , Etapa :: dibujar , gpu_.device , ctx) ;

		gpu_lanzar(ctx , transportar_ , transportar_b_[k]) ;

	}

	actual_ = 1 - k ;

}

}  // namespace shd
