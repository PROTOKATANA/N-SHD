# pragma once

# include "Buffer.h"

namespace shd :: nodos {

// Lo lejos que queda la gota en cada celda de la rejilla de sus canales, en radios : un float
// por celda, de lado_canal × lado_canal, centrada en su nodo y de ± VISTA radios (gota_comun.h).
// La da PasadaGota y la lee PasadaLiquido : así el anillo que envuelve la gota sigue su forma.
// Nulo mientras la gota no existe en el dispositivo

class DistanciaGota {

public:

	virtual ~DistanciaGota() = default ;

	virtual Diligent :: IBuffer * distancia_gota() const = 0 ;

} ;

}  // namespace shd :: nodos
