# include "cbuffer_agujero.h"

# include <cmath>

# include "forma_agujero.h"

namespace shd {

namespace {

constexpr double pi = 3.14159265358979 ;

// Ver kerr.rs

constexpr float margen_horizonte = 0.01f ;

// El ruido vuelve a 0 cada día : en float no pierde precisión

constexpr double periodo_ruido = 86400.0 ;

}  // namespace

const char * const agujero_hlsl = R"(
cbuffer Agujero {
  float4 tiempo;    // desde el arranque, para el ruido, paso (s), 0
  uint4 tam;        // ancho, alto (px), 0, 0
  float4 camara;    // posición (xyz), el giro a
  float4 derecha;   // la base de la cámara (xyz), tan(campo vertical / 2)
  float4 arriba;    // (xyz), ancho / alto
  float4 adelante;  // (xyz), 0
  float4 kerr;      // horizonte, borde interior del disco, exterior, lejos
  float4 trazado;   // precisión, pasos máximos, margen del horizonte, rozar
  float4 disco;     // ln r interior, ln r exterior, opacidad, brillo
  float4 luz;       // 0, 0, pico del brillo del disco, exposición
  float4 orbita;    // azimut de la cámara (rad), reducción del mapa bajo, filas listas del mapa alto, de qué mapa son los bordes
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

// [0, 1)
float azar(uint x) { return float(mezclar(x) >> 8) * (1.0 / 16777216.0); }

// Los rayos de Kerr, como en kerr.rs (Kerr-Schild cartesianas, M = 1, p_t =
// +1 : hacia atrás)
struct Metrica {
  float r;
  float f;
  float3 l;
};

Metrica metrica(float3 x) {
  float a = camara.w;
  float a2 = a * a;
  float b = dot(x, x) - a2;
  float r2 = 0.5 * (b + sqrt(b * b + 4.0 * a2 * x.z * x.z));
  float r = sqrt(max(r2, 1e-12));
  float d = r2 + a2;
  Metrica m;
  m.r = r;
  m.f = 2.0 * r * r2 / (r2 * r2 + a2 * x.z * x.z);
  m.l = float3((r * x.x + a * x.y) / d, (r * x.y - a * x.x) / d, x.z / r);
  return m;
}

// dx/dλ = p − f (l·p) l ; dp/dλ = ½ ∇F, con el gradiente exacto (como
// gradiente_efe en kerr.rs : de r⁴ − (R² − a²) r² − a² z² = 0, ∂r/∂x = r³ x / Q,
// ∂r/∂z = z r (r² + a²) / Q, Q = r⁴ + a² z²)
void derivada(float3 x, float3 p, out float3 dx, out float3 dp) {
  Metrica m = metrica(x);
  float a = camara.w;
  float a2 = a * a;
  float r = m.r;
  float r2 = r * r;
  float q = r2 * r2 + a2 * x.z * x.z;
  float d = r2 + a2;
  float3 dr = float3(r2 * r * x.x, r2 * r * x.y, x.z * r * d) / q;
  float s = x.x * p.x + x.y * p.y;
  float t = x.y * p.x - x.x * p.y;
  float n = r * s + a * t;
  float lp = -1.0 + n / d + x.z * p.z / r;
  dx = p - m.f * lp * m.l;
  float3 dz = float3(0.0, 0.0, 1.0);
  float3 df = 2.0 * r2 * ((3.0 * q - 4.0 * r2 * r2) * dr - 2.0 * a2 * r * x.z * dz) / (q * q);
  float3 dn = dr * s + r * float3(p.x, p.y, 0.0) + a * float3(-p.y, p.x, 0.0);
  float3 dlp = (dn * d - n * 2.0 * r * dr) / (d * d) + p.z * (dz * r - x.z * dr) / r2;
  dp = 0.5 * (df * lp * lp + 2.0 * m.f * lp * dlp);
}

void paso_rk4(inout float3 x, inout float3 p, float h) {
  float3 a1, b1, a2, b2, a3, b3, a4, b4;
  derivada(x, p, a1, b1);
  derivada(x + a1 * (h * 0.5), p + b1 * (h * 0.5), a2, b2);
  derivada(x + a2 * (h * 0.5), p + b2 * (h * 0.5), a3, b3);
  derivada(x + a3 * h, p + b3 * h, a4, b4);
  x += (a1 + 2.0 * a2 + 2.0 * a3 + a4) * (h / 6.0);
  p += (b1 + 2.0 * b2 + 2.0 * b3 + b4) * (h / 6.0);
}

// El rayo que sale de la cámara en la dirección n (unitaria): H = 0
float3 momento_desde(float3 x, float3 n) {
  Metrica m = metrica(x);
  float ln = dot(m.l, n);
  float A = 1.0 - m.f * ln * ln;
  float B = 2.0 * m.f * ln;
  float C = -(1.0 + m.f);
  return n * ((-B + sqrt(B * B - 4.0 * A * C)) / (2.0 * A));
}
)" ;

void rellenar_agujero(const MomentoAgujero & m , AgujeroGpu & g) {

	const FormaAgujero & f = forma_agujero ;

	g = AgujeroGpu{} ;

	g.tiempo[0] = static_cast<float>(m.t) ;

	g.tiempo[1] = static_cast<float>(std :: fmod(m.t , periodo_ruido)) ;

	g.tiempo[2] = static_cast<float>(m.paso) ;

	g.tam[0] = m.ancho ;

	g.tam[1] = m.alto ;

	// la cámara : en su esfera, mirando al centro, con la base de trazado de
	// la órbita (la de la cámara llevada a azimut 0 : el azimut lo pone
	// componer, girando el disco y el cielo)

	const agujero :: BaseCamara & b = m.camara ;

	for (int k = 0 ; k < 3 ; k++) {

		g.camara[k] = static_cast<float>(f.camara.distancia * b.atras[k]) ;

		g.derecha[k] = static_cast<float>(b.derecha[k]) ;

		g.arriba[k] = static_cast<float>(b.arriba[k]) ;

		g.adelante[k] = static_cast<float>(-b.atras[k]) ;

	}

	g.camara[3] = static_cast<float>(f.kerr.giro) ;

	g.derecha[3] = static_cast<float>(std :: tan(f.camara.campo * pi / 360)) ;

	g.arriba[3] = m.alto ? static_cast<float>(m.ancho) / static_cast<float>(m.alto) : 1.0f ;

	// el agujero y el disco

	const double r_in = agujero :: isco(f.kerr.giro) ;

	g.kerr[0] = static_cast<float>(agujero :: horizonte(f.kerr.giro)) ;

	g.kerr[1] = static_cast<float>(r_in) ;

	g.kerr[2] = static_cast<float>(f.kerr.r_out) ;

	g.kerr[3] = static_cast<float>(f.kerr.lejos) ;

	g.trazado[0] = static_cast<float>(f.kerr.precision) ;

	g.trazado[1] = static_cast<float>(f.kerr.max_pasos) ;

	g.trazado[2] = margen_horizonte ;

	g.trazado[3] = static_cast<float>(f.kerr.rozar * r_in) ;

	g.disco[0] = static_cast<float>(std :: log(r_in)) ;

	g.disco[1] = static_cast<float>(std :: log(f.kerr.r_out)) ;

	g.disco[2] = f.disco.opacidad ;

	g.disco[3] = f.disco.brillo ;

	// el brillo de un disco fino (Novikov-Thorne) : r⁻³ (1 − √(r_in / r)),
	// con su pico en r = 49/36 r_in

	const double pico = 49.0 / 36.0 * r_in ;

	g.luz[2] = static_cast<float>(std :: pow(pico , -3.0) * (1 - std :: sqrt(r_in / pico))) ;

	g.luz[3] = f.luz.exposicion ;

	g.orbita[0] = static_cast<float>(m.azimut) ;

	g.orbita[1] = static_cast<float>(m.reduccion) ;

	g.orbita[2] = static_cast<float>(m.filas_listas) ;

}

}  // namespace shd
