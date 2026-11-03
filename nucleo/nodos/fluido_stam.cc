# include "fluido_stam.h"

# include <utility>

# include "computo.h"

using namespace Diligent ;

namespace shd :: nodos {

namespace {

// Al arrancar : lo que diga el efecto

constexpr const char * iniciar_hlsl = R"(
RWStructuredBuffer<float2> vel_sal;
RWStructuredBuffer<float> esc_sal;
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  int i = indice(c);
  if (!es_fluido(c)) { vel_sal[i] = float2(0.0, 0.0); esc_sal[i] = 0.0; return; }
  vel_sal[i] = velocidad_inicial(c);
  esc_sal[i] = escalar_inicial(c);
})" ;

// Advección semilagrangiana (de dónde viene cada celda) de la velocidad y el
// escalar, con las fuerzas y las fuentes del efecto

constexpr const char * advectar_hlsl = R"(
StructuredBuffer<float2> vel_ent;
StructuredBuffer<float> esc_ent;
RWStructuredBuffer<float2> vel_sal;
RWStructuredBuffer<float> esc_sal;
MUESTREADOR(vel_en, vel_ent, float2)
MUESTREADOR(esc_en, esc_ent, float)
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  int i = indice(c);
  if (!es_fluido(c)) { vel_sal[i] = float2(0.0, 0.0); esc_sal[i] = 0.0; return; }
  float2 atras = float2(c) - fluido.x * vel_ent[i];
  float2 v = forzar(c, vel_en(atras));
  float t = transportar(c, esc_en(atras));
  if (any(isnan(v)) || isnan(t)) { v = float2(0.0, 0.0); t = 0.0; }
  vel_sal[i] = v;
  esc_sal[i] = acotar(t);
})" ;

// El rotacional (escalar en 2D) de la velocidad; la pared, quieta

constexpr const char * rotacional_hlsl = R"(
StructuredBuffer<float2> vel_ent;
RWStructuredBuffer<float> rot_sal;
float2 v_en(int2 c) { return es_fluido(c) ? vel_ent[indice(c)] : float2(0.0, 0.0); }
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  int i = indice(c);
  if (!es_fluido(c)) { rot_sal[i] = 0.0; return; }
  rot_sal[i] = 0.5 * ((v_en(c + DX).y - v_en(c - DX).y) - (v_en(c + DY).x - v_en(c - DY).x));
})" ;

// Vorticity confinement : refuerza los remolinos pequeños que la advección
// iría borrando (Fedkiw, Stam y Jensen)

constexpr const char * confinar_hlsl = R"(
StructuredBuffer<float2> vel_ent;
StructuredBuffer<float> rot_ent;
RWStructuredBuffer<float2> vel_sal;
float w_en(int2 c) { return es_fluido(c) ? abs(rot_ent[indice(c)]) : 0.0; }
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  int i = indice(c);
  if (!es_fluido(c)) { vel_sal[i] = float2(0.0, 0.0); return; }
  float2 grad = 0.5 * float2(w_en(c + DX) - w_en(c - DX), w_en(c + DY) - w_en(c - DY));
  float2 n = grad / (length(grad) + 1e-5);
  vel_sal[i] = vel_ent[i] + fluido.y * float2(n.y, -n.x) * rot_ent[i] * fluido.x;
})" ;

// La divergencia menos las fuentes del efecto (la presión deja el resto sin
// divergencia)

constexpr const char * divergencia_hlsl = R"(
StructuredBuffer<float2> vel_ent;
RWStructuredBuffer<float> div_sal;
float2 v_en(int2 c) { return es_fluido(c) ? vel_ent[indice(c)] : float2(0.0, 0.0); }
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  int i = indice(c);
  if (!es_fluido(c)) { div_sal[i] = 0.0; return; }
  float div = 0.5 * ((v_en(c + DX).x - v_en(c - DX).x) + (v_en(c + DY).y - v_en(c - DY).y));
  div_sal[i] = div - fuente(c);
})" ;

// Una iteración de Jacobi de la presión; en la pared, la de la celda (sin
// flujo a través)

