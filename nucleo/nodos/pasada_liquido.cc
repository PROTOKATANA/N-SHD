# include "pasada_liquido.h"

# include <string>

# include <utility>

# include "PipelineState.h"

# include "RefCntAutoPtr.hpp"

# include "Shader.h"

# include "ShaderResourceBinding.h"

# include "computo.h"

# include "gota_comun.h"

# include "gpu.h"

# include "numero_hlsl.h"

# include "pipeline_comun.h"

using namespace Diligent ;

namespace shd :: nodos {

namespace {

constexpr const char * liquido_vs = R"(
cbuffer Camara { float4 transform; float4 caja; float4 extra; };
struct Salida { float4 Position : SV_POSITION; float2 Mundo : TEXCOORD0; };
void main(in uint vid : SV_VertexID, out Salida o) {
  float2 esquinas[6] = { float2(0,0), float2(1,0), float2(1,1),
                         float2(0,0), float2(1,1), float2(0,1) };
  float2 w = lerp(caja.xy, caja.zw, esquinas[vid]);
  float2 ndc = w * transform.xy + transform.zw;
  // Vulkan y GLES recorren la imagen importada desde bordes verticales opuestos.
  o.Position = float4(ndc.x, -ndc.y, 0.0, 1.0);
  o.Mundo = w;
})" ;

constexpr const char * liquido_ps = R"(
cbuffer Camara { float4 transform; float4 caja; float4 extra; };
cbuffer Liquido {
  float4 forma;           // gota, cuello, fusión, opacidad
  float4 cuenta;          // oscurecer, nodos, enlaces, filas de la rejilla
  float4 rejilla;         // x0, y0, lado de celda (mundo), columnas
  float4 nodos[256];      // x, y, radio que se ve (mundo), lo que se hunde su gota (mundo)
  float4 colores[256];    // rgb, y en a si su gota es el anillo de la gota envuelta
  uint4 aristas[256];     // dos enlaces por elemento: (a0, b0, a1, b1)
  uint4 celdas[256];      // por celda: inicio (16 bits bajos) y cuántos (16 altos)
  uint4 indices[3068];    // dos elementos de 16 bits por uint: nodo i, o enlace nodos + e
  float4 anillo;          // la gota envuelta : centro (x, y) y radio (mundo) de su rejilla, y lo que asoma su anillo (radios)
};
struct Entrada { float4 Position : SV_POSITION; float2 Mundo : TEXCOORD0; };

// Unión suave polinómica; h es el peso de a (sirve también para el color).
// Se escribe como min(a, b) menos el redondeo, no como lerp(b, a, h) - ...:
// con b = 1e9 (el campo vacío) el lerp resta números grandes y en float32
// redondea la distancia a múltiplos de 64 (un disco de borde duro).
float smin(float a, float b, float k, out float h) {
  h = saturate(0.5 + 0.5 * (b - a) / k);
  float m = max(k - abs(a - b), 0.0) / k;
  return min(a, b) - m * m * k * 0.25;
}

#ifdef ANILLO_GOTA
// Lo lejos que queda la gota (DistanciaGota) : en radios, en su rejilla de M × M celdas de ± VISTA
// radios alrededor de su nodo, entre las cuatro celdas de alrededor. Fuera de la rejilla, lejos
StructuredBuffer<float> distancia_gota;
float lejos_de_la_gota(float2 q) {
  float2 g = (q + VISTA) / (2.0 * VISTA / float(M)) - 0.5;
  if (any(g < 0.0) || any(g > float(M) - 1.001)) return 1e3;
  int2 a = int2(floor(g));
  float2 f = g - float2(a);
  int i = a.y * int(M) + a.x;
  return lerp(lerp(distancia_gota[i], distancia_gota[i + 1], f.x), lerp(distancia_gota[i + int(M)], distancia_gota[i + int(M) + 1], f.x), f.y);
}
#endif

// La gota de un nodo, negativa dentro. La del vórtice envuelto (colores.a) es un anillo alrededor
// de su gota, a lo que asoma : no un círculo, sino la gota con su forma, aplastada contra un borde
// o deformada por un nodo que la atraviesa. Un círculo no la seguía : en una esquina la gota se
// lo comía, o se salía de él
float gota_nodo(float2 p, uint e) {
#ifdef ANILLO_GOTA
  if (colores[e].a > 0.0) return (lejos_de_la_gota((p - anillo.xy) / anillo.z) - anillo.w) * anillo.z;
#endif
  return length(p - nodos[e].xy) - forma.x * nodos[e].z + nodos[e].w;
}

float4 main(in Entrada i) : SV_TARGET {
  float2 p = i.Mundo;
  float d = 1e9;
  float3 col = float3(0.0, 0.0, 0.0);
  float h;
  uint nn = (uint)cuenta.y;
  // Solo los elementos de la celda del píxel (los demás están más lejos que
  // su radio más la fusión: no cambian la smin), en el orden de siempre:
  // nodos y luego enlaces
  uint cols = (uint)rejilla.w;
  uint filas = (uint)cuenta.w;
  int2 c = clamp(int2(floor((p - rejilla.xy) / rejilla.z)), int2(0, 0), int2(cols - 1u, filas - 1u));
  uint celda = (uint)c.y * cols + (uint)c.x;
  uint w = celdas[celda >> 2][celda & 3u];
  uint inicio = w & 0xFFFFu;
  uint fin = inicio + (w >> 16);
  for (uint k = inicio; k < fin; ++k) {
    uint doble = indices[k >> 3][(k >> 1) & 3u];
    uint e = (k & 1u) != 0u ? (doble >> 16) : (doble & 0xFFFFu);
    if (e < nn) {
      float r = nodos[e].z;
      d = smin(gota_nodo(p, e), d, forma.z * r, h);
      col = lerp(col, colores[e].rgb, h);
    } else {
      uint q = e - nn;
      uint4 par = aristas[q >> 1];
      uint a = (q & 1u) != 0u ? par.z : par.x;
      uint b = (q & 1u) != 0u ? par.w : par.y;
      float2 pa = nodos[a].xy;
      float2 ab = nodos[b].xy - pa;
      float t = saturate(dot(p - pa, ab) / max(dot(ab, ab), 1e-6));
      float r = min(nodos[a].z, nodos[b].z);
      d = smin(length(p - pa - ab * t) - forma.y * r, d, forma.z * r, h);
      col = lerp(col, lerp(colores[a].rgb, colores[b].rgb, t), h);
    }
  }
  // un píxel de suavizado en el borde. La distancia se recorta a 4 píxeles
  // antes de derivarla: entre una celda vacía (d = 1e9) y otra con
  // elementos, fwidth(d) se dispararía y dejaría la cobertura al 50% (una
  // raya en el borde de la celda). Donde el líquido se ve, d y sus vecinos
  // están por debajo de ~3 píxeles: el recorte no cambia nada
  float pixel = max(fwidth(p.x), fwidth(p.y));
  float dc = min(d, 4.0 * pixel);
  float cobertura = saturate(0.5 - dc / max(fwidth(dc), 1e-6));
  return float4(col * (1.0 - cuenta.x), forma.w * cobertura);
})" ;

}  // namespace

