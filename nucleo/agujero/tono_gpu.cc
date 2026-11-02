# include "tono_gpu.h"

# include <string>

# include "computo.h"

# include "gpu.h"

using namespace Diligent ;

namespace shd {

namespace {

// Un triángulo que cubre toda la superficie

constexpr const char * tono_vs = R"(
void main(in uint vid : SV_VertexID, out float4 posicion : SV_POSITION) {
  float2 uv = float2(float((vid << 1u) & 2u), float(vid & 2u));
  posicion = float4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)" ;

constexpr const char * tono_ps = R"(
cbuffer Tono {
  uint4 tam;          // ancho, alto (px), 0, 0
  float4 exposicion;  // la exposición, 0, 0, 0
};
StructuredBuffer<uint> acumulado;

float4 main(in float4 posicion : SV_POSITION) : SV_TARGET {
  uint2 q = min(uint2(posicion.xy), uint2(tam.x - 1u, tam.y - 1u));
  uint k = (q.y * tam.x + q.x) * 3u;
  float3 luz = float3(acumulado[k], acumulado[k + 1u], acumulado[k + 2u]) / 256.0;
  return float4(1.0 - exp(-luz * exposicion.x), 1.0);
}
)" ;

}  // namespace

bool TonoGpu :: crear(const Gpu & gpu , float exposicion , std :: string & error) {

	exposicion_ = exposicion ;

	ancho_ = alto_ = 0 ;

	BufferDesc desc ;

	desc.Name = "Agujero tono uniforms" ;

	desc.Size = 32 ;

	desc.Usage = USAGE_DEFAULT ;

	desc.BindFlags = BIND_UNIFORM_BUFFER ;

	desc.ImmediateContextMask = gpu.mascara ;

	gpu.device->CreateBuffer(desc , nullptr , &uniforme_) ;

	if (!uniforme_) { return gpu_fallo(error , "No se pudo crear el cbuffer del tono") ; }

	RefCntAutoPtr<IShader> vs = gpu_compilar(gpu , { SHADER_TYPE_VERTEX , "Agujero tono VS" , tono_vs }) ;

	RefCntAutoPtr<IShader> ps = gpu_compilar(gpu , { SHADER_TYPE_PIXEL , "Agujero tono PS" , tono_ps }) ;

	if (!vs || !ps) { return gpu_fallo(error , "No se pudieron compilar los shaders del tono") ; }

	GraphicsPipelineStateCreateInfo pso ;

	pso.PSODesc.Name = "Agujero tono" ;

	pso.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS ;

	pso.GraphicsPipeline.NumRenderTargets = 1 ;

	pso.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM ;

	pso.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ;

	pso.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE ;

	pso.GraphicsPipeline.DepthStencilDesc.DepthEnable = False ;

	const ShaderResourceVariableDesc fijas[] = { { SHADER_TYPE_PIXEL , "Tono" , SHADER_RESOURCE_VARIABLE_TYPE_STATIC } } ;

	pso.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC ;

	pso.PSODesc.ResourceLayout.Variables = fijas ;

	pso.PSODesc.ResourceLayout.NumVariables = 1 ;

	pso.pVS = vs ;

	pso.pPS = ps ;

	pso.PSODesc.ImmediateContextMask = gpu.mascara ;

	gpu.device->CreateGraphicsPipelineState(pso , &pso_) ;

	if (!pso_) { return gpu_fallo(error , "No se pudo crear el pipeline del tono") ; }

	if (auto * v = pso_->GetStaticVariableByName(SHADER_TYPE_PIXEL , "Tono")) { v->Set(uniforme_) ; }

	pso_->CreateShaderResourceBinding(&binding_ , true) ;

	return true ;

}

void TonoGpu :: dibujar(IDeviceContext * ctx , IBuffer * acumulado , std :: uint32_t ancho , std :: uint32_t alto) {

	if (ancho != ancho_ || alto != alto_) {

		struct { std :: uint32_t tam[4] ; float exposicion[4] ; } datos{ { ancho , alto , 0 , 0 } , { exposicion_ , 0 , 0 , 0 } } ;

		ctx->UpdateBuffer(uniforme_ , 0 , sizeof(datos) , &datos , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

		ancho_ = ancho ;

		alto_ = alto ;

	}

	if (auto * v = binding_->GetVariableByName(SHADER_TYPE_PIXEL , "acumulado")) { v->Set(gpu_lectura(acumulado)) ; }

	ctx->SetPipelineState(pso_) ;

	ctx->CommitShaderResources(binding_ , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	DrawAttribs draw ;

	draw.NumVertices = 3 ;

	ctx->Draw(draw) ;

}

}  // namespace shd