constexpr const char * jacobi_hlsl = R"(
StructuredBuffer<float> pres_ent;
StructuredBuffer<float> div_ent;
RWStructuredBuffer<float> pres_sal;
float p_en(int2 c, float propia) { return es_fluido(c) ? pres_ent[indice(c)] : propia; }
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  int i = indice(c);
  if (!es_fluido(c)) { pres_sal[i] = 0.0; return; }
  float p = pres_ent[i];
  pres_sal[i] = (p_en(c + DX, p) + p_en(c - DX, p) + p_en(c + DY, p) + p_en(c - DY, p) - div_ent[i]) * 0.25;
})" ;

// Resta el gradiente de presión (queda incompresible), no deja atravesar la
// pared y guarda la presión, algo amortiguada, para empezar el siguiente paso
// (si no, una deriva constante se iría acumulando)

constexpr const char * proyectar_hlsl = R"(
StructuredBuffer<float2> vel_ent;
StructuredBuffer<float> pres_ent;
RWStructuredBuffer<float2> vel_sal;
RWStructuredBuffer<float> pres_sal;
float p_en(int2 c, float propia) { return es_fluido(c) ? pres_ent[indice(c)] : propia; }
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  int2 c = int2(id.xy);
  int i = indice(c);
  if (!es_fluido(c)) { vel_sal[i] = float2(0.0, 0.0); pres_sal[i] = 0.0; return; }
  float p = pres_ent[i];
  float2 v = vel_ent[i] - 0.5 * float2(p_en(c + DX, p) - p_en(c - DX, p), p_en(c + DY, p) - p_en(c - DY, p));
  bool pared = !es_fluido(c + DX) || !es_fluido(c - DX) || !es_fluido(c + DY) || !es_fluido(c - DY);
  if (pared) {
    float2 n = normalize(float2(c) - MEDIO);
    v -= max(dot(v, n), 0.0) * n;
  }
  vel_sal[i] = v;
  pres_sal[i] = 0.98 * p;
})" ;

// Los campos van por parejas (uno se lee y el otro se escribe, y se turnan).
// La velocidad y el escalar se turnan a la vez; la presión, en cada
// iteración de Jacobi

class FluidoStam final : public Fluido {

public:

	explicit FluidoStam(PedidoFluido pedido) : pedido_(std :: move(pedido)) {}

	bool crear(const Gpu & gpu , IBuffer * cbuffer_efecto , std :: string & error) override {

		gpu_ = gpu ;

		return crear_uniforme(error) && crear_kernels(cbuffer_efecto , error) && crear_campos(error) && atar() ;

	}

	void avanzar(IDeviceContext * ctx , float dt) override {

		subir_paso(ctx , dt) ;

		if (!iniciado_) {

			gpu_lanzar(ctx , iniciar_ , iniciar_b_) ;

			iniciado_ = true ;

		}

		{

			Tramo t(banco_ , primera_ , Etapa :: subir , gpu_.device , ctx) ;

			advectar_y_confinar(ctx) ;

		}

		{

			Tramo t(banco_ , primera_ + 1 , Etapa :: subir , gpu_.device , ctx) ;

			proyectar(ctx) ;

		}

		v_ = 1 - v_ ;

		pasos_++ ;

		dejar_listos(ctx) ;

	}

	IBuffer * velocidad() const override { return vel_[v_] ; }

	IBuffer * escalar() const override { return esc_[v_] ; }

	std :: uint64_t pasos() const override { return pasos_ ; }

	std :: vector<std :: string> tramos() const override { return { "vortice advectar" , "vortice presion" } ; }

	void medir(Medidor * banco , std :: size_t primera) override { banco_ = banco ; primera_ = primera ; }

private:

	PedidoFluido pedido_ ;

	Gpu gpu_ ;

	RefCntAutoPtr<IBuffer> uniforme_ ;

	Kernel iniciar_ , advectar_ , rotacional_ , confinar_ , divergencia_ , jacobi_ , proyectar_ ;

	RefCntAutoPtr<IBuffer> vel_[2] , esc_[2] , presion_[2] , div_ , rot_ ;

