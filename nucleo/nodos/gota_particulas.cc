# include "gota_particulas.h"

# include "gota_comun.h"

# include "pipeline_comun.h"

using namespace Diligent ;

namespace shd :: nodos {

namespace {

// Las vecinas, ordenadas por celda (contar, sumar los prefijos, repartir y ordenar
// dentro de cada celda por su número) : las de una celda quedan seguidas en memoria,
// y la presión y la velocidad se calculan en ese orden, con sus posiciones ya
// ordenadas. Antes cada celda tenía sitio para 24 y leer la posición de cada vecina
// era un salto por la memoria; además, el orden de llegada de los atómicos cambiaba
// las sumas en cada ejecución. Ahora la gota se repite igual, y en un choque no se
// pierde ninguna vecina

// Al arrancar : un disco uniforme (el girasol), ya con su calor

constexpr const char * sembrar_hlsl = R"(
RWStructuredBuffer<float4> estado;
RWStructuredBuffer<float> calor;
RWStructuredBuffer<float2> empuje;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint i = id.x;
  if (i >= N) return;
  float r = sqrt((float(i) + 0.5) / float(N));
  float a = float(i) * 2.3999632;
  estado[i] = float4(r * cos(a), r * sin(a), 0.0, 0.0);
  calor[i] = calor_remolino(estado[i].xy, 0.0);
  empuje[i] = float2(0.0, 0.0);
}
)" ;

// El centro de masa y su velocidad, con una muestra : en un hilo de GPU, leerlas todas
// es caro, y están repartidas sin orden

// Lo que barre un nodo al moverse : lo que pasa por encima lo empuja por delante, como una
// pala. Sin esto, si el nodo avanzaba más que su tamaño en un fotograma, o si se iba el
// vórtice, el líquido de detrás de una pared de nodos la atravesaba

constexpr const char * barrer_hlsl = R"(
RWStructuredBuffer<float4> estado;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= N) return;
  float2 x = estado[id.x].xy;
  for (uint i = 0u; i < uint(inercia.w); ++i) {
    float2 d = barrido_de(i);
    float largo = length(d);
    if (largo < 1e-6) continue;
    float2 u = d / largo;
    float r = obstaculos[i].z;
    float2 antes = obstaculos[i].xy - d;
    float a = dot(x - antes, u);
    float2 lado = x - antes - u * a;
    float l2 = dot(lado, lado);
    if (l2 >= r * r || a < 0.0) continue;
    float frente = sqrt(r * r - l2);
    if (a < largo + frente) x = obstaculos[i].xy + lado + u * frente;
  }
  estado[id.x].xy = x;
}
)" ;

constexpr const char * medir_hlsl = R"(
StructuredBuffer<float4> estado;
RWStructuredBuffer<float4> medida;
[numthreads(1, 1, 1)]
void main() {
  float4 s = float4(0.0, 0.0, 0.0, 0.0);
  for (uint i = 0u; i < N; i += 16u) s += estado[i];
  medida[0] = s / float(N / 16u);
}
)" ;

// Las fuerzas y la posición prevista

constexpr const char * predecir_hlsl = R"(
RWStructuredBuffer<float4> estado;
StructuredBuffer<float2> empuje;
StructuredBuffer<float4> medida;
RWStructuredBuffer<float2> prevista;
static const float ENVUELVE = 0.25;   // radios : lo que pasa la zona de un nodo de su gota en los enlaces
static const float ATRAE = 160.0;      // radios/s² : lo que atrae un nodo el líquido junto a él
static const float SUELTA = 0.3;      // radios : lo que se aleja el nodo de la gota hasta que lo suelta
static const float RAPIDO = 0.06;     // radios por fotograma de 1/60 s : desde ahí ya no lo envuelve

