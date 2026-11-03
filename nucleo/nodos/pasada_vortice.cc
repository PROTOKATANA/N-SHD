# include "pasada_vortice.h"

# include <cstdio>

# include <string>

# include <utility>

# include <vector>

# include "computo.h"

# include "copia_cpu.h"

# include "gpu.h"

# include "pipeline_comun.h"

using namespace Diligent ;

namespace shd :: nodos {

namespace {

// El forzamiento del vórtice (el contrato está en fluido_gpu.h) : el remolino
// que tira del fluido, la turbulencia que nunca para, la agitación al pulsar,
// el fluido que entra frío por el borde (en brazos fríos y calientes) y sale
// por el ojo, y el calor que coge al caer

constexpr const char * forzamiento_hlsl = R"(
cbuffer Vortice {
  float4 centro;   // x, y (mundo), radio que se ve (mundo), tiempo (s)
  float4 brazos;   // cuántos, fase, brillo, semilla de la última pulsación
  float4 agitar;   // segundos desde la pulsación, duración, aceleración de los chorros, turbulencia
  float4 color;    // rgba del nodo
  float4 giro;     // velocidad, núcleo, arrastre, calentar
  float4 fuentes;  // por celda: entrada en el borde, salida en el ojo; q del borde, q del ojo
};

float2 tangente(float2 d) { return float2(-d.y, d.x) / max(length(d), 1e-3); }

// La velocidad del remolino a distancia r (celdas): crece en el núcleo y
// luego cae (un vórtice de Rankine suavizado)
float giro_en(float r) {
  float rho = r / RADIO;
  return giro.x * (rho < giro.y ? rho / giro.y : pow(giro.y / rho, 0.7));
}

// La temperatura con que entra el fluido por el borde: fría o caliente
// según el brazo (las corrientes que luego se jaspean)
float entrada(float angulo) {
  float brazo = 0.5 + 0.5 * cos(brazos.x * (angulo - brazos.y));
  return lerp(0.05, 0.8, brazo * brazo);
}

// Al arrancar: el remolino ya girando y la temperatura ya repartida (fría en
// el borde, caliente hacia el ojo, en espiral)
float2 velocidad_inicial(int2 c) {
  float2 d = float2(c) - MEDIO;
  float r = length(d);
  return tangente(d) * giro_en(r);
}

float escalar_inicial(int2 c) {
  float2 d = float2(c) - MEDIO;
  float r = length(d);
  float rho = r / RADIO;
  float espiral = atan2(d.y, d.x) - 2.5 * log(1.0 / max(rho, 0.05));
  return saturate(entrada(espiral) + 1.1 * pow(1.0 - rho, 2.5));
}

float2 forzar(int2 c, float2 v) {
  float dt = fluido.x;
  float2 g = float2(c);
  float2 d = g - MEDIO;
  float r = length(d);
  float2 tg = tangente(d);
  // el remolino tira del fluido hacia su giro; tras pulsar deja de tirar y
  // vuelve poco a poco en la segunda mitad de la agitación
  float tirar = smoothstep(0.5 * agitar.y, agitar.y, agitar.x);
  v += tg * (giro_en(r) - dot(v, tg)) * (1.0 - exp(-giro.z * dt * tirar));
  // una agitación leve en el borde siembra remolinos (sin ella, el flujo
  // sería liso): cada celda, un empujón al azar que cambia 8 veces por segundo
  if (q_de(c) > fuentes.z) {
    float h = azar(uint(indice(c)) * 977u + uint(centro.w * 8.0) * 7919u);
    v += float2(cos(h * TAU), sin(h * TAU)) * 30.0 * dt;
  }
  // la turbulencia que nunca para: tres chorros vivos a la vez, cada uno en
  // un sitio y una dirección al azar, que nacen y se apagan en 0,6 s y se
  // relevan desfasados
  for (uint j = 0u; j < 3u; ++j) {
    float fase = centro.w / 0.6 + float(j) / 3.0;
    uint s = uint(fase) * 97u + j * 7919u + 12345u;
    float a = azar(s) * TAU;
    float2 p = MEDIO + sqrt(azar(s + 1u)) * RADIO * 0.85 * float2(cos(a), sin(a));
    float b = azar(s + 2u) * TAU;
    float2 e = g - p;
    float vida = sin(frac(fase) * 3.1415927);
    v += float2(cos(b), sin(b)) * agitar.w * vida * dt * exp(-dot(e, e) / (0.02 * RADIO * RADIO));
  }
  // la agitación al pulsar: siete chorros al azar durante 0,3 s
  if (agitar.x < 0.3) {
    uint semilla = uint(brazos.w) * 64u;
    for (uint k = 0u; k < 7u; ++k) {
      float a = azar(semilla + k * 4u) * TAU;
      float2 p = MEDIO + sqrt(azar(semilla + k * 4u + 1u)) * RADIO * 0.8 * float2(cos(a), sin(a));
      float b = azar(semilla + k * 4u + 2u) * TAU;
      float2 e = g - p;
      v += float2(cos(b), sin(b)) * agitar.z * dt * exp(-dot(e, e) / (0.02 * RADIO * RADIO));
    }
  }
  return v;
}

// La temperatura: entra por el borde (fría o caliente según el brazo) y se
// calienta al caer hacia el ojo
float transportar(int2 c, float t) {
  float dt = fluido.x;
  float2 d = float2(c) - MEDIO;
  float r = length(d);
  float rho = r / RADIO;
  if (q_de(c) > fuentes.z) t = lerp(t, entrada(atan2(d.y, d.x)), 1.0 - exp(-4.0 * dt));
  t += giro.w * dt * (1.0 - rho) * (1.0 - rho);
  return t;
}

float acotar(float t) { return saturate(t); }

// Entra por la franja del borde y sale por el ojo
float fuente(int2 c) {
  float q = q_de(c);
  return q > fuentes.z ? fuentes.x : (q <= fuentes.w ? fuentes.y : 0.0);
}
)" ;

// Pintar : un quad sobre el círculo; cada píxel lee la temperatura y la
// pinta con la gama de V3, por bandas como la ilustración

constexpr const char * pintar_vs = R"(
cbuffer Camara { float4 transform; float4 caja; float4 extra; };
struct Salida { float4 Position : SV_POSITION; float2 Mundo : TEXCOORD0; };
void main(in uint vid : SV_VertexID, out Salida o) {
  float2 esquinas[6] = { float2(-1,-1), float2(1,-1), float2(1,1),
                         float2(-1,-1), float2(1,1), float2(-1,1) };
  float2 w = centro.xy + esquinas[vid] * centro.z;
  float2 ndc = w * transform.xy + transform.zw;
  o.Position = float4(ndc.x, -ndc.y, 0.0, 1.0);
  o.Mundo = w;
})" ;

