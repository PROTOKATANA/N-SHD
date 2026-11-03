# include "gota_canales.h"

# include "bloque_hlsl.h"

# include "gota_comun.h"

# include "pipeline_comun.h"

using namespace Diligent ;

namespace shd :: nodos {

namespace {

// Iteraciones por fotograma de lo cerca y del alcance (8 celdas) y de la presión de los
// canales, y cuántas hace cada lanzamiento de bloque. Lanzamientos pares : el resultado queda
// en el [0] de cada pareja, que es el que se lee

constexpr Uint32 pasadas_alcance = 8 , iter_alcance = 4 , pasadas_jacobi = 20 , iter_jacobi = 5 ;

static_assert(pasadas_alcance % iter_alcance == 0 && (pasadas_alcance / iter_alcance) % 2 == 0) ;

static_assert(pasadas_jacobi % iter_jacobi == 0 && (pasadas_jacobi / iter_jacobi) % 2 == 0) ;

// el lado del bloque, en celdas : un grupo de lado × lado hilos

constexpr Uint32 lado_bloque = 32 ;

static_assert(lado_canal % lado_bloque == 0) ;

// Qué es cada celda : pared (0), canal (1) o gota (2), y lo que la gota le da

constexpr const char * canal_mascara_hlsl = R"(
StructuredBuffer<uint> campo;
StructuredBuffer<int> campo_v;
StructuredBuffer<float> cerca;
RWStructuredBuffer<uint> masc;
RWStructuredBuffer<float4> impuesto;
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  if (!dentro_c(c)) return;
  float2 x = centro_celda(c);
  int2 q = clamp(int2(floor((x + VISTA) / (2.0 * VISTA) * float(G))), 0, int(G) - 1);
  uint k = (uint(q.y) * G + uint(q.x)) * 2u;
  float m = float(campo[k]);
  float cubre = m / lleno_campo();
  float calor_g = float(campo[k + 1u]) / max(m, 1.0);
  float2 vel = float2(campo_v[k], campo_v[k + 1u]) / (1000.0 * max(m, 1.0));
  bool solido = any(x < bordes.xy) || any(x > bordes.zw);
  for (uint o = 0u; o < uint(inercia.w); ++o) solido = solido || length(x - obstaculos[o].xy) < obstaculos[o].z;
  uint tipo = 0u;
  if (!solido) {
    if (cubre > 0.5) tipo = 2u;
    // los canales, abiertos en embudo donde llegan a la gota
    // solo por los canales : por el halo invisible de la gota, el líquido saltaba a un enlace
    // cercano que no toca, y quedaba un charco suelto sobre él
    else if (embudo(x, cerca[ic(c)]) < 0.0) tipo = 1u;
  }
  masc[ic(c)] = tipo;
  impuesto[ic(c)] = float4(vel, calor_g, cubre);
}
)" ;

// Advección semilagrangiana de la velocidad y del calor; en la gota, lo que da la gota

constexpr const char * canal_advectar_hlsl = R"(
StructuredBuffer<uint> masc;
StructuredBuffer<float4> impuesto;
StructuredBuffer<float2> vel_ent;
StructuredBuffer<float> cal_ent;
RWStructuredBuffer<float2> vel_sal;
RWStructuredBuffer<float> cal_sal;
MUESTRA_C(vel_en, vel_ent, float2)
MUESTRA_CM(cal_en, cal_ent, float, masc, 0.0)
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  if (!dentro_c(c)) return;
  int i = ic(c);
  uint tipo = masc[i];
  if (tipo == 0u) { vel_sal[i] = float2(0.0, 0.0); cal_sal[i] = 0.0; return; }
  float4 dado = impuesto[i];
  if (tipo == 2u) { vel_sal[i] = dado.xy; cal_sal[i] = dado.z; return; }
  float dt = ritmo.y;
  // a lo sumo una celda por paso : así no salta las paredes de un canal estrecho
  float2 paso_c = dt * vel_ent[i] / CELDA_C;
  float largo = length(paso_c);
  if (largo > 1.0) paso_c /= largo;
  float2 atras = float2(c) - paso_c;
  float2 v = vel_en(atras) * exp(-FRENO_C * dt);
  float t = cal_en(atras);
  if (any(isnan(v)) || isnan(t)) { v = float2(0.0, 0.0); t = 0.0; }
  // donde la gota cubre a medias, a medias lo suyo
  float w = saturate(dado.w * 2.0);
  vel_sal[i] = lerp(v, dado.xy, w);
  cal_sal[i] = lerp(t, dado.z, w);
}
)" ;