float giro_en(float rho) { return fuerzas.z * (rho < 0.2 ? rho / 0.2 : pow(0.2 / rho, 0.7)); }

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint i = id.x;
  if (i >= N) return;
  float dt = paso.x;
  float t = paso.y;
  float2 x = estado[i].xy;
  float2 v = estado[i].zw;
  float2 c = medida[0].xy;
  float2 vc = medida[0].zw;
  float2 a = empuje[i];
  // el ancla devuelve el líquido a su nodo tras un choque : no cambia su forma
  a += -fuerzas.x * c - fuerzas.y * vc;
  // cada partícula cae hacia el centro de masa : una gravedad propia que la mantiene
  // unida contra lo que la centrifuga y que trae de vuelta lo que salpica
  float2 d = x - c;
  a -= centro.w * d;
  // el arrastre : el líquido va en el marco del nodo, así que arrastrarlo no lo separa
  // de su enlace, y lo que se queda atrás es el borde. El núcleo tira al otro lado, lo
  // justo para que el centro de masa no se mueva : un disco tiene rho² medio 0,5
  a += inercia.xy * (min(dot(d, d), 1.0) - 0.5);
  // el remolino, alrededor del centro de masa
  float rho = length(d);
  // los canales no los llenan las partículas : en un canal estrecho caben dos o tres a lo
  // ancho, y así se rompen en gotas. Los llena el fluido de los canales (canal_*)
  // fuera del disco la caída crece y frena : lo que salpica vuelve enseguida y se
  // funde, en vez de quedarse flotando en cuentas redondas. Lo que solo se deforma
  // apenas llega ahí
  float fuera = max(rho - 1.05, 0.0);
  if (fuera > 0.0) {
    a -= d / rho * fuera * 200.0;
    v *= exp(-dt * min(fuera * 60.0, 20.0));
  }
  if (rho > 1e-4 && rho < 1.4) {
    float2 tg = float2(-d.y, d.x) / rho;
    a += tg * (giro_en(rho) - dot(v - vc, tg)) * fuerzas.w;
  }
  // tres chorros vivos a la vez, que nacen y se apagan y se relevan
  for (uint j = 0u; j < 3u; ++j) {
    float fase = t / 0.6 + float(j) / 3.0;
    uint s = uint(fase) * 97u + j * 7919u + 12345u;
    float2 p = c + sqrt(azar(s + 1u)) * 0.85 * float2(cos(azar(s) * TAU), sin(azar(s) * TAU));
    float b = azar(s + 2u) * TAU;
    float2 e = x - p;
    a += float2(cos(b), sin(b)) * superficie.w * sin(frac(fase) * PI) * exp(-dot(e, e) / 0.02);
  }
  // la pulsación : siete chorros durante 0,3 s
  if (paso.z < 0.3) {
    uint semilla = uint(paso.w) * 64u;
    for (uint k = 0u; k < 7u; ++k) {
      float g = azar(semilla + k * 4u) * TAU;
      float2 p = c + sqrt(azar(semilla + k * 4u + 1u)) * 0.8 * float2(cos(g), sin(g));
      float b = azar(semilla + k * 4u + 2u) * TAU;
      float2 e = x - p;
      a += float2(cos(b), sin(b)) * inercia.z * exp(-dot(e, e) / 0.02);
    }
  }
  // lo que choca empuja al líquido desde su lado
  for (uint m = 0u; m < 4u; ++m) {
    if (contactos[m].z <= 0.0) continue;
    float2 e = x - (c + contactos[m].xy * 0.8);
    a -= contactos[m].xy * contactos[m].z * exp(-dot(e, e) / 0.25);
  }
  // los nodos son esferas en el laberinto de los canales : cuando el líquido llega a la
  // zona de uno, lo atrae y lo envuelve, más cuanto más cerca de él. Su círculo sólido no
  // lo cruza : eso lo hace chocar, y ahí se sigue viendo el nodo
  for (uint o = 0u; o < uint(inercia.w); ++o) {
    float2 e = x - obstaculos[o].xy;
    float r = length(e);
    float solido = obstaculos[o].z;
    float zona = obstaculos[o].w + ENVUELVE;
    // solo mientras el nodo está dentro de la gota o tocándola : cuando se aleja, la gota
    // lo suelta, y lo que lo envolvía vuelve con ella en vez de quedarse pegado
    float dentro = 1.0 - saturate((length(obstaculos[o].xy) - solido - 1.0) / SUELTA);
    // y si pasa despacio : a toda velocidad el líquido no tiene tiempo de mojarlo, y tirar
    // de él lo estiraría en películas finas que se rompen en gotas
    dentro *= 1.0 - saturate(length(barrido_de(o)) / (max(ritmo.y, 1e-4) * RAPIDO * 60.0));
    if (r < zona && r > 1e-5 && dentro > 0.0) {
      float q = 1.0 - saturate((r - solido) / (zona - solido));
      a -= e / r * ATRAE * q * dentro;
    }
  }
  v += a * dt;
  float rapidez = length(v);
  if (rapidez > superficie.z) v *= superficie.z / rapidez;
  estado[i] = float4(x, v);
  prevista[i] = chocar(x + v * dt);
}
)" ;

