# pragma once

# include <cstddef>

# include <string>

# include <vector>

# include "Buffer.h"

# include "DeviceContext.h"

# include "RefCntAutoPtr.hpp"

# include "ShaderResourceBinding.h"

# include "computo.h"

# include "gpu.h"

# include "medidor.h"

# include "shd/src/nodos/puente.rs.h"

namespace shd :: nodos {

// Las partículas de la gota : Position Based Fluids, con presión, viscosidad y cohesión,
// sus vecinas ordenadas por celda, el barrido de los nodos y la medida del centro de masa.
// Cada partícula lleva también su calor, que mezcla y reparte el campo (CampoGota)

class ParticulasGota {

public:

	// Lo que leen las demás piezas : el estado (posición y velocidad) de cada pareja, el
	// calor, el centro de masa y su velocidad, y las vecinas (dónde empieza cada celda,
	// cuántas tiene y la lista por celdas)

	struct Lecturas {

		Diligent :: IBuffer * estado[2] ;

		Diligent :: IBuffer * calor[2] ;

		Diligent :: IBuffer * medida ;

		Diligent :: IBuffer * inicio ;

		Diligent :: IBuffer * cuentas ;

		Diligent :: IBuffer * lista ;

	} ;

	// `uniforme`, el cbuffer «Gota»; `hlsl`, lo común de los shaders de la gota

	bool crear(const Gpu & gpu , const DescGota & desc , Diligent :: IBuffer * uniforme , const std :: string & hlsl , std :: string & error) ;

	// Sembrar (la primera vez), barrer, medir y `subpasos` subpasos. Mide en `banco`, de
	// `primera` a `primera` + 4

	void avanzar(Diligent :: IDeviceContext * ctx , Diligent :: Uint32 subpasos , Medidor * banco , std :: size_t primera) ;

	Lecturas lecturas() const ;

	// cuál de la pareja de estado es el actual

	int actual() const { return actual_ ; }

	static std :: vector<std :: string> tramos() ;

private:

	Gpu gpu_ ;

	DescGota desc_{} ;

	// Cada una, por parejas : una se lee y la otra se escribe. `pos`, las posiciones de la
	// presión en el orden de la lista

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> estado_[2] , calor_[2] , pos_[2] ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> prevista_ , partida_ , empuje_ , medida_ , lambda_ ;

	// la rejilla de vecinos : cuántas hay en cada celda, dónde empieza, en qué puesto llegó
	// cada una y la lista, ordenada por celdas

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> cuentas_ , inicio_ , llegada_ , lista_ ;

	Kernel sembrar_ , barrer_ , medir_ , predecir_ , limpiar_ , contar_ , prefijo_ , repartir_ , ordenar_ , reunir_ , lambda_k_ , desplazar_ , velocidad_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> sembrar_b_ , limpiar_b_ , contar_b_ , prefijo_b_ , repartir_b_ , ordenar_b_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> barrer_b_[2] , medir_b_[2] , predecir_b_[2] , reunir_b_[2] , velocidad_b_[2] , lambda_b_[2] , desplazar_b_[2] ;

	int actual_ = 0 ;

	bool sembrado_ = false ;

	bool crear_campos(std :: string & error) ;

	bool crear_kernels(Diligent :: IBuffer * uniforme , const std :: string & hlsl , std :: string & error) ;

	void atar() ;

	void subpaso(Diligent :: IDeviceContext * ctx , Medidor * banco , std :: size_t primera) ;

} ;

}  // namespace shd :: nodos