// Lo lejos que queda cada celda de la gota, en línea recta, sin paredes : con eso la boca de
// cada canal se abre en embudo hacia la gota, siga la forma que siga la gota. Como el
// alcance, cada celda toma la menor de sus vecinas más el paso

// Las tres que iteran sobre la rejilla (cerca, alcance y presión) van en bloques
// (bloque_hlsl.h) : el resultado es el mismo bit a bit, con 2 + 2 + 4 lanzamientos en vez de
// 8 + 8 + 20. Medido (banco del visor) : cerca, de 0,042 a 0,035 ms; alcance, de 0,054 a 0,042;
// presión, de 0,080 a 0,070. Bloques de 32 : con 16 se recalculaba más del doble por el borde

constexpr const char * canal_cerca_hlsl = R"(
StructuredBuffer<uint> masc;
StructuredBuffer<float> cer_ent;
RWStructuredBuffer<float> cer_sal;
groupshared uint b_masc[CELDAS_B];
groupshared float b_cer[CELDAS_B];
[numthreads(T_B, T_B, 1)]
void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID) {
  int2 origen = int2(g.xy) * T - K;
  uint hilo = t.y * uint(T_B) + t.x;
  for (uint s = hilo; s < CELDAS_B; s += HILOS_B) {
    int2 c = celda_b(origen, s);
    b_masc[s] = dentro_c(c) ? masc[ic(c)] : FUERA;
    b_cer[s] = cer_ent[ic(c)];
  }
  GroupMemoryBarrierWithGroupSync();
  for (int it = 0; it < K; ++it) {
    float nuevo[POR_HILO];
    for (uint q = 0u; q < POR_HILO; ++q) {
      uint s = min(hilo + q * HILOS_B, CELDAS_B - 1u);
      nuevo[q] = b_cer[s];
      if (!interior_b(s) || b_masc[s] == FUERA) continue;
      if (b_masc[s] == 2u) { nuevo[q] = 0.0; continue; }
      float d = 1.0;
      for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
          uint e = puesto_b(s, x, y);
          if (x == 0 && y == 0) continue;
          if (b_masc[e] == FUERA) continue;
          d = min(d, b_cer[e] + CELDA_C * ((x != 0 && y != 0) ? 1.41421 : 1.0));
        }
      }
      nuevo[q] = d;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint q = 0u; q < POR_HILO; ++q) { uint s = hilo + q * HILOS_B; if (s < CELDAS_B) b_cer[s] = nuevo[q]; }
    GroupMemoryBarrierWithGroupSync();
  }
  int2 l = int2(t.xy) + K;
  int2 c = origen + l;
  if (dentro_c(c)) cer_sal[ic(c)] = b_cer[uint(l.y * R + l.x)];
}
)" ;

// Hasta dónde llega el líquido : lo lejos que queda cada celda de la gota, recorriendo los
// canales, como el agua que se esparce por donde las paredes la dejan. Cada celda toma la
// menor de sus vecinas más el paso, sin quedarse con la suya : así también crece cuando un
// canal se cierra, y unas pocas iteraciones por fotograma bastan para seguir a los nodos