// La rejilla de vecinos : limpiar, contar, sumar los prefijos, repartir, ordenar cada celda
// y reunir las posiciones en ese orden

constexpr const char * limpiar_hlsl = R"(
RWStructuredBuffer<uint> cuentas;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { cuentas[id.x] = 0u; }
)" ;

constexpr const char * celda_hlsl = R"(
uint celda(float2 x) {
  int2 c = int2(floor(x / H));
  return uint((c.y & (LADO - 1)) * LADO + (c.x & (LADO - 1)));
}
)" ;

// cuántas caen en cada celda, y el puesto en que llegó cada una a la suya
constexpr const char * contar_hlsl = R"(
StructuredBuffer<float2> prevista;
RWStructuredBuffer<uint> cuentas;
RWStructuredBuffer<uint> llegada;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= N) return;
  uint p;
  InterlockedAdd(cuentas[celda(prevista[id.x])], 1u, p);
  llegada[id.x] = p;
}
)" ;

// dónde empieza cada celda : la suma de las anteriores, en un solo grupo (enteros : sale
// siempre igual)
constexpr const char * prefijo_hlsl = R"(
StructuredBuffer<uint> cuentas;
RWStructuredBuffer<uint> inicio;
static const uint CELDAS = uint(LADO * LADO);
static const uint TROZO = (CELDAS + 1023u) / 1024u;
groupshared uint parcial[1024];
[numthreads(1024, 1, 1)]
void main(uint3 id : SV_GroupThreadID) {
  uint t = id.x;
  uint s = 0u;
  for (uint k = 0u; k < TROZO; ++k) { uint c = t * TROZO + k; if (c < CELDAS) s += cuentas[c]; }
  parcial[t] = s;
  GroupMemoryBarrierWithGroupSync();
  for (uint d = 1u; d < 1024u; d <<= 1u) {
    // el índice, siempre dentro : en HLSL las dos ramas de «?:» se evalúan
    uint v = parcial[t >= d ? t - d : 0u];
    GroupMemoryBarrierWithGroupSync();
    if (t >= d) parcial[t] += v;
    GroupMemoryBarrierWithGroupSync();
  }
  uint antes = parcial[t] - s;
  for (uint k = 0u; k < TROZO; ++k) {
    uint c = t * TROZO + k;
    if (c < CELDAS) { inicio[c] = antes; antes += cuentas[c]; }
  }
}
)" ;

constexpr const char * repartir_hlsl = R"(
StructuredBuffer<float2> prevista;
StructuredBuffer<uint> inicio;
StructuredBuffer<uint> llegada;
RWStructuredBuffer<uint> lista;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= N) return;
  lista[inicio[celda(prevista[id.x])] + llegada[id.x]] = id.x;
}
)" ;

