# include "rayos_kerr.h"

# include <algorithm>

# include <string>

# include <vector>

# include "cbuffer_agujero.h"

# include "gpu.h"

using namespace Diligent ;

namespace shd {

namespace {

// El rayo del punto px (en píxeles de la ventana, continuo) : sus cruces con
// el disco y su cielo, como se guardan en el mapa (ver rayos_kerr.h)

constexpr const char * rayo_hlsl = R"(
cbuffer Banda { uint4 banda; };  // primera fila, filas, reducción, columnas del mapa

static const uint SIN_BORDE = 0xFFFFFFFFu;

void rayo_en(float2 px, out float4 m0, out float4 m1) {
  float2 s = float2((2.0 * px.x / float(tam.x) - 1.0) * arriba.w, 1.0 - 2.0 * px.y / float(tam.y)) * derecha.w;
  float3 n = normalize(adelante.xyz + s.x * derecha.xyz + s.y * arriba.xyz);
  float3 x = camara.xyz;
  float3 p = momento_desde(x, n);
  float2 c0 = float2(0.0, 0.0), c1 = float2(0.0, 0.0), c2 = float2(0.0, 0.0);
  uint k = 0u;
  bool cae = false;
  int maximo = int(trazado.y);
  float r_min = 1e30;
  for (int i = 0; i < maximo; ++i) {
    float r = metrica(x).r;
    r_min = min(r_min, r);
    // ya rozó : será sombra (sin más disco ni cielo), no hace falta seguirlo
    if (r_min < trazado.w) { cae = true; break; }
    // cae (ver kerr.rs : hacia atrás no cruza el horizonte, se le acerca)
    if (r < kerr.x + trazado.z) { cae = true; break; }
    if (dot(x, x) > kerr.w * kerr.w && dot(x, p) > 0.0) break;
    float3 antes = x;
    // como paso_en en kerr.rs: lejos, más largo
    paso_rk4(x, p, trazado.x * clamp(r - kerr.x, 0.01, 50.0) * max(1.0, r / 8.0));
    // cruza el plano del disco: el punto de cruce, interpolado
    if ((antes.z > 0.0) != (x.z > 0.0) && k < 3u) {
      float t = antes.z / (antes.z - x.z);
      float3 q = float3(lerp(antes.xy, x.xy, t), 0.0);
      float rq = metrica(q).r;
      // el que rozó el agujero (más cerca que trazado.w) ya no recoge disco
      if (rq >= kerr.y && rq <= kerr.z && r_min >= trazado.w) {
        float2 c = float2(rq, atan2(q.y, q.x) + atan2(camara.w, rq));
        if (k == 0u) c0 = c; else if (k == 1u) c1 = c; else c2 = c;
        k++;
      }
    }
  }
  // el que rozó tampoco ve el cielo: la esfera negra, limpia
  cae = cae || r_min < trazado.w;
  float3 d = normalize(p);
  float2 cielo = cae ? float2(-10.0, 0.0) : float2(acos(clamp(d.z, -1.0, 1.0)), atan2(d.y, d.x));
  m0 = float4(c0, c1);
  m1 = float4(c2, cielo);
}
)" ;

constexpr const char * trazar_hlsl = R"(
RWStructuredBuffer<float4> mapa;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint fila = banda.x + id.y;
  uint filas_mapa = (tam.y + banda.z - 1u) / banda.z;
  if (id.x >= banda.w || fila >= filas_mapa || id.y >= banda.y) return;
  // el centro del píxel (en el mapa bajo, el de su bloque)
  float4 m0, m1;
  rayo_en((float2(id.x, fila) + 0.5) * float(banda.z), m0, m1);
  uint i = fila * banda.w + id.x;
  mapa[2u * i] = m0;
  mapa[2u * i + 1u] = m1;
}
)" ;

// Los bordes : donde el mapa cambia de naturaleza entre vecinos (la «firma» :
// qué cruces tiene y si ve el cielo, o un salto grande del primer cruce),
// un rayo por píxel hace escalera. Se apuntan (hasta CAPACIDAD) y se trazan
// 4 subrayos, en los cuartos del píxel, en cada píxel de la ventana del
// borde : en el mapa alto, uno por píxel; en el bajo, los r × r píxeles de
// su bloque (entradas seguidas). Así también se suaviza mientras se mueve