constexpr const char * canal_alcance_hlsl = R"(
StructuredBuffer<uint> masc;
StructuredBuffer<float4> impuesto;
StructuredBuffer<float2> alc_ent;
RWStructuredBuffer<float2> alc_sal;
groupshared uint b_masc[CELDAS_B];
groupshared float2 b_imp[CELDAS_B];
groupshared float2 b_alc[CELDAS_B];
[numthreads(T_B, T_B, 1)]
void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID) {
  int2 origen = int2(g.xy) * T - K;
  uint hilo = t.y * uint(T_B) + t.x;
  for (uint s = hilo; s < CELDAS_B; s += HILOS_B) {
    int2 c = celda_b(origen, s);
    b_masc[s] = dentro_c(c) ? masc[ic(c)] : FUERA;
    b_imp[s] = impuesto[ic(c)].zw;
    b_alc[s] = alc_ent[ic(c)];
  }
  GroupMemoryBarrierWithGroupSync();
  for (int it = 0; it < K; ++it) {
    float2 nuevo[POR_HILO];
    for (uint q = 0u; q < POR_HILO; ++q) {
      uint s = min(hilo + q * HILOS_B, CELDAS_B - 1u);
      nuevo[q] = b_alc[s];
      uint tipo = b_masc[s];
      if (!interior_b(s) || tipo == FUERA) continue;
      // y con qué color sale de la gota : el de su boca, que cambia al girar el remolino
      // y el borde de la gota también : así el canal sale con el color exacto del borde
      if (tipo == 2u || b_imp[s].y > BORDE_GOTA) { nuevo[q] = float2(0.0, b_imp[s].x); continue; }
      if (tipo == 0u) { nuevo[q] = float2(1e3, 0.0); continue; }
      float2 d = float2(1e3, 0.0);
      for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
          uint e = puesto_b(s, x, y);
          if ((x == 0 && y == 0) || b_masc[e] == FUERA || b_masc[e] == 0u) continue;
          float2 n = b_alc[e];
          float l = n.x + CELDA_C * ((x != 0 && y != 0) ? 1.41421 : 1.0);
          if (l < d.x) d = float2(l, n.y);
        }
      }
      nuevo[q] = d;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint q = 0u; q < POR_HILO; ++q) { uint s = hilo + q * HILOS_B; if (s < CELDAS_B) b_alc[s] = nuevo[q]; }
    GroupMemoryBarrierWithGroupSync();
  }
  int2 l = int2(t.xy) + K;
  int2 c = origen + l;
  if (dentro_c(c)) alc_sal[ic(c)] = b_alc[uint(l.y * R + l.x)];
}
)" ;

// El calor se suaviza un poco con el de sus vecinas : sin esto, el de un canal estrecho
// salta de una celda a otra

constexpr const char * canal_difundir_hlsl = R"(
StructuredBuffer<uint> masc;
StructuredBuffer<float> cal_ent;
RWStructuredBuffer<float> cal_sal;
#define VECINA_C(E) { int2 e = (E); if (dentro_c(e) && masc[ic(e)] != 0u) { s += cal_ent[ic(e)]; n += 1.0; } }
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  if (!dentro_c(c)) return;
  int i = ic(c);
  if (masc[i] != 1u) { cal_sal[i] = cal_ent[i]; return; }
  float s = 0.0, n = 0.0;
  VECINA_C(c + int2(1, 0))
  VECINA_C(c + int2(-1, 0))
  VECINA_C(c + int2(0, 1))
  VECINA_C(c + int2(0, -1))
  cal_sal[i] = n > 0.0 ? lerp(cal_ent[i], s / n, 0.25) : cal_ent[i];
}
)" ;
constexpr const char * canal_divergencia_hlsl = R"(
StructuredBuffer<uint> masc;
StructuredBuffer<float2> vel_ent;
RWStructuredBuffer<float> div_sal;
float2 v_en(int2 c) { return dentro_c(c) && masc[ic(c)] != 0u ? vel_ent[ic(c)] : float2(0.0, 0.0); }
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  if (!dentro_c(c)) return;
  int i = ic(c);
  if (masc[i] == 0u) { div_sal[i] = 0.0; return; }
  int2 dx = int2(1, 0), dy = int2(0, 1);
  div_sal[i] = 0.5 * ((v_en(c + dx).x - v_en(c - dx).x) + (v_en(c + dy).y - v_en(c - dy).y)) / CELDA_C;
}
)" ;

