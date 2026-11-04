# pragma once

# include <memory>

# include <string>

# include "distancia_gota.h"

# include "pasada.h"

namespace shd :: nodos {

// Los enlaces como un líquido (una gota alrededor de cada nodo, unida a sus
// vecinos por cuellos que se estrechan en el medio). Es un campo de distancia :
// unión suave (smin polinómica) de una gota por nodo y una cápsula fina por
// enlace, evaluado en un quad que cubre la caja del grafo. El color se mezcla
// con el mismo factor de la smin : cada cuello pasa del color de un nodo al del
// otro

class PasadaLiquido : public Pasada {

public:

	// `con_rejilla` en false : la lista completa en cada píxel, para comparar
	// lo que se pinta con y sin rejilla. `gota`, si la hay : la del vórtice envuelto
	// (sin vórtice libre) es un anillo alrededor de la gota, que sigue su forma. No es
	// suya : quien las crea la mantiene viva mientras viva esta

	explicit PasadaLiquido(bool con_rejilla = true , const DistanciaGota * gota = nullptr) ;

	~PasadaLiquido() override ;

	bool preparar(const Gpu & gpu , const Fotograma & f , CamaraGpu & camara , std :: string & error) override ;

	void subir(Diligent :: IDeviceContext * ctx , const Fotograma & f) override ;

	void dibujar(Diligent :: IDeviceContext * ctx , const Fotograma & f) override ;

	const char * nombre() const override { return "liquido" ; }

private:

	struct Pipelines ;

	std :: unique_ptr<Pipelines> pipes_ ;

	LiquidoGpu liquido_{} ;

	bool con_rejilla_ ;

	const DistanciaGota * gota_ ;

	// la distancia que tiene atada el pintado : cambia si la gota se rehace

	Diligent :: IBuffer * atada_ = nullptr ;

	bool hay_ = false ;

} ;

}  // namespace shd :: nodos
