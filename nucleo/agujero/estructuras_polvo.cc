# include "estructuras_polvo.h"

namespace shd {

namespace {

constexpr const char * estructuras = R"(// Las hebras (ver PolvoForma) : la partícula i va en una si i % CADA_HEBRA
// es 0; en la hebra (i / CADA_HEBRA) % CURVAS
bool de_hebra(uint i) { return i % CADA_HEBRA == 0u; }

// El brillo de las partículas de hebras y cúmulos : todas el mismo, la
// media en luz lineal del de las sueltas (0,55 a 1 al azar) :
// (∫ (0,55 + 0,45 u)^2,2 du)^(1/2,2)
static const float LUZ_IGUAL = 0.788;

uint hebra_de(uint i) { return (i / CADA_HEBRA) % CURVAS; }

// Los cúmulos (las estrellas lejanas) : la partícula i va en uno si no va en
// una hebra e i % CADA_CUMULO es 2; en el cúmulo (i / CADA_CUMULO) % CUMULOS
bool de_cumulo(uint i) { return !de_hebra(i) && i % CADA_CUMULO == 2u; }

uint cumulo_de(uint i) { return (i / CADA_CUMULO) % CUMULOS; }

// Las estructuras : las hebras (0 a CURVAS − 1) y los cúmulos (después). La
// vida de la estructura s en el instante t : su ciclo (cada vez que renace),
// su edad y lo que dura
void vida_estructura(uint s, float t, out float ciclo, out float edad, out float dura) {
  float4 v = s < CURVAS ? hebra : cumulo;
  dura = v.x + v.y * azar(s * 29u + 5u);
  float desfase = azar(s * 29u + 6u) * dura;
  ciclo = floor((t + desfase) / dura);
  edad = (t + desfase) - ciclo * dura;
}

float3 rotar(float3 v, float3 k, float a) { return v * cos(a) + cross(k, v) * sin(a) + k * dot(k, v) * (1.0 - cos(a)); }

// El punto medio de la hebra h al nacer (en su ciclo)
float3 medio_hebra(uint h, uint ciclo) {
  uint s = h * 7919u + ciclo * 104729u + 11u;
  return en_casquete(1.0 - (1.0 - centro.w) * 0.5 * azar(s), TAU * azar(s + 1u));
}

// El largo de la hebra h (en su ciclo), y lo que la partícula i se aparta
// de su punto medio a lo largo de ella (rad) : repartidas por igual en el
// largo máximo, todas las hebras tienen la misma densidad de partículas (y
// brillan igual); en las más cortas, las que caen fuera no se pintan
float largo_hebra(uint h, uint ciclo) { return lerp(LARGO_MIN, LARGO_MAX, azar(h * 7919u + ciclo * 104729u + 15u)); }

float a_lo_largo(uint i, uint ciclo) { return (azar(i * 13u + 31u + ciclo * 17u) - 0.5) * LARGO_MAX; }

bool en_su_hebra(uint i, uint h, uint ciclo) { return abs(a_lo_largo(i, ciclo)) <= 0.5 * largo_hebra(h, ciclo); }

// El sitio de la partícula i al nacer su hebra h (en su ciclo) : un arco de
// círculo menor (curvo, como una hebra ya hecha) dentro del casquete
float3 en_hebra(uint i, uint h, uint ciclo) {
  uint s = h * 7919u + ciclo * 104729u + 11u;
  float3 medio = medio_hebra(h, ciclo);
  float3 t1 = normalize(cross(medio, abs(medio.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
  float3 t2 = cross(medio, t1);
  float a = TAU * azar(s + 2u);
  // el eje del círculo menor, a rho del punto medio
  float rho = lerp(CURVA_MIN, CURVA_MAX, azar(s + 3u));
  float3 eje = medio * cos(rho) + (t1 * cos(a) + t2 * sin(a)) * sin(rho);
  return normalize(rotar(medio, eje, a_lo_largo(i, ciclo) / sin(rho)));
}

// El centro del cúmulo k al nacer (en su ciclo)
float3 centro_cumulo(uint k, uint ciclo) {
  uint s = k * 6007u + ciclo * 92821u + 23u;
  return en_casquete(1.0 - (1.0 - centro.w) * 0.9 * azar(s), TAU * azar(s + 1u));
}

// El sitio de la partícula i al nacer su cúmulo k : alrededor del centro,
// en una gaussiana redonda de RADIO_CUMULO (todos iguales : brillan igual)
float3 en_cumulo(uint i, uint k, uint ciclo) {
  float3 c = centro_cumulo(k, ciclo);
  float u1 = max(azar(i * 17u + ciclo * 13u + 1u), 1e-6), u2 = azar(i * 17u + ciclo * 13u + 2u);
  float r = RADIO_CUMULO * sqrt(-2.0 * log(u1)), a = TAU * u2;
  float3 t1 = normalize(cross(c, abs(c.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
  float3 t2 = cross(c, t1);
  return normalize(c + (t1 * cos(a) + t2 * sin(a)) * r);
}

// Dónde nace la estructura s (su centro)
float3 nace_estructura(uint s, uint ciclo) { return s < CURVAS ? medio_hebra(s, ciclo) : centro_cumulo(s - CURVAS, ciclo); }

)" ;

constexpr const char * centros = R"(
RWStructuredBuffer<float4> centros;
RWStructuredBuffer<uint> alcance;

// lo que se deja de margen al pasar una estructura al otro lado (rad, medio grado)
static const float MARGEN_ESTRUCTURA = 0.0087;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint s = id.x;
  if (s >= ESTRUCTURAS) return;
  // lo que mide (el mover lo midió en el fotograma anterior : hasta dónde
  // llegan sus partículas visibles desde el centro), y a cero, para medirlo
  // otra vez
  float mide = asfloat(alcance[s]);
  alcance[s] = 0u;
  float dt = paso.x, t = paso.y;
  float ciclo, edad, dura, ciclo_antes, edad_antes, dura_antes;
  vida_estructura(s, t, ciclo, edad, dura);
  vida_estructura(s, t - dt, ciclo_antes, edad_antes, dura_antes);
  float3 x;
  // si pasa al otro lado del casquete (la cámara se ha movido), de dónde
  // saltó : sus partículas dan el mismo salto, la estructura entera
  float4 salto = float4(0.0, 0.0, 0.0, 0.0);
  if (paso.z > 0.5 || ciclo != ciclo_antes) {
    x = nace_estructura(s, uint(ciclo));
  } else {
    float3 movido = normalize(centros[3u * s].xyz + flujo_en(centros[3u * s].xyz, t) * dt);
    // pasa al otro lado solo cuando ya no se ve nada de ella : con el
    // centro más allá del borde lo que mide, y el margen. Si pasaba al
    // cruzar el borde el centro, la mitad de una hebra que aún se veía
    // desaparecía de golpe y aparecía en el lado contrario
    x = al_casquete(movido, s * 13u + uint(ciclo) * 7u + 900u, radio_casquete() + mide + MARGEN_ESTRUCTURA);
    if (dot(x, movido) < 0.9999999) salto = float4(movido, 1.0);
  }
  centros[3u * s] = float4(x, 0.0);
  centros[3u * s + 1u] = float4(flujo_en(x, t), 0.0);
  centros[3u * s + 2u] = salto;
}
)" ;

}  // namespace

const char * estructuras_hlsl() { return estructuras ; }

const char * centros_hlsl() { return centros ; }

}  // namespace shd
