// Pruebas de las piezas del agujero que siguen en C++ (sin GPU
// ni Diligent) : la pintura y la paleta, que acaban en HLSL. La física, la órbita,
// lo agitado y el guion se prueban en Rust (motor/, con cargo test). Sin
// biblioteca de pruebas : cada comprobación que falla lo dice y el programa
// sale con error. Se compilan y corren con pruebas/probar.sh

# include <cctype>

# include <cmath>

# include <cstdio>

# include <string>

# include "pintura_disco.h"

# include "paleta_polvo.h"

using namespace shd ;

namespace {

int fallos = 0 ;

void comprobar(bool bien , const char * que) {

	if (!bien) {

		std :: printf("FALLA : %s\n" , que) ;

		fallos++ ;

	}

}

bool cerca(double a , double b , double margen = 1e-9) { return std :: fabs(a - b) < margen ; }

}  // namespace

// Los colores del polvo : todos con la luminancia del blanco (ninguno brilla
// más que otro), el blanco tal cual, los pesos acumulados crecientes y
// hasta 1, y el HLSL con punto decimal

void la_paleta_del_polvo_brilla_igual() {

	const PolvoForma & f = forma_agujero.polvo ;

	const PaletaPolvo p = paleta_polvo(f , 2.2f) ;

	bool igual = true , crece = true ;

	for (int k = 0 ; k < colores_polvo ; k++) {

		igual = igual && cerca(luminancia(p.lineal[k]) , luminancia(p.lineal[0]) , 1e-9) ;

		crece = crece && (k == 0 || p.hasta[k] >= p.hasta[k - 1]) ;

	}

	comprobar(igual && cerca(p.lineal[0][0] , std :: pow(f.colores.blanco[0] , 2.2f) , 1e-12) , "la paleta del polvo : todos con la luminancia del blanco") ;

	comprobar(crece && cerca(p.hasta[colores_polvo - 1] , 1.0 , 1e-6) , "la paleta del polvo : los pesos acumulados, hasta 1") ;

	const std :: string h = paleta_hlsl(p) ;

	bool coma = false ;

	for (std :: size_t i = 1 ; i + 1 < h.size() ; i++) { coma = coma || (h[i] == ',' && std :: isdigit(static_cast<unsigned char>(h[i - 1])) && std :: isdigit(static_cast<unsigned char>(h[i + 1]))) ; }

	comprobar(!coma && h.find("PALETA[" + std :: to_string(colores_polvo) + "]") != std :: string :: npos , "la paleta del polvo : el HLSL, con punto decimal") ;

}

void la_pintura_reparte_los_colores() {

	const PinturaForma & p = forma_agujero.pintura ;

	comprobar(bandas(p) == 9 , "10 niveles : 9 bandas") ;

	const float azul = p.colores[0][2] , verde = p.colores[1][1] , rojo = p.colores[3][0] ;

	comprobar(color_de_nivel(p , 0)[2] == azul && color_de_nivel(p , 1)[2] == azul , "los dos primeros niveles, azules") ;

	comprobar(color_de_nivel(p , 2)[1] == verde && color_de_nivel(p , 3)[0] == p.colores[2][0] , "luego el verde y el amarillo") ;

	comprobar(color_de_nivel(p , 5)[0] == rojo && color_de_nivel(p , 9)[0] == rojo , "y del 5 al 9, rojo") ;

	comprobar(repartir(p , 0.0f) == 0.0f && cerca(repartir(p , 1.0f) , 1.0 , 1e-6) , "repartir va de 0 a 1") ;

	// cada corte cae en la frontera entre un color y el siguiente

	comprobar(cerca(repartir(p , p.cortes[1] / 9) , 2.5 / 9 , 1e-5) , "el corte del verde al amarillo, entre los niveles 2 y 3") ;

	float antes = -1 ;

	bool crece = true ;

	for (int k = 0 ; k <= 1000 ; k++) {

		const float r = repartir(p , k / 1000.0f) ;

		crece = crece && r >= antes && r >= 0 && r <= 1 ;

		antes = r ;

	}

	comprobar(crece , "repartir crece, sin salirse de 0 a 1") ;

	const std :: string h = pintura_hlsl(p) ;

	comprobar(h.find("static const float3 CORTE_T = float3(1.5, 2.87, 5.38) / 9.0;") != std :: string :: npos , "el HLSL, con punto decimal") ;

}

int main() {

	la_pintura_reparte_los_colores() ;

	la_paleta_del_polvo_brilla_igual() ;

	if (fallos) {

		std :: printf("%d comprobaciones fallaron\n" , fallos) ;

		return 1 ;

	}

	std :: printf("pruebas del agujero : todo bien\n") ;

	return 0 ;

}