// las de cada celda, por su número : el orden de llegada cambia en cada ejecución
constexpr const char * ordenar_hlsl = R"(
StructuredBuffer<uint> inicio;
StructuredBuffer<uint> cuentas;
RWStructuredBuffer<uint> lista;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= uint(LADO * LADO)) return;
  uint a = inicio[id.x];
  uint n = cuentas[id.x];
  for (uint s = 1u; s < n; ++s) {
    uint v = lista[a + s];
    uint t = s;
    // en HLSL «&&» evalúa las dos partes : con t = 0, el índice no puede salirse por debajo
    while (t > 0u && lista[a + max(t, 1u) - 1u] > v) { lista[a + t] = lista[a + t - 1u]; --t; }
    lista[a + t] = v;
  }
}
)" ;

// las posiciones previstas y las de partida, en el orden de la lista
constexpr const char * reunir_hlsl = R"(
StructuredBuffer<uint> lista;
StructuredBuffer<float2> prevista;
StructuredBuffer<float4> estado;
RWStructuredBuffer<float2> pos;
RWStructuredBuffer<float2> partida;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= N) return;
  uint i = lista[id.x];
  pos[id.x] = prevista[i];
  partida[id.x] = estado[i].xy;
}
)" ;

// La presión (Position Based Fluids) : la densidad de cada partícula, y lo que hay
// que moverla para que no pase de la de reposo. Solo empuja, nunca tira : tirar lo
// hace la cohesión, y así no se apelotona en la superficie. Cada hilo, un puesto de la
// lista : las de un grupo están juntas, y leen casi las mismas vecinas

constexpr const char * lambda_hlsl = R"(
StructuredBuffer<float2> pos;
StructuredBuffer<uint> inicio;
StructuredBuffer<uint> cuentas;
RWStructuredBuffer<float> lambda;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint k = id.x;
  if (k >= N) return;
  float2 xi = pos[k];
  float densidad = 0.0, suma = 0.0;
  float2 gi = float2(0.0, 0.0);
  VECINOS(xi, {
    float2 d = xi - pos[m];
    float r2 = dot(d, d);
    if (r2 < H2) {
      densidad += w_poly6(r2);
      if (r2 > 1e-10 && m != k) {
        float2 g = g_spiky(d, sqrt(r2)) / REPOSO;
        gi += g;
        suma += dot(g, g);
      }
    }
  })
  float c = max(densidad / REPOSO - 1.0, 0.0);
  lambda[k] = -c / (suma + dot(gi, gi) + 100.0);
}
)" ;

constexpr const char * desplazar_hlsl = R"(
StructuredBuffer<float2> pos;
StructuredBuffer<float> lambda;
StructuredBuffer<uint> inicio;
StructuredBuffer<uint> cuentas;
RWStructuredBuffer<float2> corregida;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint k = id.x;
  if (k >= N) return;
  float2 xi = pos[k];
  float li = lambda[k];
  float2 mueve = float2(0.0, 0.0);
  VECINOS(xi, {
    float2 d = xi - pos[m];
    float r2 = dot(d, d);
    if (r2 < H2 && r2 > 1e-10 && m != k) mueve += (li + lambda[m]) * g_spiky(d, sqrt(r2));
  })
  mueve /= REPOSO;
  float largo = length(mueve);
  if (largo > 0.3 * H) mueve *= 0.3 * H / largo;
  corregida[k] = chocar(xi + mueve);
}
)" ;

// La velocidad que ha salido, la viscosidad y la cohesión del paso siguiente : de vuelta a
// cada partícula por su número

constexpr const char * velocidad_hlsl = R"(
StructuredBuffer<uint> lista;
StructuredBuffer<float2> partida;
StructuredBuffer<float2> acabada;
StructuredBuffer<uint> inicio;
StructuredBuffer<uint> cuentas;
RWStructuredBuffer<float4> ahora;
RWStructuredBuffer<float2> empuje;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint k = id.x;
  if (k >= N) return;
  float dt = max(paso.x, 1e-5);
  float2 xi = acabada[k];
  float2 vi = (xi - partida[k]) / dt;
  float2 suave = float2(0.0, 0.0);
  float2 tira = float2(0.0, 0.0);
  VECINOS(xi, {
    float2 d = xi - acabada[m];
    float r2 = dot(d, d);
    if (r2 < H2 && r2 > 1e-10 && m != k) {
      float r = sqrt(r2);
      float2 vj = (acabada[m] - partida[m]) / dt;
      suave += (vj - vi) * w_poly6(r2);
      tira -= d / r * cohesion_en(r);
    }
  })
  vi += suave * superficie.y / REPOSO;
  uint i = lista[k];
  ahora[i] = float4(xi, vi);
  empuje[i] = tira * superficie.x;
}
)" ;

}  // namespace

