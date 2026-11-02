# pragma once

# include "DeviceObject.h"

# include "PipelineState.h"

namespace shd {

// Lo que comparten las pasadas del grafo al crear sus pipelines

// PSO de triángulos con mezcla alfa normal sobre RGBA8, sin profundidad ni
// recorte de caras

Diligent :: GraphicsPipelineStateCreateInfo gpu_pso_mezcla(const char * nombre) ;

// Fija un recurso estático del pipeline. Un recurso usado en los dos stages
// puede verse como dos variables : se fija en cada uno

void gpu_fijar_variable(Diligent :: IPipelineState * pso , const char * nombre , Diligent :: IDeviceObject * objeto) ;

}  // namespace shd