	// los bindings, preparados una vez para cada turno : [cuál de la pareja
	// de velocidad y escalar es la actual] y [cuál de la de presión]

	RefCntAutoPtr<IShaderResourceBinding> iniciar_b_ , advectar_b_[2] , rotacional_b_[2] , confinar_b_[2] , divergencia_b_[2] ;

	RefCntAutoPtr<IShaderResourceBinding> jacobi_b_[2] , proyectar_b_[2][2] ;

	// cuál de cada pareja tiene el valor actual

	int v_ = 0 , p_ = 0 ;

	bool iniciado_ = false ;

	std :: uint64_t pasos_ = 0 ;

	Medidor * banco_ = nullptr ;

	std :: size_t primera_ = 0 ;

	Uint64 celdas() const { return static_cast<Uint64>(pedido_.forma.celdas) * pedido_.forma.celdas ; }

	bool crear_uniforme(std :: string & error) {

		BufferDesc desc ;

		desc.Name = "Nodos fluido uniforms" ;

		desc.Size = sizeof(FluidoGpuDatos) ;

		desc.Usage = USAGE_DEFAULT ;

		desc.BindFlags = BIND_UNIFORM_BUFFER ;

		desc.ImmediateContextMask = gpu_.mascara ;

		gpu_.device->CreateBuffer(desc , nullptr , &uniforme_) ;

		return uniforme_ || gpu_fallo(error , "No se pudo crear el uniform del fluido") ;

	}

	bool crear_kernels(IBuffer * cbuffer_efecto , std :: string & error) {

		const std :: string base = fluido_hlsl(pedido_.forma.celdas) + pedido_.forzamiento ;

		const Uint32 grupos = pedido_.forma.celdas / 8 ;

		const auto kernel = [&](const char * nombre , const char * codigo , Kernel & k) {

			k = gpu_kernel(gpu_ , { nombre , base + codigo , { { "Fluido" , uniforme_ } , { pedido_.efecto.c_str() , cbuffer_efecto } } , grupos , grupos } , error) ;

			return k.pipeline.RawPtr() != nullptr ;

		} ;

		return kernel("Nodos fluido iniciar" , iniciar_hlsl , iniciar_)

			&& kernel("Nodos fluido advectar" , advectar_hlsl , advectar_)

			&& kernel("Nodos fluido rotacional" , rotacional_hlsl , rotacional_)

			&& kernel("Nodos fluido confinar" , confinar_hlsl , confinar_)

			&& kernel("Nodos fluido divergencia" , divergencia_hlsl , divergencia_)

			&& kernel("Nodos fluido jacobi" , jacobi_hlsl , jacobi_)

			&& kernel("Nodos fluido proyectar" , proyectar_hlsl , proyectar_) ;

	}

	bool crear_campos(std :: string & error) {

		for (int k = 0 ; k < 2 ; k++) {

			vel_[k] = gpu_campo(gpu_ , { celdas() , 8 , "Nodos fluido velocidad" }) ;

			esc_[k] = gpu_campo(gpu_ , { celdas() , 4 , "Nodos fluido escalar" }) ;

			presion_[k] = gpu_campo(gpu_ , { celdas() , 4 , "Nodos fluido presion" }) ;

			if (!vel_[k] || !esc_[k] || !presion_[k]) { return gpu_fallo(error , "No se pudieron crear los campos del fluido") ; }

		}

		div_ = gpu_campo(gpu_ , { celdas() , 4 , "Nodos fluido divergencia" }) ;

		rot_ = gpu_campo(gpu_ , { celdas() , 4 , "Nodos fluido rotacional" }) ;

		return (div_ && rot_) || gpu_fallo(error , "No se pudieron crear los campos del fluido") ;

	}

	// Un binding por cada turno de las parejas