constexpr const char * canal_jacobi_hlsl = R"(
StructuredBuffer<uint> masc;
StructuredBuffer<float> pres_ent;
StructuredBuffer<float> div_ent;
RWStructuredBuffer<float> pres_sal;
groupshared uint b_masc[CELDAS_B];
groupshared float b_div[CELDAS_B];
groupshared float b_pres[CELDAS_B];
// la de la vecina, si es líquido; si no, la propia
float p_b(uint e, float propia) { return b_masc[e] != FUERA && b_masc[e] != 0u ? b_pres[e] : propia; }
[numthreads(T_B, T_B, 1)]
void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID) {
  int2 origen = int2(g.xy) * T - K;
  uint hilo = t.y * uint(T_B) + t.x;
  for (uint s = hilo; s < CELDAS_B; s += HILOS_B) {
    int2 c = celda_b(origen, s);
    b_masc[s] = dentro_c(c) ? masc[ic(c)] : FUERA;
    b_div[s] = div_ent[ic(c)];
    b_pres[s] = pres_ent[ic(c)];
  }
  GroupMemoryBarrierWithGroupSync();
  for (int it = 0; it < K; ++it) {
    float nuevo[POR_HILO];
    for (uint q = 0u; q < POR_HILO; ++q) {
      uint s = min(hilo + q * HILOS_B, CELDAS_B - 1u);
      nuevo[q] = b_pres[s];
      if (!interior_b(s) || b_masc[s] == FUERA) continue;
      if (b_masc[s] == 0u) { nuevo[q] = 0.0; continue; }
      float p = b_pres[s];
      nuevo[q] = (p_b(puesto_b(s, 1, 0), p) + p_b(puesto_b(s, -1, 0), p) + p_b(puesto_b(s, 0, 1), p) + p_b(puesto_b(s, 0, -1), p) - b_div[s] * CELDA_C * CELDA_C) * 0.25;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint q = 0u; q < POR_HILO; ++q) { uint s = hilo + q * HILOS_B; if (s < CELDAS_B) b_pres[s] = nuevo[q]; }
    GroupMemoryBarrierWithGroupSync();
  }
  int2 l = int2(t.xy) + K;
  int2 c = origen + l;
  if (dentro_c(c)) pres_sal[ic(c)] = b_pres[uint(l.y * R + l.x)];
}
)" ;

constexpr const char * canal_proyectar_hlsl = R"(
StructuredBuffer<uint> masc;
StructuredBuffer<float2> vel_ent;
StructuredBuffer<float> pres_ent;
RWStructuredBuffer<float2> vel_sal;
float p_en(int2 c, float propia) { return dentro_c(c) && masc[ic(c)] != 0u ? pres_ent[ic(c)] : propia; }
bool pared(int2 c) { return !dentro_c(c) || masc[ic(c)] == 0u; }
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  if (!dentro_c(c)) return;
  int i = ic(c);
  if (masc[i] == 0u) { vel_sal[i] = float2(0.0, 0.0); return; }
  float p = pres_ent[i];
  int2 dx = int2(1, 0), dy = int2(0, 1);
  float2 v = vel_ent[i] - 0.5 * float2(p_en(c + dx, p) - p_en(c - dx, p), p_en(c + dy, p) - p_en(c - dy, p)) / CELDA_C;
  // no atraviesa la pared
  if (pared(c + dx)) v.x = min(v.x, 0.0);
  if (pared(c - dx)) v.x = max(v.x, 0.0);
  if (pared(c + dy)) v.y = min(v.y, 0.0);
  if (pared(c - dy)) v.y = max(v.y, 0.0);
  vel_sal[i] = v;
}
)" ;

}  // namespace

bool CanalesGota :: crear(

	const Gpu & gpu ,

	IBuffer * uniforme ,

	const std :: string & hlsl ,

	IBuffer * campo ,

	IBuffer * velocidad ,

	std :: string & error

){

	gpu_ = gpu ;

	if (!crear_campos(error) || !crear_kernels(uniforme , hlsl , error)) { return false ; }

	atar(campo , velocidad) ;

	return true ;

}

