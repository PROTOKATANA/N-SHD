# include "gota_campo.h"

# include <vector>

# include "gota_comun.h"

# include "pipeline_comun.h"

using namespace Diligent ;

namespace shd :: nodos {

namespace {

// El campo : cada partícula deja su masa y su calor en las celdas que toca, en coma
// fija. Su calor tiende al del remolino : fría fuera, caliente en el ojo, con brazos

constexpr const char * limpiar_campo_hlsl = R"(
RWStructuredBuffer<uint> campo;
RWStructuredBuffer<int> campo_v;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { if (id.x < G * G * 2u) { campo[id.x] = 0u; campo_v[id.x] = 0; } }
)" ;

constexpr const char * salpicar_hlsl = R"(
StructuredBuffer<float4> estado;
StructuredBuffer<float4> medida;
StructuredBuffer<uint> inicio;
StructuredBuffer<uint> cuentas;
StructuredBuffer<uint> lista;
StructuredBuffer<float> calor_ent;
RWStructuredBuffer<float> calor;
RWStructuredBuffer<uint> campo;
RWStructuredBuffer<int> campo_v;
static const float MEZCLA = 12.0;  // 1/s : lo que se iguala con sus vecinas
static const float VUELTA = 0.6;   // 1/s : lo que vuelve al calor del remolino por dentro
static const float BORDE = 4.0;    // 1/s : y en el borde, por donde entra el frío
static const float OJO = 5.0;      // 1/s : y en el ojo, que nunca se apaga
static const float SUELO_OJO = 0.05;  // radios : hasta aquí, siempre rojo
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint i = id.x;
  if (i >= N) return;
  float2 x = estado[i].xy;
  float dt = ritmo.y;
  // el calor se reparte con las vecinas : sin esto, cada partícula lleva el suyo y se
  // ve el grano. Se lee el del fotograma anterior : leer el de las demás mientras se
  // escribe cambiaba en cada ejecución
  float suma = 0.0, pesos = 0.0;
  VECINOS(x, {
    uint j = lista[m];
    float2 e = x - estado[j].xy;
    float r2 = dot(e, e);
    if (r2 < H2) {
      float w = w_poly6(r2);
      suma += calor_ent[j] * w;
      pesos += w;
    }
  })
  float t = lerp(calor_ent[i], suma / max(pesos, 1e-5), 1.0 - exp(-MEZCLA * dt));
  // como en un remolino, el frío entra por el borde y dentro manda el flujo : el
  // calor lo lleva el líquido, y lo que remueva un golpe se ve mezclarse antes de
  // asentarse. El ojo se recalienta enseguida : por mucho que se remueva, queda rojo
  float2 d = x - medida[0].xy;
  float rho = length(d);
  float vuelta = lerp(VUELTA, BORDE, smoothstep(0.75, 0.95, rho));
  vuelta = lerp(OJO, vuelta, smoothstep(0.07, 0.20, rho));
  t = lerp(t, calor_remolino(d, paso.y), 1.0 - exp(-vuelta * dt));
  // si el líquido cruza el ojo más deprisa de lo que se calienta, este suelo lo garantiza
  t = max(t, 1.05 * (1.0 - smoothstep(SUELO_OJO, SUELO_OJO + 0.06, rho)));
  calor[i] = t;
  float2 g = (x + VISTA) / (2.0 * VISTA) * float(G);
  int2 c = int2(floor(g));
  for (int y = -ALCANCE; y <= ALCANCE; ++y) {
    for (int z = -ALCANCE; z <= ALCANCE; ++z) {
      int2 e = c + int2(z, y);
      if (any(e < 0) || any(e >= int(G))) continue;
      float2 u = g - float2(e) - 0.5;
      float q = saturate(1.0 - dot(u, u) / float(ALCANCE * ALCANCE));
      uint masa = uint(q * q * 256.0);
      if (masa == 0u) continue;
      uint k = (uint(e.y) * G + uint(e.x)) * 2u;
      InterlockedAdd(campo[k], masa);
      InterlockedAdd(campo[k + 1u], uint(float(masa) * saturate(t)));
      // y su velocidad, para el fluido de los canales
      InterlockedAdd(campo_v[k], int(float(masa) * estado[i].z * 1000.0));
      InterlockedAdd(campo_v[k + 1u], int(float(masa) * estado[i].w * 1000.0));
    }
  }
}
)" ;

}  // namespace

bool CampoGota :: crear(

	const Gpu & gpu ,

	const DescGota & desc ,

	IBuffer * uniforme ,

	const std :: string & hlsl ,

	const ParticulasGota :: Lecturas & particulas ,

	std :: string & error

){

	gpu_ = gpu ;

	const Uint64 celdas = static_cast<Uint64>(desc.lado_campo) * desc.lado_campo * 2 ;

	campo_ = gpu_campo(gpu_ , { celdas , 4 , "Nodos gota campo" }) ;

	campo_v_ = gpu_campo(gpu_ , { celdas , 4 , "Nodos gota campo velocidad" }) ;

	if (!campo_ || !campo_v_) { return gpu_fallo(error , "No se pudieron crear los campos de la gota") ; }

	const std :: vector<Recurso> fijos{ { "Gota" , uniforme } } ;

	limpiar_ = gpu_kernel(gpu_ , { "Nodos gota limpiar campo" , hlsl + limpiar_campo_hlsl , fijos , grupos_gota(desc.lado_campo * desc.lado_campo * 2) , 1 } , error) ;

	if (!limpiar_.pipeline.RawPtr()) { return false ; }

	salpicar_ = gpu_kernel(gpu_ , { "Nodos gota salpicar" , hlsl + salpicar_hlsl , fijos , grupos_gota(desc.particulas) , 1 } , error) ;

	if (!salpicar_.pipeline.RawPtr()) { return false ; }

	limpiar_b_ = gpu_atar(limpiar_ , { { "campo" , gpu_escritura(campo_) } , { "campo_v" , gpu_escritura(campo_v_) } }) ;

	for (int c = 0 ; c < 2 ; c++) { calor_[c] = particulas.calor[c] ; }

	for (int k = 0 ; k < 2 ; k++) {

		for (int c = 0 ; c < 2 ; c++) {

			salpicar_b_[k][c] = gpu_atar(salpicar_ , {

				{ "estado" , gpu_lectura(particulas.estado[k]) } ,

				{ "medida" , gpu_lectura(particulas.medida) } ,

				{ "inicio" , gpu_lectura(particulas.inicio) } ,

				{ "cuentas" , gpu_lectura(particulas.cuentas) } ,

				{ "lista" , gpu_lectura(particulas.lista) } ,

				{ "calor_ent" , gpu_lectura(calor_[c]) } ,

				{ "calor" , gpu_escritura(calor_[1 - c]) } ,

				{ "campo" , gpu_escritura(campo_) } ,

				{ "campo_v" , gpu_escritura(campo_v_) }

			}) ;

		}

	}

	return true ;

}

void CampoGota :: avanzar(IDeviceContext * ctx , int actual , Medidor * banco , std :: size_t primera) {

	Tramo t(banco , primera , Etapa :: subir , gpu_.device , ctx) ;

	gpu_lanzar(ctx , limpiar_ , limpiar_b_ , { campo_ }) ;

	gpu_lanzar(ctx , salpicar_ , salpicar_b_[actual][calor_actual_] , { calor_[1 - calor_actual_] , campo_ , campo_v_ }) ;

	calor_actual_ = 1 - calor_actual_ ;

}

}  // namespace shd :: nodos
