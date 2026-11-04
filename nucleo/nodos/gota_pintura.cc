# include "gota_pintura.h"

# include "Shader.h"

# include "computo.h"

# include "gota_liquido_hlsl.h"

# include "pipeline_comun.h"

using namespace Diligent ;

namespace shd :: nodos {

namespace {

constexpr const char * pintar_vs = R"(
cbuffer Camara { float4 transform; float4 caja; float4 extra; };
struct Salida { float4 Position : SV_POSITION; float2 Mundo : TEXCOORD0; };
void main(in uint vid : SV_VertexID, out Salida o) {
  float2 esquinas[6] = { float2(-1,-1), float2(1,-1), float2(1,1),
                         float2(-1,-1), float2(1,1), float2(-1,1) };
  float2 w = centro.xy + esquinas[vid] * centro.z * VISTA;
  float2 ndc = w * transform.xy + transform.zw;
  o.Position = float4(ndc.x, -ndc.y, 0.0, 1.0);
  o.Mundo = w;
})" ;

// La masa de una partícula en el campo, si estuvieran repartidas en el disco : el
// contorno va bastante por debajo, para que un hueco entre partículas no se vea. El
// calor, por bandas como en el vórtice. Dónde hay líquido, en gota_liquido_hlsl.h

constexpr const char * pintar_ps = R"(
StructuredBuffer<uint> unido;

struct Entrada { float4 Position : SV_POSITION; float2 Mundo : TEXCOORD0; };

// Lo unido al cuerpo de la gota en un punto (SueltosGota) : entre las celdas de líquido de una
// vecindad de 4 × 4, pesadas por la distancia, cuánto son del cuerpo. En el cuerpo es 1, en un
// trozo suelto 0, y donde no hay ninguna también 0 : lo que queda más cerca de un trozo que del
// cuerpo no se pinta. Los cuatro bits de las columnas x0 a x0 + 3 de la fila y, en la mitad
// `mitad` (0 lo unido, 1 el líquido); fuera de la rejilla, 0
uint cuatro_bits(int x0, int y, uint mitad) {
  uint base = mitad * M * (M / 32u) + uint(clamp(y, 0, int(M) - 1)) * (M / 32u);
  uint s = 0u;
  [unroll] for (int x = 0; x < 4; ++x) {
    int c = x0 + x;
    uint q = uint(clamp(c, 0, int(M) - 1));
    uint b = (unido[base + q / 32u] >> (q & 31u)) & 1u;
    s |= (c >= 0 && c < int(M) ? b : 0u) << uint(x);
  }
  return y >= 0 && y < int(M) ? s : 0u;
}
float unido_en(float2 g) {
  int2 a = int2(floor(g));
  // la vecindad en 16 bits : dónde hay líquido y qué es del cuerpo
  uint liq = 0u, cuerpo = 0u;
  [unroll] for (int y = 0; y < 4; ++y) {
    liq |= cuatro_bits(a.x - 1, a.y - 1 + y, 1u) << uint(4 * y);
    cuerpo |= cuatro_bits(a.x - 1, a.y - 1 + y, 0u) << uint(4 * y);
  }
  // Casi siempre : no hay líquido, o todo el de alrededor es del cuerpo y alguno está fuera de
  // las cuatro esquinas (cada celda de fuera pesa al menos 5,5e-4 : pasa del tope de 1e-5 de la
  // cuenta entera). Da lo mismo que la cuenta entera; medido (banco del visor, arrastre a
  // 1280 × 800) : el pintado, de 0,172 a 0,129 ms
  const uint ESQUINAS = 0x9009u;
  if (liq == 0u) return 0.0;
  if ((liq & ~cuerpo) == 0u && (liq & ~ESQUINAS) != 0u) return 1.0;
  float s = 0.0;
  float sw = 0.0;
  [unroll] for (int k = 0; k < 16; ++k) {
    if (((liq >> uint(k)) & 1u) == 0u) continue;
    float2 e = g - float2(a + int2(k % 4 - 1, k / 4 - 1));
    float w = exp(-dot(e, e) * 1.5);
    s += w * float((cuerpo >> uint(k)) & 1u);
    sw += w;
  }
  return sw > 1e-5 ? s / sw : 0.0;
}

static const float3 GAMA[8] = {
  float3(0.03, 0.07, 0.25), float3(0.07, 0.22, 0.72), float3(0.12, 0.56, 0.91), float3(0.20, 0.82, 0.94),
  float3(0.60, 0.92, 0.55), float3(0.95, 0.93, 0.16), float3(0.96, 0.60, 0.09), float3(0.89, 0.19, 0.10)
};

float3 gama(float t) {
  float x = saturate(t) * 7.0;
  int k = min(int(x), 6);
  return lerp(GAMA[k], GAMA[k + 1], x - float(k));
}

// lo azul que es un calor : de 1 en los azules a 0 desde el cian
float lo_azul(float t) { return 1.0 - smoothstep(0.30, 0.40, t); }

// por bandas, como el vórtice ; en los azules, cada una pasa a la siguiente poco a poco : de
// golpe, el salto del azul oscuro al claro se veía brusco
float3 bandas(float t) {
  float azul = lo_azul(t);
  float x = t * 9.0;
  float medio = lerp(0.1, 0.35, azul);
  float escalon = (floor(x) + smoothstep(0.5 - medio, 0.5 + medio, frac(x))) / 9.0;
  return gama(lerp(t, escalon, lerp(0.6, 0.3, azul)));
}

float4 main(in Entrada i) : SV_TARGET {
  float2 p = (i.Mundo - centro.xy) / centro.z;
  float2 v = campo_en(celda_de(p));
  float lleno = lleno_campo();
  float gota = forma_gota(p, v, max(fwidth(v.x), 1.0));
  // y el líquido de los canales
  float2 gc = celda_canal(p);
  float3 cc = canal_en(gc);
  float2 alc = cc.xy;
  float canal = forma_canal(p, gc, alc.x, max(fwidth(p.x), fwidth(p.y)));
  // solo el líquido : lo que no es líquido no se pinta
  float dentro = max(gota, canal);
  if (dentro <= 0.0) discard;
  // y sin los trozos sueltos : ni un grumo
  dentro *= smoothstep(0.25, 0.75, unido_en(gc));
  if (dentro <= 0.0) discard;
  // en el canal, el color con que salió de la boca, que se enfría al alejarse, removido por
  // el flujo de los canales
  float t_canal = lerp(alc.y * (1.0 - 0.35 * saturate(alc.x / LLEGA)), cc.z, 0.3);
  // donde la gota y el canal se juntan, los colores se funden poco a poco : un cambio
  // brusco dibujaría el contorno de la gota encima del canal
  float hondo = saturate((v.x / lleno - 0.4) / 0.9);
  float t_gota = v.y / max(v.x, 1.0);
  float t = lerp(t_gota, lerp(t_canal, t_gota, hondo), canal);
  float x = t * 9.0;
  float r = frac(x);
  float3 col = bandas(t);
  float ancho = max(fwidth(x), 1e-4);
  col *= 1.0 - lerp(0.2, 0.05, lo_azul(t)) * (1.0 - smoothstep(0.0, 1.5 * ancho, abs(r - 0.5)));
  return float4(saturate(col * 1.25), dentro);
})" ;

}  // namespace