bool CanalesGota :: crear_campos(std :: string & error) {

	const Uint64 cc = static_cast<Uint64>(lado_canal) * lado_canal ;

	for (int k = 0 ; k < 2 ; k++) {

		cal_[k] = gpu_campo(gpu_ , { cc , 4 , "Nodos gota canal calor" }) ;

		alc_[k] = gpu_campo(gpu_ , { cc , 8 , "Nodos gota canal alcance" }) ;

		cer_[k] = gpu_campo(gpu_ , { cc , 4 , "Nodos gota canal cerca" }) ;

		vel_[k] = gpu_campo(gpu_ , { cc , 8 , "Nodos gota canal velocidad" }) ;

		pres_[k] = gpu_campo(gpu_ , { cc , 4 , "Nodos gota canal presion" }) ;

		if (!cal_[k] || !alc_[k] || !cer_[k] || !vel_[k] || !pres_[k]) { return gpu_fallo(error , "No se pudieron crear los campos de los canales") ; }

	}

	div_ = gpu_campo(gpu_ , { cc , 4 , "Nodos gota canal divergencia" }) ;

	masc_ = gpu_campo(gpu_ , { cc , 4 , "Nodos gota canal mascara" }) ;

	imp_ = gpu_campo(gpu_ , { cc , 16 , "Nodos gota canal impuesto" }) ;

	return (div_ && masc_ && imp_) || gpu_fallo(error , "No se pudieron crear los campos de los canales") ;

}

bool CanalesGota :: crear_kernels(IBuffer * uniforme , const std :: string & hlsl , std :: string & error) {

	const std :: vector<Recurso> fijos{ { "Gota" , uniforme } } ;

	const Uint32 g2 = lado_canal / 8 ;

	const auto kernel_canal = [&](const char * nombre , const char * codigo , Kernel & k) {

		k = gpu_kernel(gpu_ , { nombre , hlsl + codigo , fijos , g2 , g2 } , error) ;

		return k.pipeline.RawPtr() != nullptr ;

	} ;

	// los de bloques : K iteraciones en cada lanzamiento, un grupo por bloque

	const auto kernel_bloque = [&](const char * nombre , const char * codigo , Uint32 iteraciones , Kernel & k) {

		const std :: string con_k = "#define K " + std :: to_string(iteraciones) + "\n#define T_B " + std :: to_string(lado_bloque) + "\n" + bloque_hlsl + codigo ;

		k = gpu_kernel(gpu_ , { nombre , hlsl + con_k , fijos , lado_canal / lado_bloque , lado_canal / lado_bloque } , error) ;

		return k.pipeline.RawPtr() != nullptr ;

	} ;

	return kernel_canal("Nodos gota canal mascara" , canal_mascara_hlsl , mascara_k_)

		&& kernel_canal("Nodos gota canal advectar" , canal_advectar_hlsl , advectar_)

		&& kernel_bloque("Nodos gota canal alcance" , canal_alcance_hlsl , iter_alcance , alcance_k_)

		&& kernel_bloque("Nodos gota canal cerca" , canal_cerca_hlsl , iter_alcance , cerca_k_)

		&& kernel_canal("Nodos gota canal difundir" , canal_difundir_hlsl , difundir_)

		&& kernel_canal("Nodos gota canal divergencia" , canal_divergencia_hlsl , divergencia_)

		&& kernel_bloque("Nodos gota canal jacobi" , canal_jacobi_hlsl , iter_jacobi , jacobi_)

		&& kernel_canal("Nodos gota canal proyectar" , canal_proyectar_hlsl , proyectar_) ;

}

