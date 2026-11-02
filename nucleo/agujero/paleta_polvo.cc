# include "paleta_polvo.h"

# include <algorithm>

# include <cmath>

# include "numero_hlsl.h"

namespace shd {

double luminancia(const std :: array<double , 3> & c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2] ; }

PaletaPolvo paleta_polvo(const PolvoForma & f , float gamma) {

	auto lineal = [gamma](const float (& c)[3]) { return std :: array<double , 3>{ std :: pow(c[0] , gamma) , std :: pow(c[1] , gamma) , std :: pow(c[2] , gamma) } ; } ;

	const std :: array<double , 3> blanco = lineal(f.colores.blanco) ;

	PaletaPolvo p ;

	double hasta = 0 ;

	for (int k = 0 ; k < colores_polvo ; k++) {

		const std :: array<double , 3> c = k == 0 ? blanco : lineal(f.colores.paleta[k - 1]) ;

		const double a_blanco = luminancia(blanco) / std :: max(luminancia(c) , 1e-6) ;

		p.lineal[k] = { c[0] * a_blanco , c[1] * a_blanco , c[2] * a_blanco } ;

		hasta += f.colores.pesos[k] ;

		p.hasta[k] = hasta ;

	}

	return p ;

}

std :: string paleta_hlsl(const PaletaPolvo & p) {

	std :: string colores , pesos ;

	for (int k = 0 ; k < colores_polvo ; k++) {

		const std :: array<double , 3> & c = p.lineal[k] ;

		colores += std :: string(k ? ", " : "") + "float3(" + numero_hlsl(static_cast<float>(c[0])) + ", " + numero_hlsl(static_cast<float>(c[1])) + ", " + numero_hlsl(static_cast<float>(c[2])) + ")" ;

		pesos += std :: string(k ? ", " : "") + numero_hlsl(static_cast<float>(p.hasta[k])) ;

	}

	const std :: string n = std :: to_string(colores_polvo) ;

	return "#define COLORES " + n + "u\nstatic const float3 PALETA[" + n + "] = { " + colores + " };\nstatic const float PESO_HASTA[" + n + "] = { " + pesos + " };\n" ;

}

}  // namespace shd
