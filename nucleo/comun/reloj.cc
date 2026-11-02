# include "reloj.h"

# include <chrono>

namespace shd {

Reloj reloj_monotono() {

	return [] { return std :: chrono :: duration<double>(std :: chrono :: steady_clock :: now().time_since_epoch()).count() ; } ;

}

Reloj reloj_de_pasos(double paso) {

	return [paso , pasos = 0.0]() mutable { return ++pasos * paso ; } ;

}

}  // namespace shd
