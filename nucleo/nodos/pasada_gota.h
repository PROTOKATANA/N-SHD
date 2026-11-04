# pragma once

# include <memory>

# include <string>

# include "distancia_gota.h"

# include "pasada.h"

# include "reloj.h"

namespace shd :: nodos {

// El vórtice como un líquido suelto, sin pared : partículas que resuelven
// Navier-Stokes (Position Based Fluids), con presión, viscosidad y cohesión, que
// hace de tensión superficial. La forma sale de ahí : el líquido se estira, salpica
// contra lo que choca y se recompone, y el volumen se conserva porque cada
// partícula es un trozo de líquido. Lo que se calcula en la CPU, en
// motor/src/nodos/gota/.
//
// Se pinta como un campo : cada partícula deja su masa y su temperatura en una
// rejilla, y el contorno es donde la masa pasa de un umbral. Va después del líquido
// y se anima solo. Va en piezas (gota_particulas, gota_campo, gota_canales y
// gota_pintura) : la pasada las une

class PasadaGota : public Pasada , public DistanciaGota {

public:

	explicit PasadaGota(Reloj reloj) ;

	~PasadaGota() override ;

	bool preparar(const Gpu & gpu , const Fotograma & f , CamaraGpu & camara , std :: string & error) override ;

	void subir(Diligent :: IDeviceContext * ctx , const Fotograma & f) override ;

	void dibujar(Diligent :: IDeviceContext * ctx , const Fotograma & f) override ;

	bool animada(const Escena & escena) const override { return escena.vortice != 0 ; }

	const char * nombre() const override { return "gota" ; }

	std :: vector<std :: string> tramos() const override ;

	Diligent :: IBuffer * distancia_gota() const override ;

	void medir(Medidor * banco , std :: size_t primera) override { banco_ = banco ; primera_ = primera ; }

private:

	struct Recursos ;

	std :: unique_ptr<Recursos> recursos_ ;

	Reloj reloj_ ;

	rust :: Box<EstadoGota> estado_ ;

	GotaGpu datos_{} ;

	bool hay_ = false ;

	Medidor * banco_ = nullptr ;

	std :: size_t primera_ = 0 ;

} ;

}  // namespace shd :: nodos