	bool atar() {

		iniciar_b_ = gpu_atar(iniciar_ , { { "vel_sal" , gpu_escritura(vel_[0]) } , { "esc_sal" , gpu_escritura(esc_[0]) } }) ;

		for (int a = 0 ; a < 2 ; a++) {

			const int b = 1 - a ;

			advectar_b_[a] = gpu_atar(advectar_ , {

				{ "vel_ent" , gpu_lectura(vel_[a]) } ,

				{ "esc_ent" , gpu_lectura(esc_[a]) } ,

				{ "vel_sal" , gpu_escritura(vel_[b]) } ,

				{ "esc_sal" , gpu_escritura(esc_[b]) }

			}) ;

			rotacional_b_[a] = gpu_atar(rotacional_ , { { "vel_ent" , gpu_lectura(vel_[b]) } , { "rot_sal" , gpu_escritura(rot_) } }) ;

			confinar_b_[a] = gpu_atar(confinar_ , {

				{ "vel_ent" , gpu_lectura(vel_[b]) } ,

				{ "rot_ent" , gpu_lectura(rot_) } ,

				{ "vel_sal" , gpu_escritura(vel_[a]) }

			}) ;

			divergencia_b_[a] = gpu_atar(divergencia_ , { { "vel_ent" , gpu_lectura(vel_[a]) } , { "div_sal" , gpu_escritura(div_) } }) ;

			jacobi_b_[a] = gpu_atar(jacobi_ , {

				{ "pres_ent" , gpu_lectura(presion_[a]) } ,

				{ "div_ent" , gpu_lectura(div_) } ,

				{ "pres_sal" , gpu_escritura(presion_[b]) }

			}) ;

			for (int c = 0 ; c < 2 ; c++) {

				proyectar_b_[a][c] = gpu_atar(proyectar_ , {

					{ "vel_ent" , gpu_lectura(vel_[a]) } ,

					{ "pres_ent" , gpu_lectura(presion_[c]) } ,

					{ "vel_sal" , gpu_escritura(vel_[b]) } ,

					{ "pres_sal" , gpu_escritura(presion_[1 - c]) }

				}) ;

			}

		}

		return true ;

	}

	void subir_paso(IDeviceContext * ctx , float dt) {

		const FluidoGpuDatos datos{ { dt , pedido_.forma.confinamiento , q_circulo(pedido_.forma.celdas) , 0.0f } } ;

		ctx->UpdateBuffer(uniforme_ , 0 , sizeof(datos) , &datos , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	}

	// Advección con las fuerzas, y el refuerzo de los remolinos pequeños

	void advectar_y_confinar(IDeviceContext * ctx) {

		gpu_lanzar(ctx , advectar_ , advectar_b_[v_]) ;

		gpu_lanzar(ctx , rotacional_ , rotacional_b_[v_]) ;

		gpu_lanzar(ctx , confinar_ , confinar_b_[v_]) ;

	}

	// Divergencia, presión y proyección : queda incompresible

	void proyectar(IDeviceContext * ctx) {

		gpu_lanzar(ctx , divergencia_ , divergencia_b_[v_]) ;

		for (std :: uint32_t j = 0 ; j < pedido_.forma.jacobi ; j++) {

			gpu_lanzar(ctx , jacobi_ , jacobi_b_[p_]) ;

			p_ = 1 - p_ ;

		}

		gpu_lanzar(ctx , proyectar_ , proyectar_b_[v_][p_]) ;

		p_ = 1 - p_ ;

	}

	// Listos para leer antes de fijar el objetivo : dentro del render pass ya
	// no se pueden cambiar de estado

	void dejar_listos(IDeviceContext * ctx) {

		StateTransitionDesc listos[] = {

			{ vel_[v_].RawPtr() , RESOURCE_STATE_UNKNOWN , RESOURCE_STATE_SHADER_RESOURCE , STATE_TRANSITION_FLAG_UPDATE_STATE } ,

			{ esc_[v_].RawPtr() , RESOURCE_STATE_UNKNOWN , RESOURCE_STATE_SHADER_RESOURCE , STATE_TRANSITION_FLAG_UPDATE_STATE }

		} ;

		ctx->TransitionResourceStates(2 , listos) ;

	}

} ;

}  // namespace

FabricaFluido fabrica_fluido_stam() {

	return [](const PedidoFluido & pedido) -> std :: unique_ptr<Fluido> { return std :: make_unique<FluidoStam>(pedido) ; } ;

}

}  // namespace shd :: nodos