constexpr const char * pintar_ps = R"(
StructuredBuffer<float> calor;
MUESTREADOR(calor_en, calor, float)
struct Entrada { float4 Position : SV_POSITION; float2 Mundo : TEXCOORD0; };

// La gama de calor de V3: del azul marino (frío) al rojo
static const float3 GAMA[8] = {
  float3(0.03, 0.07, 0.25), float3(0.07, 0.22, 0.72), float3(0.12, 0.56, 0.91), float3(0.20, 0.82, 0.94),
  float3(0.60, 0.92, 0.55), float3(0.95, 0.93, 0.16), float3(0.96, 0.60, 0.09), float3(0.89, 0.19, 0.10)
};

float3 gama(float t) {
  float x = saturate(t) * 7.0;
  int k = min(int(x), 6);
  return lerp(GAMA[k], GAMA[k + 1], x - float(k));
}

float4 main(in Entrada i) : SV_TARGET {
  float2 d = (i.Mundo - centro.xy) / centro.z;
  float rho = length(d);
  float aa = max(fwidth(rho), 1e-4);
  float dentro = 1.0 - smoothstep(1.0 - aa, 1.0, rho);
  float2 g = MEDIO + d * RADIO;
  float t = calor_en(g);
  // por bandas, como la ilustración: la temperatura sube a escalones
  // suaves, con un filo oscuro entre uno y otro
  float x = t * 9.0;
  float f = frac(x);
  float escalon = (floor(x) + smoothstep(0.4, 0.6, f)) / 9.0;
  float3 col = gama(lerp(t, escalon, 0.6));
  float ancho = max(fwidth(x), 1e-4);
  col *= 1.0 - 0.2 * (1.0 - smoothstep(0.0, 1.5 * ancho, abs(f - 0.5)));
  return float4(saturate(col * brazos.z), dentro);
})" ;

}  // namespace

