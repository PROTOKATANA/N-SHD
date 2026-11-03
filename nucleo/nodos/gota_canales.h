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

namespace shd :: nodos {

// El fluido de los canales : Navier-Stokes en una rejilla que cubre la vista de la gota,
// dentro de la forma elástica de los enlaces. La gota le da, donde está, su velocidad, su
// calor y su líquido (lo lee del campo); desde ahí el líquido se esparce por donde las
// paredes lo dejan, y se sabe hasta dónde llega y lo lejos que queda la gota

class CanalesGota {

public:

	// `campo` y `velocidad`, los del campo de la gota (CampoGota)

	bool crear(

		const Gpu & gpu ,

		Diligent :: IBuffer * uniforme ,

		const std :: string & hlsl ,

		Diligent :: IBuffer * campo ,

		Diligent :: IBuffer * velocidad ,

		std :: string & error

	) ;

	// Un paso por fotograma. Mide en `banco`, de `primera` a `primera` + 4

	void avanzar(Diligent :: IDeviceContext * ctx , Medidor * banco , std :: size_t primera) ;

	// Lo que lee la pintura : qué es cada celda (pared 0, canal 1, gota 2), hasta dónde llega
	// el líquido y con qué color (float2), lo lejos que queda la gota y el calor

	Diligent :: IBuffer * mascara() const { return masc_ ; }

	Diligent :: IBuffer * alcance() const { return alc_[0] ; }

	Diligent :: IBuffer * cerca() const { return cer_[0] ; }

	Diligent :: IBuffer * calor() const { return cal_[0] ; }

	static std :: vector<std :: string> tramos() ;

private:

	Gpu gpu_ ;

	// velocidad, calor, alcance, cerca y presión van por parejas

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> vel_[2] , cal_[2] , alc_[2] , cer_[2] , pres_[2] , div_ , masc_ , imp_ ;

	Kernel mascara_k_ , cerca_k_ , advectar_ , alcance_k_ , difundir_ , divergencia_ , jacobi_ , proyectar_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> mascara_b_ , advectar_b_ , alcance_b_[2] , cerca_b_[2] , difundir_b_ , divergencia_b_ , jacobi_b_[2] , proyectar_b_ ;

	bool crear_campos(std :: string & error) ;

	bool crear_kernels(Diligent :: IBuffer * uniforme , const std :: string & hlsl , std :: string & error) ;

	void atar(Diligent :: IBuffer * campo , Diligent :: IBuffer * velocidad) ;

} ;

}  // namespace shd :: nodos
