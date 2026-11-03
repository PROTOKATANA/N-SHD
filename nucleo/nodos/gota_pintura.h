# pragma once

# include <string>

# include "Buffer.h"

# include "DeviceContext.h"

# include "PipelineState.h"

# include "RefCntAutoPtr.hpp"

# include "ShaderResourceBinding.h"

# include "gpu.h"

namespace shd :: nodos {

// La pintura de la gota : el contorno donde la masa del campo pasa de un umbral, el líquido
// de los canales hasta donde llega, y el calor en las bandas del vórtice. Lee lo que dejan
// el campo (CampoGota) y los canales (CanalesGota)

class PinturaGota {

public:

	struct Lecturas {

		Diligent :: IBuffer * campo ;

		Diligent :: IBuffer * mascara ;

		Diligent :: IBuffer * alcance ;

		Diligent :: IBuffer * cerca ;

		Diligent :: IBuffer * calor ;

	} ;

	// `camara` y `uniforme`, los cbuffers «Camara» y «Gota»

	bool crear(

		const Gpu & gpu ,

		Diligent :: IBuffer * camara ,

		Diligent :: IBuffer * uniforme ,

		const std :: string & hlsl ,

		const Lecturas & lecturas ,

		std :: string & error

	) ;

	// Con el objetivo fijado

	void dibujar(Diligent :: IDeviceContext * ctx) ;

private:

	Diligent :: RefCntAutoPtr<Diligent :: IPipelineState> pipeline_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> binding_ ;

} ;

}  // namespace shd :: nodos
