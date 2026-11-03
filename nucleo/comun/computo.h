# pragma once

# include <initializer_list>

# include <string>

# include <vector>

# include "Buffer.h"

# include "DeviceContext.h"

# include "PipelineState.h"

# include "RefCntAutoPtr.hpp"

# include "RenderDevice.h"

# include "Shader.h"

# include "ShaderResourceBinding.h"

# include "gpu.h"

// Cálculo en la GPU con compute shaders : lo que necesita cualquier
// simulación sobre una rejilla (compilar kernels, crear campos, atarlos y
// lanzarlos). Es a los kernels lo que pipeline_comun es a dibujar

namespace shd {

// Un recurso atado por su nombre en el shader

struct Recurso {

	const char * nombre ;

	Diligent :: IDeviceObject * objeto ;

} ;

// Un shader : su tipo, su nombre y su HLSL (con su main)

struct DescShader {

	Diligent :: SHADER_TYPE tipo ;

	const char * nombre ;

	std :: string codigo ;

} ;

Diligent :: RefCntAutoPtr<Diligent :: IShader> gpu_compilar(const Gpu & gpu , const DescShader & d) ;

// Un kernel : su HLSL, sus cbuffers (estáticos) y los grupos de cada
// lanzamiento

struct DescKernel {

	const char * nombre ;

	std :: string codigo ;

	std :: vector<Recurso> fijos ;

	Diligent :: Uint32 grupos_x ;

	Diligent :: Uint32 grupos_y ;

} ;

// Los buffers se atan una vez en bindings preparados (gpu_atar), uno por cada
// combinación de campos que use el kernel, y al lanzar solo se elige cuál
// (buscar cada buffer por su nombre y atarlo en cada lanzamiento costaba la
// mitad de la CPU del paso)

struct Kernel {

	Diligent :: RefCntAutoPtr<Diligent :: IPipelineState> pipeline ;

	Diligent :: Uint32 grupos_x = 0 ;

	Diligent :: Uint32 grupos_y = 0 ;

} ;

// Sin pipeline si no se pudo crear (el motivo, en `error`)

Kernel gpu_kernel(const Gpu & gpu , const DescKernel & d , std :: string & error) ;

// Un binding del kernel con estos buffers atados

Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> gpu_atar(const Kernel & k , std :: initializer_list<Recurso> recursos) ;

// Lo mismo, con una lista que se arma en marcha (los recursos de un puerto)

Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> gpu_atar(const Kernel & k , const std :: vector<Recurso> & recursos) ;

// Lanza el kernel con un binding preparado. Diligent pone las barreras entre
// un kernel que escribe un buffer y el siguiente que lo lee

void gpu_lanzar(Diligent :: IDeviceContext * ctx , const Kernel & k , Diligent :: IShaderResourceBinding * atado) ;

// Lo mismo, y espera a que se escriba lo de `escritos` antes del siguiente : una barrera
// entre dos kernels que escriben y leen el mismo buffer como UAV

void gpu_lanzar(

	Diligent :: IDeviceContext * ctx ,

	const Kernel & k ,

	Diligent :: IShaderResourceBinding * atado ,

	std :: initializer_list<Diligent :: IBuffer *> escritos

) ;

// Un campo : un buffer estructurado de `elementos` de `paso` bytes, a ceros,
// que se lee y se escribe desde los shaders

struct DescCampo {

	Diligent :: Uint64 elementos ;

	Diligent :: Uint32 paso ;

	const char * nombre ;

} ;

Diligent :: RefCntAutoPtr<Diligent :: IBuffer> gpu_campo(const Gpu & gpu , const DescCampo & d) ;

Diligent :: IBufferView * gpu_lectura(Diligent :: IBuffer * b) ;

Diligent :: IBufferView * gpu_escritura(Diligent :: IBuffer * b) ;

}  // namespace shd