// Pipeline y cbuffer del líquido, en un dispositivo

struct PasadaLiquido :: Pipelines {

	IRenderDevice * device = nullptr ;

	RefCntAutoPtr<IPipelineState> pipeline ;

	RefCntAutoPtr<IShaderResourceBinding> binding ;

	RefCntAutoPtr<IBuffer> uniforme ;

	// `con_gota` : con el anillo de la gota envuelta, que lee su distancia

	bool crear(const Gpu & gpu , IBuffer * camara , bool con_gota , std :: string & error) {

		device = gpu.device ;

		ShaderCreateInfo shader ;

		shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL ;

		shader.Desc.UseCombinedTextureSamplers = true ;

		shader.EntryPoint = "main" ;

		RefCntAutoPtr<IShader> vs , ps ;

		shader.Desc.Name = "Nodos liquido VS" ;

		shader.Desc.ShaderType = SHADER_TYPE_VERTEX ;

		shader.Source = liquido_vs ;

		gpu.device->CreateShader(shader , &vs) ;

		shader.Desc.Name = "Nodos liquido PS" ;

		shader.Desc.ShaderType = SHADER_TYPE_PIXEL ;

		const std :: string ps_fuente = con_gota ? "#define ANILLO_GOTA\n#define M " + std :: to_string(lado_canal) + "u\n#define VISTA " + numero_hlsl(desc_gota().vista) + "\n" + liquido_ps : std :: string(liquido_ps) ;

		shader.Source = ps_fuente.c_str() ;

		gpu.device->CreateShader(shader , &ps) ;

		if (!vs || !ps) { return gpu_fallo(error , "No se pudieron compilar los shaders del liquido") ; }

		BufferDesc desc ;

		desc.Name = "Nodos liquido uniforms" ;

		desc.Size = sizeof(LiquidoGpu) ;

		desc.Usage = USAGE_DEFAULT ;

		desc.BindFlags = BIND_UNIFORM_BUFFER ;

		desc.ImmediateContextMask = gpu.mascara ;

		gpu.device->CreateBuffer(desc , nullptr , &uniforme) ;

		if (!uniforme) { return gpu_fallo(error , "No se pudo crear el uniform del liquido") ; }

		GraphicsPipelineStateCreateInfo pso = gpu_pso_mezcla("Nodos liquido") ;

		// con la gota, su distancia se ata después (cambia si la gota se rehace)

		const ShaderResourceVariableDesc fijas[] = {

			{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL , "Camara" , SHADER_RESOURCE_VARIABLE_TYPE_STATIC } ,

			{ SHADER_TYPE_PIXEL , "Liquido" , SHADER_RESOURCE_VARIABLE_TYPE_STATIC }

		} ;

		if (con_gota) {

			pso.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC ;

			pso.PSODesc.ResourceLayout.Variables = fijas ;

			pso.PSODesc.ResourceLayout.NumVariables = 2 ;

		}

		pso.pVS = vs ;

		pso.pPS = ps ;

		pso.PSODesc.ImmediateContextMask = gpu.mascara ;

		gpu.device->CreateGraphicsPipelineState(pso , &pipeline) ;

		if (!pipeline) { return gpu_fallo(error , "No se pudo crear el pipeline del liquido") ; }

		gpu_fijar_variable(pipeline , "Camara" , camara) ;

		gpu_fijar_variable(pipeline , "Liquido" , uniforme) ;

		pipeline->CreateShaderResourceBinding(&binding , true) ;

		return true ;

	}

} ;

