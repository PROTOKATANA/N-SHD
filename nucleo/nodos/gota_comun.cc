# include "gota_comun.h"

# include "numero_hlsl.h"

namespace shd :: nodos {

namespace {

// Lo común : el cbuffer, los núcleos del SPH y recorrer los vecinos. La rejilla de
// vecinos es un hash : da la vuelta, así que el líquido puede ir a cualquier parte
// sin salirse de ella, y lo que coincida por la vuelta lo descarta la distancia

constexpr const char * comun_hlsl = R"(
cbuffer Gota {
  float4 paso;       // dt de cada subpaso (s), tiempo (s), segundos desde la pulsación, semilla
  float4 inercia;    // lo que tira el arrastre del borde (x, y), fuerza de la pulsación, cuántos obstáculos
  float4 fuerzas;    // ancla, freno del ancla, giro, arrastre del giro
  float4 superficie; // cohesión, viscosidad, velocidad máxima, chorros que nunca paran
  float4 bordes;     // los de la vista: x0, y0, x1, y1
  float4 centro;     // el nodo en el mundo: x, y, radio que se ve; lo que se atrae el líquido
  float4 obstaculos[8];  // x, y, radio, radio de su gota en el líquido de los enlaces
  float4 contactos[4];   // de dónde vienen (x, y), fuerza, 0
  float4 ritmo;          // subpasos de este fotograma, lo que duró (s), 0, 0
  float4 barrido[4];     // lo que barrió cada obstáculo: dos por elemento
  float4 tubo;           // la forma del líquido de los enlaces : gota, fusión, cuello; lo que cae en una tubería
  float4 cuenta_liquido; // gotas y tubos del líquido de los enlaces que hay cerca, 0, 0
  float4 gotas[12];      // sus gotas, en su orden : x, y, radio que se ve, fusión
  float4 tubos[16];      // sus tubos : un extremo (x, y) y el otro (x, y)
  float4 anchos[4];      // la media anchura de cada tubo, cuatro por elemento
  float4 funde[4];       // lo que se funde la gota con cada tubo, de 0 a 1, cuatro por elemento
};

static const float TAU = 6.2831853;
static const float PI = 3.1415927;
static const float H2 = H * H;
// las celdas del campo que toca una partícula : con menos, una sola ya pasa del contorno
// y se ve como un punto suelto
static const int ALCANCE = 4;
// la densidad de reposo : las partículas repartidas en el círculo de radio 1
static const float REPOSO = float(N) / PI;

float azar(uint x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return float(x >> 8) * (1.0 / 16777216.0);
}

// Poly6 y el gradiente de Spiky, en 2D
static const float POLY6 = 4.0 / (PI * H2 * H2 * H2 * H2);
static const float SPIKY = -30.0 / (PI * H2 * H2 * H);
float w_poly6(float r2) { float d = H2 - r2; return POLY6 * d * d * d; }
float2 g_spiky(float2 d, float r) { float k = H - r; return d / r * SPIKY * k * k; }

// Atrae a media distancia y repele un poco de cerca (Akinci) : como es más fuerte
// donde faltan vecinas, hace de tensión superficial
float cohesion_en(float r) {
  float q = r / H;
  float c = (1.0 - q) * (1.0 - q) * (1.0 - q) * q * q * q;
  return q > 0.5 ? c : 2.0 * c - 1.0 / 64.0;
}

// El calor al que tiende el líquido, en d del centro de masa : brazos fríos y calientes
// y más calor hacia el ojo. Es el del remolino de fluido que fue antes el vórtice, ya
// asentado, medido, no el de su arranque. Pasa de 1 a propósito : así el rojo del centro queda plano, y lo que se
// mezcla a su alrededor no lo apaga
float calor_remolino(float2 d, float tiempo) {
  float rho = length(d);
  float espiral = atan2(d.y, d.x) - 2.5 * log(1.0 / max(rho, 0.05)) - tiempo * 0.7;
  float brazo = 0.5 + 0.5 * cos(3.0 * espiral);
  float falta = saturate(1.0 - rho);
  return min(lerp(0.05, 0.8, brazo * brazo) + 1.65 * falta * falta, 1.3);
}

// Dos vueltas : sacar una partícula de un círculo puede meterla en el de al lado, y por
// la juntura de dos nodos que se tocan se colaba
float2 chocar(float2 x) {
  for (uint vuelta = 0u; vuelta < 2u; ++vuelta) {
    x = clamp(x, bordes.xy, bordes.zw);
    for (uint i = 0u; i < uint(inercia.w); ++i) {
      float2 d = x - obstaculos[i].xy;
      float r = length(d);
      if (r < obstaculos[i].z && r > 1e-5) x = obstaculos[i].xy + d / r * obstaculos[i].z;
    }
  }
  return x;
}

float2 barrido_de(uint i) { float4 b = barrido[i >> 1]; return (i & 1u) != 0u ? b.zw : b.xy; }

// el fluido de los canales
static const float FRENO_C = 1.5;   // 1/s : lo que se frena el fluido de los canales
static const float LLEGA = 0.255;    // radios : hasta dónde llega el líquido por los canales, con el menisco (0,85, un 70 % más corto : la gota, una esfera con puntas cortas)
static const float FRENTE = 0.09;    // radios : lo que se redondea el frente del líquido, como un menisco (0,3, en proporción al alcance)
static const float GRUMO = 5.0;      // celdas del campo : lo que mide lo más pequeño que se dibuja suelto
static const float BORDE_GOTA = 0.3;  // lo que cubre la gota en su borde visible : de ahí sale el color de los canales

float smin_tubo(float a, float b, float k) {
  float m = max(k - abs(a - b), 0.0) / k;
  return min(a, b) - m * m * k * 0.25;
}

// La silueta del líquido de los enlaces cerca del vórtice, tal como la pinta
// PasadaLiquido : sus gotas y luego sus tubos, en el mismo orden y con la misma unión
// suave. Negativa dentro. Son las paredes de las tuberías : por eso un puente que
// engorda, porque sus nodos se juntan, es una tubería más ancha
float tuberias(float2 x) {
  float d = 1e9;
  for (uint k = 0u; k < uint(cuenta_liquido.x); ++k) {
    d = smin_tubo(length(x - gotas[k].xy) - gotas[k].z, d, gotas[k].w);
  }
  for (uint t = 0u; t < uint(cuenta_liquido.y); ++t) {
    float2 a = tubos[t].xy;
    float2 ab = tubos[t].zw - a;
    float u = saturate(dot(x - a, ab) / max(dot(ab, ab), 1e-6));
    float medio = anchos[t >> 2][t & 3u];
    d = smin_tubo(length(x - a - ab * u) - medio, d, tubo.y * medio / tubo.z);
  }
  return d;
}

// La misma silueta, unida a la gota como el líquido de los enlaces une un nodo con los
// suyos : cada enlace y cada gota se funden con ella con su propia suavidad, que crece con
// el radio del nodo. `cer`, lo lejos que queda la gota. Negativa dentro
float embudo(float2 x, float cer) {
  float d = tuberias(x);
  for (uint k = 0u; k < uint(cuenta_liquido.x); ++k) {
    d = min(d, smin_tubo(length(x - gotas[k].xy) - gotas[k].z, cer, gotas[k].w));
  }
  for (uint t = 0u; t < uint(cuenta_liquido.y); ++t) {
    float2 a = tubos[t].xy;
    float2 ab = tubos[t].zw - a;
    float u = saturate(dot(x - a, ab) / max(dot(ab, ab), 1e-6));
    float medio = anchos[t >> 2][t & 3u];
    // un tubo ajeno se une a la gota poco a poco, según lo cerca que pase : ya está en la
    // silueta, tal cual, y de golpe tendería un puente hasta él
    float f = funde[t >> 2][t & 3u];
    if (f > 0.0) d = min(d, smin_tubo(length(x - a - ab * u) - medio, cer, max(tubo.y * medio / tubo.z * f, 1e-4)));
  }
  return d;
}

// Las vecinas de PUNTO : `m`, su puesto en el orden por celdas (lista[m] es su número)
#define VECINOS(PUNTO, CUERPO) { \
  int2 c0 = int2(floor((PUNTO) / H)); \
  for (int cy = -1; cy <= 1; ++cy) { for (int cx = -1; cx <= 1; ++cx) { \
    uint c = uint(((c0.y + cy) & (LADO - 1)) * LADO + ((c0.x + cx) & (LADO - 1))); \
    uint fin_c = inicio[c] + cuentas[c]; \
    for (uint m = inicio[c]; m < fin_c; ++m) { CUERPO } \
  } } \
}
)" ;

