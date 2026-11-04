# pragma once

// Dónde hay líquido de la gota : su contorno en el campo y el líquido de los canales hasta
// donde llega. Lo leen la pintura (PinturaGota) y lo que separa los trozos sueltos
// (SueltosGota) : con la misma cuenta, un trozo que la pintura dibuja es el mismo que se
// encuentra suelto. Va detrás de hlsl_gota (gota_comun.h)

namespace shd :: nodos {

inline constexpr const char * liquido_gota_hlsl = R"(
StructuredBuffer<uint> campo;
StructuredBuffer<uint> canal_masc;
StructuredBuffer<float2> canal_alc;
StructuredBuffer<float> canal_cer;
MUESTRA_C(cer_en, canal_cer, float)
StructuredBuffer<float> canal_cal;

float2 en(int2 c) {
  if (any(c < 0) || any(c >= int(G))) return float2(0.0, 0.0);
  uint k = (uint(c.y) * G + uint(c.x)) * 2u;
  return float2(campo[k], campo[k + 1u]);
}

// el campo en un punto (en celdas), entre las cuatro celdas que lo rodean
float2 campo_en(float2 g) {
  float2 q = g - 0.5;
  int2 a = int2(floor(q));
  float2 f = q - float2(a);
  return lerp(lerp(en(a), en(a + int2(1, 0)), f.x), lerp(en(a + int2(0, 1)), en(a + int2(1, 1)), f.x), f.y);
}

float2 celda_de(float2 r) { return (r + VISTA) / (2.0 * VISTA) * float(G); }

// la celda de los canales de un punto, con las celdas en sus centros
float2 celda_canal(float2 p) { return (p + VISTA) / CELDA_C - 0.5; }

// El alcance y el calor de los canales con las celdas de líquido de una vecindad de 4 × 4,
// pesadas por la distancia : con solo las cuatro de alrededor, el borde del canal seguía la
// rejilla, en escalera, en vez de la forma del enlace
float3 canal_en(float2 g) {
  g = clamp(g, 1.0, float(M) - 2.001);
  int2 a = int2(floor(g));
  float3 s = float3(0.0, 0.0, 0.0);
  float sw = 0.0;
  [unroll] for (int y = -1; y <= 2; ++y) {
    [unroll] for (int x = -1; x <= 2; ++x) {
      int i = ic(a + int2(x, y));
      if (canal_masc[i] == 0u) continue;
      float2 e = g - float2(a + int2(x, y));
      float w = exp(-dot(e, e) * 1.5);
      s += float3(canal_alc[i], canal_cal[i]) * w;
      sw += w;
    }
  }
  return sw > 1e-5 ? s / sw : float3(1e3, 0.0, 0.0);
}

// Lo que hay de líquido alrededor, a unas celdas : un grupito de partículas suelto, o un
// bulto del borde, es más pequeño que eso, y así no se dibuja como una cuenta aparte
float masa_grande(float2 g) {
  float s = 0.0;
  [unroll] for (int k = 0; k < 8; ++k) {
    float a = float(k) * 0.7853982;
    s += campo_en(g + float2(cos(a), sin(a)) * GRUMO).x;
  }
  return s / 8.0;
}

// La gota en p (radios desde su centro), de 0 a 1 : `v`, el campo ahí; `aa`, el ancho del
// borde, en masa del campo
float forma_gota(float2 p, float2 v, float aa) {
  float lleno = lleno_campo();
  float umbral = 0.4 * lleno;
  float gota = smoothstep(umbral - aa, umbral + aa, v.x);
  // donde no llega la gota, lo de alrededor no cambia nada (0 por lo que sea es 0)
  if (gota > 0.0) gota *= smoothstep(0.12 * lleno, 0.2 * lleno, masa_grande(celda_de(p)));
  return gota;
}

// El líquido de los canales en p, de 0 a 1, con sus paredes nítidas : la forma elástica de
// los enlaces, y sin cubrir los nodos. `gc`, su celda; `alc`, su alcance; `pixel`, el ancho
// del borde, en radios
float forma_canal(float2 p, float2 gc, float alc, float pixel) {
  // sin líquido alrededor (alcance de 1e3), el frente queda a más de 998 radios : el canal es
  // 0 exacto, y ni la silueta de los enlaces ni los nodos hacen falta. Es casi toda la vista
  if (alc >= 999.0) return 0.0;
  // dentro de la forma y hasta donde llega, con el frente redondeado como un menisco
  float pared = embudo(p, cer_en(gc));
  float forma = -smin_tubo(LLEGA - alc, -pared, FRENTE);
  float canal = 1.0 - smoothstep(-pixel, pixel, forma);
  for (uint o = 0u; o < uint(inercia.w); ++o) canal *= smoothstep(0.0, 1.5 * pixel, length(p - obstaculos[o].xy) - obstaculos[o].z);
  return canal;
}
)" ;

}  // namespace shd :: nodos