bool ParticulasGota :: crear(const Gpu & gpu , const DescGota & desc , IBuffer * uniforme , const std :: string & hlsl , std :: string & error) {

	gpu_ = gpu ;

	desc_ = desc ;

	if (!crear_campos(error) || !crear_kernels(uniforme , hlsl , error)) { return false ; }

	atar() ;

	return true ;

}

bool ParticulasGota :: crear_campos(std :: string & error) {

	const Uint64 n = desc_.particulas , celdas = static_cast<Uint64>(desc_.lado_vecinos) * desc_.lado_vecinos ;

	for (int k = 0 ; k < 2 ; k++) {

		estado_[k] = gpu_campo(gpu_ , { n , 16 , "Nodos gota estado" }) ;

		calor_[k] = gpu_campo(gpu_ , { n , 4 , "Nodos gota calor" }) ;

		pos_[k] = gpu_campo(gpu_ , { n , 8 , "Nodos gota posiciones ordenadas" }) ;

		if (!estado_[k] || !calor_[k] || !pos_[k]) { return gpu_fallo(error , "No se pudieron crear los campos de la gota") ; }

	}

	prevista_ = gpu_campo(gpu_ , { n , 8 , "Nodos gota prevista" }) ;

	partida_ = gpu_campo(gpu_ , { n , 8 , "Nodos gota partida" }) ;

	empuje_ = gpu_campo(gpu_ , { n , 8 , "Nodos gota empuje" }) ;

	medida_ = gpu_campo(gpu_ , { 1 , 16 , "Nodos gota medida" }) ;

	lambda_ = gpu_campo(gpu_ , { n , 4 , "Nodos gota lambda" }) ;

	cuentas_ = gpu_campo(gpu_ , { celdas , 4 , "Nodos gota cuentas" }) ;

	inicio_ = gpu_campo(gpu_ , { celdas , 4 , "Nodos gota inicio" }) ;

	llegada_ = gpu_campo(gpu_ , { n , 4 , "Nodos gota llegada" }) ;

	lista_ = gpu_campo(gpu_ , { n , 4 , "Nodos gota lista" }) ;

	const bool bien = prevista_ && partida_ && empuje_ && medida_ && lambda_ && cuentas_ && inicio_ && llegada_ && lista_ ;

	return bien || gpu_fallo(error , "No se pudieron crear los campos de la gota") ;

}

bool ParticulasGota :: crear_kernels(IBuffer * uniforme , const std :: string & hlsl , std :: string & error) {

	const std :: vector<Recurso> fijos{ { "Gota" , uniforme } } ;

	const Uint32 todas = grupos_gota(desc_.particulas) ;

	const Uint32 celdas = grupos_gota(desc_.lado_vecinos * desc_.lado_vecinos) ;

	const auto kernel = [&](const char * nombre , const std :: string & codigo , Uint32 cuantos , Kernel & k) {

		k = gpu_kernel(gpu_ , { nombre , hlsl + codigo , fijos , cuantos , 1 } , error) ;

		return k.pipeline.RawPtr() != nullptr ;

	} ;

	return kernel("Nodos gota sembrar" , sembrar_hlsl , todas , sembrar_)

		&& kernel("Nodos gota barrer" , barrer_hlsl , todas , barrer_)

		&& kernel("Nodos gota medir" , medir_hlsl , 1 , medir_)

		&& kernel("Nodos gota predecir" , predecir_hlsl , todas , predecir_)

		&& kernel("Nodos gota limpiar" , limpiar_hlsl , celdas , limpiar_)

		&& kernel("Nodos gota contar" , std :: string(celda_hlsl) + contar_hlsl , todas , contar_)

		&& kernel("Nodos gota prefijo" , prefijo_hlsl , 1 , prefijo_)

		&& kernel("Nodos gota repartir" , std :: string(celda_hlsl) + repartir_hlsl , todas , repartir_)

		&& kernel("Nodos gota ordenar" , ordenar_hlsl , celdas , ordenar_)

		&& kernel("Nodos gota reunir" , reunir_hlsl , todas , reunir_)

		&& kernel("Nodos gota lambda" , lambda_hlsl , todas , lambda_k_)

		&& kernel("Nodos gota desplazar" , desplazar_hlsl , todas , desplazar_)

		&& kernel("Nodos gota velocidad" , velocidad_hlsl , todas , velocidad_) ;

}