constexpr const char * detectar_hlsl = R"(
StructuredBuffer<float4> mapa;
RWStructuredBuffer<uint> indice;
RWStructuredBuffer<uint> contador;
RWStructuredBuffer<uint2> posicion;

uint firma(float4 a0, float4 a1) { return (a0.x > 0.0 ? 1u : 0u) | (a0.z > 0.0 ? 2u : 0u) | (a1.x > 0.0 ? 4u : 0u) | (a1.z > -1.0 ? 8u : 0u); }

bool distinto(float4 a0, float4 a1, float4 b0, float4 b1) {
  if (firma(a0, a1) != firma(b0, b1)) return true;
  // el primer cruce salta (otra imagen del disco)
  if (a0.x > 0.0 && (abs(b0.x - a0.x) > 0.15 * a0.x || abs(b0.y - a0.y - TAU * round((b0.y - a0.y) / TAU)) > 0.3)) return true;
  return false;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint columnas = banda.w;
  uint filas = (tam.y + banda.z - 1u) / banda.z;
  if (id.x >= columnas || id.y >= filas) return;
  uint i = id.y * columnas + id.x;
  float4 a0 = mapa[2u * i], a1 = mapa[2u * i + 1u];
  bool borde = false;
  int2 v[4] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };
  for (uint k = 0u; k < 4u; ++k) {
    int2 c = int2(id.xy) + v[k];
    if (c.x < 0 || c.y < 0 || c.x >= int(columnas) || c.y >= int(filas)) continue;
    uint j = uint(c.y) * columnas + uint(c.x);
    borde = borde || distinto(a0, a1, mapa[2u * j], mapa[2u * j + 1u]);
  }
  if (!borde) { indice[i] = SIN_BORDE; return; }
  uint r = banda.z;
  uint e;
  InterlockedAdd(contador[0], r * r, e);
  if (e + r * r > CAPACIDAD) { indice[i] = SIN_BORDE; return; }
  indice[i] = e;
  for (uint k = 0u; k < r * r; ++k) posicion[e + k] = id.xy * r + uint2(k % r, k / r);
}
)" ;

constexpr const char * trazar_bordes_hlsl = R"(
RWStructuredBuffer<uint> contador;
RWStructuredBuffer<uint2> posicion;
RWStructuredBuffer<float4> bordes;

// un hilo por subrayo : los rayos de los bordes (los que rozan la sombra)
// son los más largos, y con los 4 en serie en un hilo los grupos acababan
// muy desiguales
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint e = id.x >> 2;
  uint s = id.x & 3u;
  if (e >= min(contador[0], CAPACIDAD)) return;
  float2 c = float2(posicion[e]);
  float2 sub = float2(0.25 + 0.5 * float(s & 1u), 0.25 + 0.5 * float(s >> 1));
  float4 m0, m1;
  rayo_en(c + sub, m0, m1);
  bordes[(e * 4u + s) * 2u] = m0;
  bordes[(e * 4u + s) * 2u + 1u] = m1;
}
)" ;

}  // namespace