bool PinturaGota :: crear(

	const Gpu & gpu ,

	IBuffer * camara ,

	IBuffer * uniforme ,

	const std :: string & hlsl ,

	const Lecturas & lecturas ,

	std :: string & error

){

	RefCntAutoPtr<IShader> vs = gpu_compilar(gpu , { SHADER_TYPE_VERTEX , "Nodos gota VS" , hlsl + pintar_vs }) ;

	RefCntAutoPtr<IShader> ps = gpu_compilar(gpu , { SHADER_TYPE_PIXEL , "Nodos gota PS" , hlsl + liquido_gota_hlsl + pintar_ps }) ;

	if (!vs || !ps) { return gpu_fallo(error , "No se pudieron compilar los shaders de la gota") ; }

	GraphicsPipelineStateCreateInfo pso = gpu_pso_mezcla("Nodos gota") ;

	const ShaderResourceVariableDesc fijas[] = {

		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL , "Camara" , SHADER_RESOURCE_VARIABLE_TYPE_STATIC } ,

		{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL , "Gota" , SHADER_RESOURCE_VARIABLE_TYPE_STATIC }

	} ;

	// sin los vecinos : aquí no hacen falta, y son recursos sin atar

	pso.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC ;

	pso.PSODesc.ResourceLayout.Variables = fijas ;

	pso.PSODesc.ResourceLayout.NumVariables = 2 ;

	pso.pVS = vs ;

	pso.pPS = ps ;

	pso.PSODesc.ImmediateContextMask = gpu.mascara ;

	gpu.device->CreateGraphicsPipelineState(pso , &pipeline_) ;

	if (!pipeline_) { return gpu_fallo(error , "No se pudo crear el pipeline de la gota") ; }

	gpu_fijar_variable(pipeline_ , "Camara" , camara) ;

	gpu_fijar_variable(pipeline_ , "Gota" , uniforme) ;

	pipeline_->CreateShaderResourceBinding(&binding_ , true) ;

	const auto atar = [&](const char * nombre , IBuffer * b) {

		if (auto * v = binding_->GetVariableByName(SHADER_TYPE_PIXEL , nombre)) { v->Set(gpu_lectura(b)) ; }

	} ;

	atar("campo" , lecturas.campo) ;

	atar("canal_alc" , lecturas.alcance) ;

	atar("canal_cer" , lecturas.cerca) ;

	atar("canal_masc" , lecturas.mascara) ;

	atar("canal_cal" , lecturas.calor) ;

	atar("unido" , lecturas.unido) ;

	return true ;

}

void PinturaGota :: dibujar(IDeviceContext * ctx) {

	ctx->SetPipelineState(pipeline_) ;

	ctx->CommitShaderResources(binding_ , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	DrawAttribs draw ;

	draw.NumVertices = 6 ;

	ctx->Draw(draw) ;

}

}  // namespace shd :: nodos
