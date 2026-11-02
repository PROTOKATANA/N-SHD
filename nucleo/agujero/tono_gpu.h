# pragma once

# include <cstdint>

# include <string>

# include "Buffer.h"

# include "DeviceContext.h"

# include "PipelineState.h"

# include "RefCntAutoPtr.hpp"

# include "RenderDevice.h"

# include "ShaderResourceBinding.h"

# include "gpu.h"

namespace shd {

// Pinta el acumulado (la luz de cada píxel, tres uint por píxel en 1/256,
// ver componer.h) sobre el objetivo fijado : la luz se satura suave
// (1 − e^−luz·exposición). Lleva su propio cbuffer «Tono» (el tamaño y la
// exposición) : no depende del de nadie. Todo en el hilo que pinta

class TonoGpu {

public:

	bool crear(const Gpu & gpu , float exposicion , std :: string & error) ;

	// El acumulado es de ancho × alto píxeles

	void dibujar(Diligent :: IDeviceContext * ctx , Diligent :: IBuffer * acumulado , std :: uint32_t ancho , std :: uint32_t alto) ;

private:

	Diligent :: RefCntAutoPtr<Diligent :: IPipelineState> pso_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> binding_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> uniforme_ ;

	float exposicion_ = 1 ;

	// lo último que se subió al cbuffer (se sube solo si cambia)

	std :: uint32_t ancho_ = 0 , alto_ = 0 ;

} ;

}  // namespace shd