bool RayosKerr :: crear(const Gpu & gpu , IBuffer * uniforme , std :: string & error) {

	gpu_ = gpu ;

	ancho_ = alto_ = 0 ;

	BufferDesc desc ;

	desc.Name = "Agujero banda de rayos" ;

	desc.Size = 16 ;

	desc.Usage = USAGE_DEFAULT ;

	desc.BindFlags = BIND_UNIFORM_BUFFER ;

	desc.ImmediateContextMask = gpu_.mascara ;

	gpu.device->CreateBuffer(desc , nullptr , &banda_) ;

	if (!banda_) { return gpu_fallo(error , "No se pudo crear la banda de los rayos") ; }

	const std :: string base = std :: string(agujero_hlsl) + "#define CAPACIDAD " + std :: to_string(capacidad_bordes) + "u\n" + rayo_hlsl ;

	const std :: vector<Recurso> fijos{ { "Agujero" , uniforme } , { "Banda" , banda_ } } ;

	trazar_ = gpu_kernel(gpu , { "Agujero rayos de Kerr" , base + trazar_hlsl , fijos , 1 , 1 } , error) ;

	if (!trazar_.pipeline) { return false ; }

	detectar_ = gpu_kernel(gpu , { "Agujero bordes de los rayos" , base + detectar_hlsl , fijos , 1 , 1 } , error) ;

	if (!detectar_.pipeline) { return false ; }

	trazar_bordes_ = gpu_kernel(gpu , { "Agujero rayos de los bordes" , base + trazar_bordes_hlsl , fijos , capacidad_bordes * 4 / 64 , 1 } , error) ;

	if (!trazar_bordes_.pipeline) { return false ; }

	contador_ = gpu_campo(gpu , { 1 , 4 , "Agujero bordes contador" }) ;

	posicion_ = gpu_campo(gpu , { capacidad_bordes , 8 , "Agujero bordes posición" }) ;

	bordes_ = gpu_campo(gpu , { static_cast<Uint64>(capacidad_bordes) * 8 , 16 , "Agujero bordes rayos" }) ;

	if (!contador_ || !posicion_ || !bordes_) { return gpu_fallo(error , "OUT_OF_GPU_MEMORY: los bordes de los rayos") ; }

	trazar_bordes_b_ = gpu_atar(trazar_bordes_ , { { "contador" , gpu_escritura(contador_) } , { "posicion" , gpu_escritura(posicion_) } , { "bordes" , gpu_escritura(bordes_) } }) ;

	return true ;

}