struct PasadaVortice :: Recursos {

	Gpu gpu ;

	RefCntAutoPtr<IBuffer> uniforme ;

	std :: unique_ptr<Fluido> fluido ;

	RefCntAutoPtr<IPipelineState> pintar ;

	RefCntAutoPtr<IShaderResourceBinding> binding ;

	CopiaCpu copia ;

	bool crear(const Gpu & prestada , IBuffer * camara , std :: string & error) {

		gpu = prestada ;

		BufferDesc desc ;

		desc.Name = "Nodos vortice uniforms" ;

		desc.Size = sizeof(VorticeGpu) ;

		desc.Usage = USAGE_DEFAULT ;

		desc.BindFlags = BIND_UNIFORM_BUFFER ;

		desc.ImmediateContextMask = gpu.mascara ;

		gpu.device->CreateBuffer(desc , nullptr , &uniforme) ;

		if (!uniforme) { return gpu_fallo(error , "No se pudo crear el uniform del vortice") ; }

		return fluido->crear(gpu , uniforme , error) && crear_pintar(camara , error) ;

	}

	bool crear_pintar(IBuffer * camara , std :: string & error) {

		const std :: string base = fluido_hlsl(forma_fluido_vortice().celdas) + forzamiento_hlsl ;

		RefCntAutoPtr<IShader> vs = gpu_compilar(gpu , { SHADER_TYPE_VERTEX , "Nodos vortice VS" , base + pintar_vs }) ;

		RefCntAutoPtr<IShader> ps = gpu_compilar(gpu , { SHADER_TYPE_PIXEL , "Nodos vortice PS" , base + pintar_ps }) ;

		if (!vs || !ps) { return gpu_fallo(error , "No se pudieron compilar los shaders del vortice") ; }

		GraphicsPipelineStateCreateInfo pso = gpu_pso_mezcla("Nodos vortice") ;

		const ShaderResourceVariableDesc fijas[] = {

			{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL , "Camara" , SHADER_RESOURCE_VARIABLE_TYPE_STATIC } ,

			{ SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL , "Vortice" , SHADER_RESOURCE_VARIABLE_TYPE_STATIC }

		} ;

		pso.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC ;

		pso.PSODesc.ResourceLayout.Variables = fijas ;

		pso.PSODesc.ResourceLayout.NumVariables = 2 ;

		pso.pVS = vs ;

		pso.pPS = ps ;

		pso.PSODesc.ImmediateContextMask = gpu.mascara ;

		gpu.device->CreateGraphicsPipelineState(pso , &pintar) ;

		if (!pintar) { return gpu_fallo(error , "No se pudo crear el pipeline del vortice") ; }

		gpu_fijar_variable(pintar , "Camara" , camara) ;

		gpu_fijar_variable(pintar , "Vortice" , uniforme) ;

		pintar->CreateShaderResourceBinding(&binding , true) ;

		return true ;

	}

} ;

PasadaVortice :: PasadaVortice(Reloj reloj , FabricaFluido fabrica , bool diagnostico) :

	reloj_(std :: move(reloj)) ,

	fabrica_(std :: move(fabrica)) ,

	estado_(estado_vortice()) ,

	diagnostico_(diagnostico) {}

PasadaVortice :: ~PasadaVortice() = default ;

