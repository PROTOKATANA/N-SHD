# pragma once

# include <cstddef>

# include <string>

# include "Buffer.h"

# include "DeviceContext.h"

# include "RefCntAutoPtr.hpp"

# include "ShaderResourceBinding.h"

# include "computo.h"

# include "gota_particulas.h"

# include "gpu.h"

# include "medidor.h"

# include "shd/src/nodos/puente.rs.h"

namespace shd :: nodos {

// El campo de la gota : cada partícula mezcla su calor con el de sus vecinas, lo lleva hacia
// el del remolino y deja su masa, su calor y su velocidad en las celdas que toca, en coma
// fija. De ahí salen el contorno, el color y lo que la gota da a los canales

class CampoGota {

public:

	// `particulas`, lo que leen de las partículas : estado, calor, medida y vecinas

	bool crear(

		const Gpu & gpu ,

		const DescGota & desc ,

		Diligent :: IBuffer * uniforme ,

		const std :: string & hlsl ,

		const ParticulasGota :: Lecturas & particulas ,

		std :: string & error

	) ;

	// Con el estado `actual` de las partículas. Mide en `banco`, en `primera`

	void avanzar(Diligent :: IDeviceContext * ctx , int actual , Medidor * banco , std :: size_t primera) ;

	// masa y calor por celda (uint, dos por celda), y velocidad por celda (int, dos por celda)

	Diligent :: IBuffer * campo() const { return campo_ ; }

	Diligent :: IBuffer * velocidad() const { return campo_v_ ; }

	static std :: vector<std :: string> tramos() { return { "gota campo" } ; }

private:

	Gpu gpu_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> campo_ , campo_v_ ;

	// el calor de las partículas : una de la pareja se lee y la otra se escribe

	Diligent :: IBuffer * calor_[2] = { nullptr , nullptr } ;

	Kernel limpiar_ , salpicar_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> limpiar_b_ ;

	// [cuál de estado es el actual][cuál de calor]

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> salpicar_b_[2][2] ;

	int calor_actual_ = 0 ;

} ;

}  // namespace shd :: nodos