void ParticulasGota :: atar() {

	sembrar_b_ = gpu_atar(sembrar_ , { { "estado" , gpu_escritura(estado_[0]) } , { "calor" , gpu_escritura(calor_[0]) } , { "empuje" , gpu_escritura(empuje_) } }) ;

	limpiar_b_ = gpu_atar(limpiar_ , { { "cuentas" , gpu_escritura(cuentas_) } }) ;

	contar_b_ = gpu_atar(contar_ , { { "prevista" , gpu_lectura(prevista_) } , { "cuentas" , gpu_escritura(cuentas_) } , { "llegada" , gpu_escritura(llegada_) } }) ;

	prefijo_b_ = gpu_atar(prefijo_ , { { "cuentas" , gpu_lectura(cuentas_) } , { "inicio" , gpu_escritura(inicio_) } }) ;

	repartir_b_ = gpu_atar(repartir_ , { { "prevista" , gpu_lectura(prevista_) } , { "inicio" , gpu_lectura(inicio_) } , { "llegada" , gpu_lectura(llegada_) } , { "lista" , gpu_escritura(lista_) } }) ;

	ordenar_b_ = gpu_atar(ordenar_ , { { "inicio" , gpu_lectura(inicio_) } , { "cuentas" , gpu_lectura(cuentas_) } , { "lista" , gpu_escritura(lista_) } }) ;

	for (int k = 0 ; k < 2 ; k++) {

		const int otro = 1 - k ;

		barrer_b_[k] = gpu_atar(barrer_ , { { "estado" , gpu_escritura(estado_[k]) } }) ;

		medir_b_[k] = gpu_atar(medir_ , { { "estado" , gpu_lectura(estado_[k]) } , { "medida" , gpu_escritura(medida_) } }) ;

		predecir_b_[k] = gpu_atar(predecir_ , { { "estado" , gpu_escritura(estado_[k]) } , { "empuje" , gpu_lectura(empuje_) } , { "medida" , gpu_lectura(medida_) } , { "prevista" , gpu_escritura(prevista_) } }) ;

		reunir_b_[k] = gpu_atar(reunir_ , { { "lista" , gpu_lectura(lista_) } , { "prevista" , gpu_lectura(prevista_) } , { "estado" , gpu_lectura(estado_[k]) } , { "pos" , gpu_escritura(pos_[0]) } , { "partida" , gpu_escritura(partida_) } }) ;

		// la presión acaba en pos[iteraciones % 2]

		velocidad_b_[k] = gpu_atar(velocidad_ , { { "lista" , gpu_lectura(lista_) } , { "partida" , gpu_lectura(partida_) } , { "acabada" , gpu_lectura(pos_[desc_.iteraciones % 2]) } , { "inicio" , gpu_lectura(inicio_) } , { "cuentas" , gpu_lectura(cuentas_) } , { "ahora" , gpu_escritura(estado_[otro]) } , { "empuje" , gpu_escritura(empuje_) } }) ;

		lambda_b_[k] = gpu_atar(lambda_k_ , { { "pos" , gpu_lectura(pos_[k]) } , { "inicio" , gpu_lectura(inicio_) } , { "cuentas" , gpu_lectura(cuentas_) } , { "lambda" , gpu_escritura(lambda_) } }) ;

		desplazar_b_[k] = gpu_atar(desplazar_ , { { "pos" , gpu_lectura(pos_[k]) } , { "lambda" , gpu_lectura(lambda_) } , { "inicio" , gpu_lectura(inicio_) } , { "cuentas" , gpu_lectura(cuentas_) } , { "corregida" , gpu_escritura(pos_[otro]) } }) ;

	}

}

