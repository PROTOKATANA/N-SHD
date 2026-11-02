# pragma once

# include <functional>

namespace shd {

// El reloj del vórtice : segundos de un reloj monótono. Se inyecta : la app
// usa el de verdad, y los bancos y las comprobaciones uno de pasos fijos,
// con el que la simulación es reproducible (el mismo estado, bit a bit, en
// cada ejecución)

using Reloj = std :: function<double()> ;

// El de la app : steady_clock

Reloj reloj_monotono() ;

// Avanza `paso` segundos en cada lectura (la primera ya da `paso`)

Reloj reloj_de_pasos(double paso) ;

}  // namespace shd
