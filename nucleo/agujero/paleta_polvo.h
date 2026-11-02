# pragma once

# include <array>

# include <string>

# include "forma_agujero.h"

namespace shd {

// Los colores del polvo (ver PolvoForma) : el blanco (el de las hebras) y
// los de la paleta (colores_polvo en total), en luz lineal y llevados todos a la luminancia del
// blanco (Rec. 709 : ninguno brilla más que otro), con sus pesos acumulados
// para elegir uno con un número al azar de 0 a 1.
//
// Pieza pura : se prueba sola (pruebas/pruebas.cc), y su HLSL es lo que usan
// el polvo (para elegir el color de cada partícula) y la búsqueda del cielo
// (para pintarlo)

struct PaletaPolvo {

	std :: array<std :: array<double , 3> , colores_polvo> lineal ;

	std :: array<double , colores_polvo> hasta ;  // los pesos acumulados (el último, 1)

} ;

// Con la pantalla a la potencia gamma (del valor de pantalla a luz lineal,
// en float : así sale el mismo HLSL de siempre)

PaletaPolvo paleta_polvo(const PolvoForma & f , float gamma) ;

double luminancia(const std :: array<double , 3> & lineal) ;

// El HLSL : COLORES, PALETA (float3, en luz lineal) y PESO_HASTA

std :: string paleta_hlsl(const PaletaPolvo & p) ;

}  // namespace shd