ParticulasGota :: Lecturas ParticulasGota :: lecturas() const {

	return { { estado_[0] , estado_[1] } , { calor_[0] , calor_[1] } , medida_ , inicio_ , cuentas_ , lista_ } ;

}

std :: vector<std :: string> ParticulasGota :: tramos() {

	return { "gota barrer y medir" , "gota predecir" , "gota vecinas" , "gota presion" , "gota velocidad" } ;

}

void ParticulasGota :: avanzar(IDeviceContext * ctx , Uint32 subpasos , Medidor * banco , std :: size_t primera) {

	if (!sembrado_) {

		gpu_lanzar(ctx , sembrar_ , sembrar_b_ , { estado_[0] , calor_[0] , empuje_ }) ;

		sembrado_ = true ;

	}

	{

		Tramo t(banco , primera , Etapa :: subir , gpu_.device , ctx) ;

		gpu_lanzar(ctx , barrer_ , barrer_b_[actual_] , { estado_[actual_] }) ;

		gpu_lanzar(ctx , medir_ , medir_b_[actual_] , { medida_ }) ;

	}

	// los subpasos que caben en lo que duró el fotograma : cada uno, siempre igual de largo.
	// Puede que ninguno, si la pantalla va más rápida que la simulación

	for (Uint32 s = 0 ; s < subpasos ; s++) { subpaso(ctx , banco , primera) ; }

}

// Un subpaso : predecir, buscar vecinas, presión y velocidad

void ParticulasGota :: subpaso(IDeviceContext * ctx , Medidor * banco , std :: size_t primera) {

	{

		Tramo t(banco , primera + 1 , Etapa :: subir , gpu_.device , ctx) ;

		gpu_lanzar(ctx , predecir_ , predecir_b_[actual_] , { estado_[actual_] , prevista_ }) ;

	}

	{

		Tramo t(banco , primera + 2 , Etapa :: subir , gpu_.device , ctx) ;

		gpu_lanzar(ctx , limpiar_ , limpiar_b_ , { cuentas_ }) ;

		gpu_lanzar(ctx , contar_ , contar_b_ , { cuentas_ , llegada_ }) ;

		gpu_lanzar(ctx , prefijo_ , prefijo_b_ , { inicio_ }) ;

		gpu_lanzar(ctx , repartir_ , repartir_b_ , { lista_ }) ;

		gpu_lanzar(ctx , ordenar_ , ordenar_b_ , { lista_ }) ;

		gpu_lanzar(ctx , reunir_ , reunir_b_[actual_] , { pos_[0] , partida_ }) ;

	}

	{

		Tramo t(banco , primera + 3 , Etapa :: subir , gpu_.device , ctx) ;

		int p = 0 ;

		for (Uint32 i = 0 ; i < desc_.iteraciones ; i++) {

			gpu_lanzar(ctx , lambda_k_ , lambda_b_[p] , { lambda_ }) ;

			gpu_lanzar(ctx , desplazar_ , desplazar_b_[p] , { pos_[1 - p] }) ;

			p = 1 - p ;

		}

	}

	{

		Tramo t(banco , primera + 4 , Etapa :: subir , gpu_.device , ctx) ;

		gpu_lanzar(ctx , velocidad_ , velocidad_b_[actual_] , { estado_[1 - actual_] , empuje_ }) ;

	}

	actual_ = 1 - actual_ ;

}

}  // namespace shd :: nodos