PasadaLiquido :: PasadaLiquido(bool con_rejilla , const DistanciaGota * gota) : con_rejilla_(con_rejilla) , gota_(gota) {}

PasadaLiquido :: ~PasadaLiquido() = default ;

bool PasadaLiquido :: preparar(const Gpu & gpu , const Fotograma & f , CamaraGpu & camara , std :: string & error) {

	if (!pipes_ || pipes_->device != gpu.device) {

		auto nuevos = std :: make_unique<Pipelines>() ;

		if (!nuevos->crear(gpu , f.uniforme_camara , gota_ != nullptr , error)) { return false ; }

		pipes_ = std :: move(nuevos) ;

		atada_ = nullptr ;

	}

	const rust :: Slice<const Nodo> nodos(f.nodos->data() , f.nodos->size()) ;

	const rust :: Slice<const std :: uint32_t> aristas(f.aristas->data() , f.aristas->size()) ;

	hay_ = f.escena.cuenta_nodos && rellenar_liquido(f.escena , nodos , aristas , liquido_ , camara , con_rejilla_) ;

	return true ;

}

void PasadaLiquido :: subir(IDeviceContext * ctx , const Fotograma &) {

	if (!hay_) { return ; }

	ctx->UpdateBuffer(pipes_->uniforme , 0 , sizeof(liquido_) , &liquido_ , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	// la distancia de la gota, si cambió (la gota se crea al preparar, después que esta)

	IBuffer * distancia = gota_ ? gota_->distancia_gota() : nullptr ;

	if (distancia && distancia != atada_) {

		if (auto * v = pipes_->binding->GetVariableByName(SHADER_TYPE_PIXEL , "distancia_gota")) { v->Set(gpu_lectura(distancia)) ; }

		atada_ = distancia ;

	}

}

void PasadaLiquido :: dibujar(IDeviceContext * ctx , const Fotograma &) {

	if (!hay_) { return ; }

	// con gota, hasta que su distancia esté atada (el primer fotograma)

	if (gota_ && !atada_) { return ; }

	ctx->SetPipelineState(pipes_->pipeline) ;

	ctx->CommitShaderResources(pipes_->binding , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	DrawAttribs draw ;

	draw.NumVertices = 6 ;

	ctx->Draw(draw) ;

}

}  // namespace shd :: nodos
