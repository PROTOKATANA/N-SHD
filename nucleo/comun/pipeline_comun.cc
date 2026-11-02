# include "pipeline_comun.h"

using namespace Diligent ;

namespace shd {

GraphicsPipelineStateCreateInfo gpu_pso_mezcla(const char * nombre) {

	GraphicsPipelineStateCreateInfo pso ;

	pso.PSODesc.Name = nombre ;

	pso.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS ;

	pso.GraphicsPipeline.NumRenderTargets = 1 ;

	pso.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM ;

	pso.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ;

	pso.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE ;

	pso.GraphicsPipeline.DepthStencilDesc.DepthEnable = False ;

	auto & mezcla = pso.GraphicsPipeline.BlendDesc.RenderTargets[0] ;

	mezcla.BlendEnable = True ;

	mezcla.SrcBlend = BLEND_FACTOR_SRC_ALPHA ;

	mezcla.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA ;

	mezcla.SrcBlendAlpha = BLEND_FACTOR_ONE ;

	mezcla.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA ;

	return pso ;

}

void gpu_fijar_variable(IPipelineState * pso , const char * nombre , IDeviceObject * objeto) {

	for (auto stage : { SHADER_TYPE_VERTEX , SHADER_TYPE_PIXEL }) {

		if (auto * v = pso->GetStaticVariableByName(stage , nombre)) { v->Set(objeto) ; }

	}

}

}  // namespace shd
