# include "gota_pintura.h"

# include "Shader.h"

# include "computo.h"

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
// calor, por bandas como en el vórtice

constexpr const char * pintar_ps = R"(
StructuredBuffer<uint> campo;
StructuredBuffer<uint> canal_masc;
StructuredBuffer<float2> canal_alc;
StructuredBuffer<float> canal_cer;
MUESTRA_C(cer_en, canal_cer, float)
StructuredBuffer<float> canal_cal;

struct Entrada { float4 Position : SV_POSITION; float2 Mundo : TEXCOORD0; };

static const float3 GAMA[8] = {
  float3(0.03, 0.07, 0.25), float3(0.07, 0.22, 0.72), float3(0.12, 0.56, 0.91), float3(0.20, 0.82, 0.94),
  float3(0.60, 0.92, 0.55), float3(0.95, 0.93, 0.16), float3(0.96, 0.60, 0.09), float3(0.89, 0.19, 0.10)
};

float3 gama(float t) {
  float x = saturate(t) * 7.0;
  int k = min(int(x), 6);
  return lerp(GAMA[k], GAMA[k + 1], x - float(k));
}

float2 en(int2 c) {
  if (any(c < 0) || any(c >= int(G))) return float2(0.0, 0.0);
  uint k = (uint(c.y) * G + uint(c.x)) * 2u;
  return float2(campo[k], campo[k + 1u]);
}

// el campo en un punto (en celdas), entre las cuatro celdas que lo rodean
float2 campo_en(float2 g) {
  float2 q = g - 0.5;
  int2 a = int2(floor(q));
  float2 f = q - float2(a);
  return lerp(lerp(en(a), en(a + int2(1, 0)), f.x), lerp(en(a + int2(0, 1)), en(a + int2(1, 1)), f.x), f.y);
}

float2 celda_de(float2 r) { return (r + VISTA) / (2.0 * VISTA) * float(G); }

// El alcance y el calor de los canales con las celdas de líquido de una vecindad de 4 × 4,
// pesadas por la distancia : con solo las cuatro de alrededor, el borde del canal seguía la
// rejilla, en escalera, en vez de la forma del enlace
float3 canal_en(float2 g) {
  g = clamp(g, 1.0, float(M) - 2.001);
  int2 a = int2(floor(g));
  float3 s = float3(0.0, 0.0, 0.0);
  float sw = 0.0;
  [unroll] for (int y = -1; y <= 2; ++y) {
    [unroll] for (int x = -1; x <= 2; ++x) {
      int i = ic(a + int2(x, y));
      if (canal_masc[i] == 0u) continue;
      float2 e = g - float2(a + int2(x, y));
      float w = exp(-dot(e, e) * 1.5);
      s += float3(canal_alc[i], canal_cal[i]) * w;
      sw += w;
    }
  }
  return sw > 1e-5 ? s / sw : float3(1e3, 0.0, 0.0);
}

// Lo que hay de líquido alrededor, a unas celdas : un grupito de partículas suelto, o un
// bulto del borde, es más pequeño que eso, y así no se dibuja como una cuenta aparte
float masa_grande(float2 g) {
  float s = 0.0;
  [unroll] for (int k = 0; k < 8; ++k) {
    float a = float(k) * 0.7853982;
    s += campo_en(g + float2(cos(a), sin(a)) * GRUMO).x;
  }
  return s / 8.0;
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
  // celdas por radio, y las partículas por celda
  float celdas = float(G) / (2.0 * VISTA);
  float lleno = float(N) / PI / (celdas * celdas) * PI * float(ALCANCE * ALCANCE) / 3.0 * 256.0;
  float umbral = 0.4 * lleno;
  float aa = max(fwidth(v.x), 1.0);
  float gota = smoothstep(umbral - aa, umbral + aa, v.x);
  // donde no llega la gota, lo de alrededor no cambia nada (0 por lo que sea es 0)
  if (gota > 0.0) gota *= smoothstep(0.12 * lleno, 0.2 * lleno, masa_grande(celda_de(p)));
  // y el líquido de los canales, con sus paredes nítidas : la forma elástica de los
  // enlaces, y sin cubrir los nodos
  float2 gc = (p + VISTA) / CELDA_C - 0.5;
  float pixel = max(fwidth(p.x), fwidth(p.y));
  // dentro de la forma y hasta donde llega, con el frente redondeado como un menisco
  float3 cc = canal_en(gc);
  float2 alc = cc.xy;
  // sin líquido alrededor (alcance de 1e3), el frente queda a más de 998 radios : el canal es
  // 0 exacto, y ni la silueta de los enlaces ni los nodos hacen falta. Es casi toda la vista
  float canal = 0.0;
  if (alc.x < 999.0) {
    float pared = embudo(p, cer_en(gc));
    float forma = -smin_tubo(LLEGA - alc.x, -pared, FRENTE);
    canal = 1.0 - smoothstep(-pixel, pixel, forma);
    for (uint o = 0u; o < uint(inercia.w); ++o) canal *= smoothstep(0.0, 1.5 * pixel, length(p - obstaculos[o].xy) - obstaculos[o].z);
  }
  float dentro = max(gota, canal);
  // solo el líquido : lo que no es líquido no se pinta
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

	RefCntAutoPtr<IShader> ps = gpu_compilar(gpu , { SHADER_TYPE_PIXEL , "Nodos gota PS" , hlsl + pintar_ps }) ;

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