void CanalesGota :: atar(IBuffer * campo , IBuffer * velocidad) {

	mascara_b_ = gpu_atar(mascara_k_ , { { "campo" , gpu_lectura(campo) } , { "campo_v" , gpu_lectura(velocidad) } , { "cerca" , gpu_lectura(cer_[0]) } , { "masc" , gpu_escritura(masc_) } , { "impuesto" , gpu_escritura(imp_) } }) ;

	for (int k = 0 ; k < 2 ; k++) {

		cerca_b_[k] = gpu_atar(cerca_k_ , { { "masc" , gpu_lectura(masc_) } , { "cer_ent" , gpu_lectura(cer_[k]) } , { "cer_sal" , gpu_escritura(cer_[1 - k]) } }) ;

		alcance_b_[k] = gpu_atar(alcance_k_ , { { "masc" , gpu_lectura(masc_) } , { "impuesto" , gpu_lectura(imp_) } , { "alc_ent" , gpu_lectura(alc_[k]) } , { "alc_sal" , gpu_escritura(alc_[1 - k]) } }) ;

		jacobi_b_[k] = gpu_atar(jacobi_ , { { "masc" , gpu_lectura(masc_) } , { "pres_ent" , gpu_lectura(pres_[k]) } , { "div_ent" , gpu_lectura(div_) } , { "pres_sal" , gpu_escritura(pres_[1 - k]) } }) ;

	}

	advectar_b_ = gpu_atar(advectar_ , { { "masc" , gpu_lectura(masc_) } , { "impuesto" , gpu_lectura(imp_) } , { "vel_ent" , gpu_lectura(vel_[0]) } , { "cal_ent" , gpu_lectura(cal_[0]) } , { "vel_sal" , gpu_escritura(vel_[1]) } , { "cal_sal" , gpu_escritura(cal_[1]) } }) ;

	// el calor advectado ([1]) vuelve suavizado a [0]

	difundir_b_ = gpu_atar(difundir_ , { { "masc" , gpu_lectura(masc_) } , { "cal_ent" , gpu_lectura(cal_[1]) } , { "cal_sal" , gpu_escritura(cal_[0]) } }) ;

	divergencia_b_ = gpu_atar(divergencia_ , { { "masc" , gpu_lectura(masc_) } , { "vel_ent" , gpu_lectura(vel_[1]) } , { "div_sal" , gpu_escritura(div_) } }) ;

	proyectar_b_ = gpu_atar(proyectar_ , { { "masc" , gpu_lectura(masc_) } , { "vel_ent" , gpu_lectura(vel_[1]) } , { "pres_ent" , gpu_lectura(pres_[0]) } , { "vel_sal" , gpu_escritura(vel_[0]) } }) ;

}

std :: vector<std :: string> CanalesGota :: tramos() {

	return { "gota canal mascara" , "gota canal cerca" , "gota canal advectar" , "gota canal alcance" , "gota canal presion" } ;

}

void CanalesGota :: avanzar(IDeviceContext * ctx , Medidor * banco , std :: size_t primera) {

	{

		Tramo t(banco , primera , Etapa :: subir , gpu_.device , ctx) ;

		gpu_lanzar(ctx , mascara_k_ , mascara_b_ , { masc_ , imp_ }) ;

	}

	// lo cerca que queda de la gota, para el embudo : 8 celdas por fotograma, y queda en [0]

	{

		Tramo t(banco , primera + 1 , Etapa :: subir , gpu_.device , ctx) ;

		for (Uint32 k = 0 ; k < pasadas_alcance / iter_alcance ; k++) { gpu_lanzar(ctx , cerca_k_ , cerca_b_[k % 2] , { cer_[1 - k % 2] }) ; }

	}

	{

		Tramo t(banco , primera + 2 , Etapa :: subir , gpu_.device , ctx) ;

		gpu_lanzar(ctx , advectar_ , advectar_b_ , { vel_[1] , cal_[1] }) ;

		gpu_lanzar(ctx , difundir_ , difundir_b_ , { cal_[0] }) ;

	}

	// hasta dónde llega : 8 celdas por fotograma, y queda en [0]

	{

		Tramo t(banco , primera + 3 , Etapa :: subir , gpu_.device , ctx) ;

		for (Uint32 k = 0 ; k < pasadas_alcance / iter_alcance ; k++) { gpu_lanzar(ctx , alcance_k_ , alcance_b_[k % 2] , { alc_[1 - k % 2] }) ; }

	}

	{

		Tramo t(banco , primera + 4 , Etapa :: subir , gpu_.device , ctx) ;

		gpu_lanzar(ctx , divergencia_ , divergencia_b_ , { div_ }) ;

		for (Uint32 j = 0 ; j < pasadas_jacobi / iter_jacobi ; j++) { gpu_lanzar(ctx , jacobi_ , jacobi_b_[j % 2] , { pres_[1 - j % 2] }) ; }

		gpu_lanzar(ctx , proyectar_ , proyectar_b_ , { vel_[0] }) ;

	}

}

}  // namespace shd :: nodos
