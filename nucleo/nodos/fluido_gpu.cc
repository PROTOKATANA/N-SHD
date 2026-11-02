# include "fluido_gpu.h"

namespace shd :: nodos {

namespace {

constexpr const char * comun_hlsl = R"(
cbuffer Fluido {
  float4 fluido;   // dt (s), confinamiento, q del círculo, 0
};

static const float TAU = 6.2831853;
// el círculo en celdas: su radio y su centro
static const float RADIO = (N - 2) * 0.5;
static const float2 MEDIO = float2(N - 1, N - 1) * 0.5;
static const int2 DX = int2(1, 0);
static const int2 DY = int2(0, 1);

// Hash entero -> [0, 1) (el mismo que vortice.rs)
float azar(uint x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return float(x >> 8) * (1.0 / 16777216.0);
}

// q: la distancia al centro al cuadrado, en coordenadas dobladas (un entero,
// como en fluido.rs: la CPU y la GPU clasifican igual las celdas)
float q_de(int2 c) { int2 d = 2 * c - (N - 1); return float(d.x * d.x + d.y * d.y); }

bool es_fluido(int2 c) { return all(c >= 0) && all(c < N) && q_de(c) <= fluido.z; }

int indice(int2 c) { return c.y * N + c.x; }

// Interpolación bilineal de un buffer de la rejilla (cada shader la crea
// para sus buffers)
#define MUESTREADOR(nombre, buffer, T) \
T nombre(float2 g) { \
  g = clamp(g, 0.0, N - 1.001); \
  int2 a = int2(floor(g)); \
  float2 f = g - float2(a); \
  int i = indice(a); \
  return lerp(lerp(buffer[i], buffer[i + 1], f.x), lerp(buffer[i + N], buffer[i + N + 1], f.x), f.y); \
}
)" ;

}  // namespace

std :: string fluido_hlsl(std :: uint32_t celdas) {

	return "#define N " + std :: to_string(celdas) + "\n" + comun_hlsl ;

}

}  // namespace shd :: nodos