void RayosKerr :: trazar(IDeviceContext * ctx , IShaderResourceBinding * b , std :: uint32_t r , std :: uint32_t desde , std :: uint32_t filas) {

	const std :: uint32_t columnas = (ancho_ + r - 1) / r ;

	const std :: uint32_t banda[4] = { desde , filas , r , columnas } ;

	ctx->UpdateBuffer(banda_ , 0 , sizeof(banda) , banda , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	trazar_.grupos_x = (columnas + 7) / 8 ;

	trazar_.grupos_y = (filas + 7) / 8 ;

	{

		Tramo tramo(banco_ , primera_medida_ , Etapa :: dibujar , gpu_.device , ctx) ;

		gpu_lanzar(ctx , trazar_ , b) ;

	}

}

void RayosKerr :: bordes_de_mapa(IDeviceContext * ctx , std :: uint32_t cual) {

	const std :: uint32_t r = cual == 2 ? 1 : reduccion_ ;

	const std :: uint32_t columnas = (ancho_ + r - 1) / r , filas = (alto_ + r - 1) / r ;

	const std :: uint32_t cero = 0 , banda[4] = { 0 , filas , r , columnas } ;

	ctx->UpdateBuffer(contador_ , 0 , sizeof(cero) , &cero , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	ctx->UpdateBuffer(banda_ , 0 , sizeof(banda) , banda , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	detectar_.grupos_x = (columnas + 7) / 8 ;

	detectar_.grupos_y = (filas + 7) / 8 ;

	{

		Tramo tramo(banco_ , primera_medida_ + 1 , Etapa :: dibujar , gpu_.device , ctx) ;

		gpu_lanzar(ctx , detectar_ , cual == 2 ? detectar_alto_b_ : detectar_bajo_b_) ;

	}

	// que acabe la lista antes de trazarla (Diligent no pone la barrera entre
	// dos kernels que usan un buffer como UAV)

	StateTransitionDesc barreras[2] = {

		{ contador_ , RESOURCE_STATE_UNORDERED_ACCESS , RESOURCE_STATE_UNORDERED_ACCESS , STATE_TRANSITION_FLAG_UPDATE_STATE } ,

		{ posicion_ , RESOURCE_STATE_UNORDERED_ACCESS , RESOURCE_STATE_UNORDERED_ACCESS , STATE_TRANSITION_FLAG_UPDATE_STATE }

	} ;

	ctx->TransitionResourceStates(2 , barreras) ;

	{

		Tramo tramo(banco_ , primera_medida_ + 2 , Etapa :: dibujar , gpu_.device , ctx) ;

		gpu_lanzar(ctx , trazar_bordes_ , trazar_bordes_b_) ;

	}

	bordes_de_ = cual ;

}

bool RayosKerr :: actualizar(IDeviceContext * ctx , std :: uint32_t ancho , std :: uint32_t alto , bool inclinacion_cambio , std :: uint32_t filas_por_fotograma , std :: uint32_t reduccion , bool * trazado , std :: string & error) {

	* trazado = false ;

	const std :: uint32_t r = std :: max(reduccion , 1u) ;

	// otro tamaño : los dos mapas y, con la inclinación quieta, el alto entero
	// ya

	if (!alto_m_ || ancho != ancho_ || alto != alto_ || r != reduccion_) {

		const Uint64 pixeles = static_cast<Uint64>(ancho) * alto ;

		const Uint64 pixeles_bajo = static_cast<Uint64>((ancho + r - 1) / r) * ((alto + r - 1) / r) ;

		// Los mapas, solo si no caben, y con holgura (la mitad más) : si no,
		// redimensionar los reservaría en cada paso (unos 50 bytes por píxel,
		// con su subida de ceros). Los kernels toman el tamaño del cbuffer, no
		// del buffer

		if (!alto_m_ || pixeles > capacidad_alto_ || pixeles_bajo > capacidad_bajo_) {

			const Uint64 capacidad = pixeles * 3 / 2 , capacidad_bajo = pixeles_bajo * 3 / 2 + 1 ;

			alto_m_ = gpu_campo(gpu_ , { capacidad * 2 , 16 , "Agujero mapa de rayos" }) ;

			bajo_m_ = gpu_campo(gpu_ , { capacidad_bajo * 2 , 16 , "Agujero mapa de rayos bajo" }) ;

			if (!alto_m_ || !bajo_m_) { return gpu_fallo(error , "OUT_OF_GPU_MEMORY: el mapa de rayos") ; }

			indice_ = gpu_campo(gpu_ , { capacidad , 4 , "Agujero bordes índice" }) ;

			if (!indice_) { return gpu_fallo(error , "OUT_OF_GPU_MEMORY: los bordes de los rayos") ; }

			capacidad_alto_ = capacidad ;

			capacidad_bajo_ = capacidad_bajo ;

			alto_b_ = gpu_atar(trazar_ , { { "mapa" , gpu_escritura(alto_m_) } }) ;

			bajo_b_ = gpu_atar(trazar_ , { { "mapa" , gpu_escritura(bajo_m_) } }) ;

			const std :: vector<Recurso> deteccion{ { "indice" , gpu_escritura(indice_) } , { "contador" , gpu_escritura(contador_) } , { "posicion" , gpu_escritura(posicion_) } } ;

			detectar_alto_b_ = gpu_atar(detectar_ , { { "mapa" , gpu_lectura(alto_m_) } , deteccion[0] , deteccion[1] , deteccion[2] }) ;

			detectar_bajo_b_ = gpu_atar(detectar_ , { { "mapa" , gpu_lectura(bajo_m_) } , deteccion[0] , deteccion[1] , deteccion[2] }) ;

		}

		ancho_ = ancho ;

		alto_ = alto ;

		reduccion_ = r ;

		// moviéndose (con la deriva, siempre), el alto entero se daría por
		// perdido en el fotograma siguiente : solo el bajo, el que se ve, como
		// en cualquier fotograma que se mueve. Sin el tirón del alto entero en
		// cada paso de un redimensionado

		if (!inclinacion_cambio) {

			trazar(ctx , alto_b_ , 1 , 0 , alto) ;

			filas_listas_ = alto ;

			bordes_de_mapa(ctx , 2) ;

			* trazado = true ;

			return true ;

		}

	}

	// la inclinación se mueve : el bajo (el alto ya no vale)

	if (inclinacion_cambio) {

		trazar(ctx , bajo_b_ , r , 0 , (alto + r - 1) / r) ;

		filas_listas_ = 0 ;

		bordes_de_mapa(ctx , 1) ;

		return true ;

	}

	// parada : unas filas más del alto

	if (filas_listas_ < alto) {

		const std :: uint32_t filas = std :: min(std :: max(filas_por_fotograma , 1u) , alto - filas_listas_) ;

		trazar(ctx , alto_b_ , 1 , filas_listas_ , filas) ;

		filas_listas_ += filas ;

		// completo : sus bordes

		if (filas_listas_ == alto) { bordes_de_mapa(ctx , 2) ; }

	}

	return true ;

}

}  // namespace shd
