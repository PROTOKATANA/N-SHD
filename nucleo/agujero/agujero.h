# pragma once

# include <cstddef>

# include <cstdint>

# include <memory>

# include <string>

# include <vector>

# include "gpu.h"

# include "medidor.h"

# include "reloj.h"

# include "shd/src/agujero/puente.rs.h"

namespace shd {

// El agujero negro de Kerr, lo único que necesita quien lo integra. No crea el
// dispositivo ni conoce la ventana : recibe un `Gpu` y pinta en el objetivo que
// tenga fijado el contexto, opaco, sin copias. La lógica (la órbita, lo
// agitado, la física) va en Rust, y se le da la entrada por `agujero :: entrada`.
//
// Una vez por fotograma, en el hilo de ese contexto :
//
//   crear    una vez por dispositivo : los kernels, en paralelo
//   ajustar  al tamaño de la vista; antes de pedir el turno a la GPU
//   avanzar  con el turno : los rayos si hace falta, el fluido, el polvo y la
//            composición
//   pintar   con el objetivo fijado, y el viewport en la región de la vista
//
// Sin vista todavía, `asentar` deja el fluido y el polvo listos, para que el
// primer fotograma salga en cuanto se pida

class Agujero {

public:

	explicit Agujero(Reloj reloj) ;

	~Agujero() ;

	Agujero(const Agujero &) = delete ;

	Agujero & operator=(const Agujero &) = delete ;

	// El nombre de cada pasada, en el orden que usa `medir`

	static std :: vector<std :: string> pasadas() ;

	// Se usa a partir del siguiente `crear`. Sin medidor, no se mide

	void medir(Medidor * medidor) ;

	bool crear(const Gpu & gpu , std :: string & error) ;

	bool ajustar(std :: uint32_t ancho , std :: uint32_t alto , std :: string & error) ;

	bool asentar(std :: uint32_t ancho , std :: uint32_t alto) ;

	bool avanzar(std :: uint32_t ancho , std :: uint32_t alto , std :: string & error) ;

	void pintar(std :: uint32_t ancho , std :: uint32_t alto) ;

	// Los segundos desde el primer fotograma

	double transcurrido() const ;

	// Los volcados del guion de ensayo

	agujero :: Motor & motor() ;

private:

	struct Impl ;

	std :: unique_ptr<Impl> impl_ ;

} ;

}  // namespace shd
