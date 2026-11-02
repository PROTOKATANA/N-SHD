# pragma once

# include "fluido_gpu.h"

namespace shd :: nodos {

// Stable Fluids (Stam) con compute shaders : en cada paso, advección
// semilagrangiana de la velocidad y del escalar (con las fuerzas del efecto),
// refuerzo de los remolinos pequeños (vorticity confinement), divergencia,
// presión por Jacobi y proyección. La pared del círculo no deja pasar el
// fluido

FabricaFluido fabrica_fluido_stam() ;

}  // namespace shd :: nodos