// El fluido de los canales : Navier-Stokes en una rejilla de M × M que cubre la vista de la
// gota, dentro de la forma elástica de los enlaces. Una rejilla no se rompe en gotas por
// estrecho que sea el canal. La gota le da, donde está, su velocidad, su calor y su
// líquido; desde ahí el remolino los mete en los canales y el líquido se esparce por donde
// las paredes lo dejan. Los nodos son obstáculos dentro : los rodea

constexpr const char * canal_hlsl = R"(
static const float CELDA_C = 2.0 * VISTA / float(M);   // radios
float2 centro_celda(int2 c) { return (float2(c) + 0.5) * CELDA_C - VISTA; }
// el índice, siempre dentro de la rejilla : en HLSL «a && b» evalúa las dos partes, y leer
// fuera de la memoria con una celda del borde dejaba la GPU sin pintar
int ic(int2 c) { int2 q = clamp(c, 0, int(M) - 1); return q.y * int(M) + q.x; }
bool dentro_c(int2 c) { return all(c >= 0) && all(c < int(M)); }
#define MUESTRA_C(nombre, buffer, T) \
T nombre(float2 g) { \
  g = clamp(g, 0.0, float(M) - 1.001); \
  int2 a = int2(floor(g)); \
  float2 f = g - float2(a); \
  int i = ic(a); \
  return lerp(lerp(buffer[i], buffer[i + 1], f.x), lerp(buffer[i + int(M)], buffer[i + int(M) + 1], f.x), f.y); \
}
// lo mismo, pero solo con las celdas de líquido : mezclar con la pared, que vale cero,
// vaciaría un canal estrecho en cada fotograma. Sin ninguna alrededor, `vacio` : un cero
// ahí se leía como líquido recién salido de la gota, y pintaba manchas en los bordes
#define MUESTRA_CM(nombre, buffer, T, mascara, vacio) \
T nombre(float2 g) { \
  g = clamp(g, 0.0, float(M) - 1.001); \
  int2 a = int2(floor(g)); \
  float2 f = g - float2(a); \
  int i = ic(a); \
  float4 w = float4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y); \
  w *= float4(mascara[i] != 0u, mascara[i + 1] != 0u, mascara[i + int(M)] != 0u, mascara[i + int(M) + 1] != 0u); \
  float s = w.x + w.y + w.z + w.w; \
  if (s < 1e-5) return vacio; \
  return (buffer[i] * w.x + buffer[i + 1] * w.y + buffer[i + int(M)] * w.z + buffer[i + int(M) + 1] * w.w) / s; \
}
// lo que deja una partícula llena, como en el pintado
float lleno_campo() {
  float celdas = float(G) / (2.0 * VISTA);
  return float(N) / PI / (celdas * celdas) * PI * float(ALCANCE * ALCANCE) / 3.0 * 256.0;
}
)" ;

}  // namespace

std :: string hlsl_gota(const DescGota & desc) {

	return "#define N " + std :: to_string(desc.particulas) + "u\n#define LADO " + std :: to_string(desc.lado_vecinos) + "\n#define G " + std :: to_string(desc.lado_campo) + "u\n#define H " + numero_hlsl(desc.alcance) + "\n#define VISTA " + numero_hlsl(desc.vista) + "\n#define M " + std :: to_string(lado_canal) + "u\n" + comun_hlsl + canal_hlsl ;

}

}  // namespace shd :: nodos