bool PasadaVortice :: preparar(const Gpu & gpu , const Fotograma & f , CamaraGpu & , std :: string & error) {

	const rust :: Slice<const Nodo> nodos(f.nodos->data() , f.nodos->size()) ;

	hay_ = f.escena.vortice && estado_->rellenar(f.escena , nodos , reloj_() , fotograma_) ;

	if (!hay_) { return true ; }

	if (!recursos_ || recursos_->gpu.device != gpu.device) {

		const FormaFluido forma = forma_fluido_vortice() ;

		recursos_ = std :: make_unique<Recursos>() ;

		recursos_->fluido = fabrica_({ forma , forzamiento_hlsl , "Vortice" }) ;

		recursos_->fluido->medir(banco_ , primera_) ;

		if (!recursos_->crear(gpu , f.uniforme_camara , error)) {

			recursos_.reset() ;

			return hay_ = false ;

		}

	}

	if (diagnostico_) { leer_diagnostico(gpu) ; }

	return true ;

}

// La copia del paso anterior, si la hay : su huella, cuánto se aleja de un
// fluido incompresible, la velocidad máxima y si hay NaN

void PasadaVortice :: leer_diagnostico(const Gpu & gpu) {

	std :: vector<std :: vector<std :: uint8_t>> datos ;

	if (!recursos_->copia.leer(gpu.ctx , datos) || datos.size() != 2 || !gpu.aviso) { return ; }

	const std :: uint32_t celdas = forma_fluido_vortice().celdas ;

	const auto * vel = reinterpret_cast<const float *>(datos[0].data()) ;

	const auto * escalar = reinterpret_cast<const float *>(datos[1].data()) ;

	const DiagnosticoFluido d = diagnostico_vortice(

		rust :: Slice<const float>(vel , datos[0].size() / sizeof(float)) ,

		rust :: Slice<const float>(escalar , datos[1].size() / sizeof(float)) ,

		celdas

	) ;

	const std :: uint64_t inicial = 14695981039346656037ull ;

	const std :: uint64_t primera = huella(rust :: Slice<const std :: uint8_t>(datos[0].data() , datos[0].size()) , inicial) ;

	const std :: uint64_t h = huella(rust :: Slice<const std :: uint8_t>(datos[1].data() , datos[1].size()) , primera) ;

	char texto[200] ;

	std :: snprintf(

		texto ,

		sizeof(texto) ,

		"NODOS VORTICE: paso %llu, huella %016llx, divergencia max %.4f media %.5f, velocidad max %.1f celdas/s, calor %.2f-%.2f, NaN %u" ,

		static_cast<unsigned long long>(recursos_->fluido->pasos()) ,

		static_cast<unsigned long long>(h) ,

		d.divergencia_max ,

		d.divergencia_media ,

		d.velocidad_max ,

		d.escalar_min ,

		d.escalar_max ,

		d.nan

	) ;

	gpu.aviso(texto) ;

}

std :: vector<std :: string> PasadaVortice :: tramos() const {

	if (!recursos_ || !recursos_->fluido) { return {} ; }

	return recursos_->fluido->tramos() ;

}

void PasadaVortice :: medir(Medidor * banco , std :: size_t primera) {

	banco_ = banco ;

	primera_ = primera ;

	if (recursos_ && recursos_->fluido) { recursos_->fluido->medir(banco , primera) ; }

}

void PasadaVortice :: subir(IDeviceContext * ctx , const Fotograma &) {

	if (!hay_) { return ; }

	ctx->UpdateBuffer(recursos_->uniforme , 0 , sizeof(VorticeGpu) , &fotograma_.cbuffer , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	recursos_->fluido->avanzar(ctx , fotograma_.dt) ;

	// cada 120 pasos, una copia para el diagnóstico

	if (diagnostico_ && recursos_->fluido->pasos() % 120 == 0) {

		recursos_->copia.copiar(recursos_->gpu , ctx , { recursos_->fluido->velocidad() , recursos_->fluido->escalar() }) ;

	}

}

void PasadaVortice :: dibujar(IDeviceContext * ctx , const Fotograma &) {

	if (!hay_) { return ; }

	if (auto * v = recursos_->binding->GetVariableByName(SHADER_TYPE_PIXEL , "calor")) { v->Set(gpu_lectura(recursos_->fluido->escalar())) ; }

	ctx->SetPipelineState(recursos_->pintar) ;

	ctx->CommitShaderResources(recursos_->binding , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	DrawAttribs draw ;

	draw.NumVertices = 6 ;

	ctx->Draw(draw) ;

}

}  // namespace shd :: nodos
