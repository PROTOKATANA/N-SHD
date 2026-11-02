# pragma once

# include <array>

# include <cstdint>

# include <string>

# include "forma_agujero.h"

namespace shd {

// La pintura del disco : de su temperatura (de 0, fría, a 1) a su color. Por
// bandas, como el vórtice de nodos : la temperatura sube a escalones, con un
// filo oscuro entre uno y otro. Cada color, de fuera al ojo, ocupa sus
// niveles (los escalones), y antes la temperatura se estira a trozos
// (repartir) para que cada color cubra la parte del disco que se quiera :
// las bandas de fuera ocupan mucho más que las de dentro (ver PinturaForma).
//
// Pieza pura : el HLSL (pintar_temperatura(t), para quien pinta el disco) y
// lo mismo en C++ donde hace falta probarlo (pruebas/pruebas.cc)

// El HLSL : BANDAS, GAMA, ESCALONADO, CORTE_T, CORTE_B y las funciones
// repartir, gama y pintar_temperatura

std :: string pintura_hlsl(const PinturaForma & p) ;

// Los intervalos entre niveles (los niveles son uno más)

std :: uint32_t bandas(const PinturaForma & p) ;

// El color del nivel k (de 0, el del borde, a bandas(p), el del ojo)

std :: array<float , 3> color_de_nivel(const PinturaForma & p , std :: uint32_t k) ;

// La temperatura estirada a trozos : cada corte (en bandas) va a la frontera
// entre los niveles de su color y los del siguiente. De 0 a 1, creciente

float repartir(const PinturaForma & p , float t) ;

}  // namespace shd
