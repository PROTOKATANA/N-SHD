# pragma once

# include <memory>

# include <string>

# include "fluido_gpu.h"

# include "pasada.h"

# include "reloj.h"

namespace shd :: nodos {

// El vórtice (ver motor/src/nodos/vortice.rs) : en cada fotograma avanza su
// fluido (un Fluido, inyectado, con el forzamiento del vórtice : el remolino,
// la turbulencia, la agitación al pulsar y el calor) y pinta el círculo con la
// temperatura en la gama de calor. Va después del líquido, y se anima solo :
// mientras la escena tiene vórtice, se redibuja en cada fotograma.
//
// Con `diagnostico`, cada 120 pasos lee el fluido de la GPU y dice por el aviso
// de `Gpu` su huella, cuánto se aleja de uno incompresible, la velocidad máxima
// y si hay NaN (para comprobar que la simulación es de verdad, estable y, con el
// reloj de pasos fijos, la misma tras cada optimización)

class PasadaVortice : public Pasada {

public:

	// El reloj y la fábrica del fluido (el solver), inyectados

	PasadaVortice(Reloj reloj , FabricaFluido fabrica , bool diagnostico = false) ;

	~PasadaVortice() override ;

	bool preparar(const Gpu & gpu , const Fotograma & f , CamaraGpu & camara , std :: string & error) override ;

	void subir(Diligent :: IDeviceContext * ctx , const Fotograma & f) override ;

	void dibujar(Diligent :: IDeviceContext * ctx , const Fotograma & f) override ;

	bool animada(const Escena & escena) const override { return escena.vortice != 0 ; }

	const char * nombre() const override { return "vortice" ; }

	// los del fluido, que se crea con el dispositivo

	std :: vector<std :: string> tramos() const override ;

	void medir(Medidor * banco , std :: size_t primera) override ;

private:

	// El fluido, el cbuffer del vórtice y el pipeline del pintado, en un
	// dispositivo

	struct Recursos ;

	std :: unique_ptr<Recursos> recursos_ ;

	Reloj reloj_ ;

	FabricaFluido fabrica_ ;

	rust :: Box<EstadoVortice> estado_ ;

	FotogramaVortice fotograma_{} ;

	bool diagnostico_ ;

	bool hay_ = false ;

	Medidor * banco_ = nullptr ;

	std :: size_t primera_ = 0 ;

	void leer_diagnostico(const Gpu & gpu) ;

} ;

}  // namespace shd :: nodos
