# include "pintura_disco.h"

# include <algorithm>

# include "numero_hlsl.h"

namespace shd {

namespace {

std :: string float3_de(const float (& v)[3]) { return "float3(" + numero_hlsl(v[0]) + ", " + numero_hlsl(v[1]) + ", " + numero_hlsl(v[2]) + ")" ; }

// Dónde acaba cada color (menos el último), en niveles : el último de los
// suyos más medio

std :: array<float , 3> fronteras(const PinturaForma & p) {

	std :: array<float , 3> b{} ;

	std :: uint32_t n = 0 ;

	for (std :: size_t i = 0 ; i < 3 ; i++) {

		n += p.niveles[i] ;

		b[i] = static_cast<float>(n) - 0.5f ;

	}

	return b ;

}

float mezclar(float a , float b , float w) { return a + (b - a) * w ; }

}  // namespace

std :: uint32_t bandas(const PinturaForma & p) { return p.niveles[0] + p.niveles[1] + p.niveles[2] + p.niveles[3] - 1 ; }

std :: array<float , 3> color_de_nivel(const PinturaForma & p , std :: uint32_t k) {

	std :: uint32_t hasta = 0 ;

	for (std :: size_t i = 0 ; i < 4 ; i++) {

		hasta += p.niveles[i] ;

		if (k < hasta) { return { p.colores[i][0] , p.colores[i][1] , p.colores[i][2] } ; }

	}

	return { p.colores[3][0] , p.colores[3][1] , p.colores[3][2] } ;

}

float repartir(const PinturaForma & p , float t) {

	const float n = static_cast<float>(bandas(p)) ;

	const std :: array<float , 3> b = fronteras(p) ;

	const float ct[3] = { p.cortes[0] / n , p.cortes[1] / n , p.cortes[2] / n } , cb[3] = { b[0] / n , b[1] / n , b[2] / n } ;

	if (t < ct[0]) { return t * cb[0] / ct[0] ; }

	if (t < ct[1]) { return mezclar(cb[0] , cb[1] , (t - ct[0]) / (ct[1] - ct[0])) ; }

	if (t < ct[2]) { return mezclar(cb[1] , cb[2] , (t - ct[1]) / (ct[2] - ct[1])) ; }

	return mezclar(cb[2] , 1.0f , std :: clamp((t - ct[2]) / (1.0f - ct[2]) , 0.0f , 1.0f)) ;

}

std :: string pintura_hlsl(const PinturaForma & p) {

	const std :: uint32_t n = bandas(p) ;

	const std :: string en_bandas = numero_hlsl(static_cast<float>(n)) ;

	std :: string gama ;

	for (std :: uint32_t k = 0 ; k <= n ; k++) {

		const std :: array<float , 3> c = color_de_nivel(p , k) ;

		const float v[3] = { c[0] , c[1] , c[2] } ;

		gama += (k ? ", " : "  ") + float3_de(v) ;

	}

	const std :: array<float , 3> b = fronteras(p) ;

	const float frontera[3] = { b[0] , b[1] , b[2] } ;

	return "static const uint BANDAS = " + std :: to_string(n) + "u;\n"
		"static const float3 GAMA[BANDAS + 1u] = {\n" + gama + "\n};\n"
		"static const float ESCALONADO = " + numero_hlsl(p.escalonado) + ";\n"
		"static const float3 CORTE_T = " + float3_de(p.cortes) + " / " + en_bandas + ";\n"
		"static const float3 CORTE_B = " + float3_de(frontera) + " / " + en_bandas + ";\n"
		R"(
// la temperatura, estirada a trozos : cada color cae en su parte del disco
float repartir(float t) {
  if (t < CORTE_T.x) return t * CORTE_B.x / CORTE_T.x;
  if (t < CORTE_T.y) return lerp(CORTE_B.x, CORTE_B.y, (t - CORTE_T.x) / (CORTE_T.y - CORTE_T.x));
  if (t < CORTE_T.z) return lerp(CORTE_B.y, CORTE_B.z, (t - CORTE_T.y) / (CORTE_T.z - CORTE_T.y));
  return lerp(CORTE_B.z, 1.0, saturate((t - CORTE_T.z) / (1.0 - CORTE_T.z)));
}

// Entre dos niveles se mezcla la luz que les falta (1 − c), no la que tienen :
// del turquesa al amarillo, una mezcla lineal pasaría por verde
float3 gama(float t) {
  float x = saturate(t) * float(BANDAS);
  int k = min(int(x), int(BANDAS) - 1);
  float w = x - float(k);
  return 1.0 - pow(max(1.0 - GAMA[k], 1e-3), 1.0 - w) * pow(max(1.0 - GAMA[k + 1], 1e-3), w);
}

// El color de una temperatura : a escalones suaves, con un filo oscuro entre
// uno y otro
float3 pintar_temperatura(float t) {
  t = repartir(t);
  float x = t * float(BANDAS);
  float f = frac(x);
  float escalon = (floor(x) + smoothstep(0.4, 0.6, f)) / float(BANDAS);
  float3 color = gama(lerp(t, escalon, ESCALONADO));
  color *= 1.0 - 0.2 * (1.0 - smoothstep(0.0, 0.08, abs(f - 0.5)));
  return color;
}
)" ;

}

}  // namespace shd
