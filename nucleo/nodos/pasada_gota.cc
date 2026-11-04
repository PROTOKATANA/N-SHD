# include "pasada_gota.h"

# include <string>

# include <utility>

# include <vector>

# include "gota_campo.h"

# include "gota_canales.h"

# include "gota_comun.h"

# include "gota_particulas.h"

# include "gota_pintura.h"

# include "gota_sueltos.h"

# include "pipeline_comun.h"

using namespace Diligent ;

namespace shd :: nodos {

// Las piezas de la gota en un dispositivo : las partículas, el campo, los canales, los trozos
// sueltos y la pintura. La pasada solo las une : les da los buffers que leen y las lanza por
// turno

struct PasadaGota :: Recursos {

	Gpu gpu ;

	RefCntAutoPtr<IBuffer> uniforme ;

	ParticulasGota particulas ;

	CampoGota campo ;

	CanalesGota canales ;

	SueltosGota sueltos ;

	PinturaGota pintura ;

	bool crear(const Gpu & prestada , IBuffer * camara , std :: string & error) {

		gpu = prestada ;

		BufferDesc d ;

		d.Name = "Nodos gota uniforms" ;

		d.Size = sizeof(GotaGpu) ;

		d.Usage = USAGE_DEFAULT ;

		d.BindFlags = BIND_UNIFORM_BUFFER ;

		d.ImmediateContextMask = gpu.mascara ;

		gpu.device->CreateBuffer(d , nullptr , &uniforme) ;

		if (!uniforme) { return gpu_fallo(error , "No se pudo crear el uniform de la gota") ; }

		const DescGota desc = desc_gota() ;

		const std :: string hlsl = hlsl_gota(desc) ;

		if (!particulas.crear(gpu , desc , uniforme , hlsl , error)) { return false ; }

		if (!campo.crear(gpu , desc , uniforme , hlsl , particulas.lecturas() , error)) { return false ; }

		if (!canales.crear(gpu , uniforme , hlsl , campo.campo() , campo.velocidad() , error)) { return false ; }

		const SueltosGota :: Lecturas de_sueltos = { campo.campo() , canales.mascara() , canales.alcance() , canales.cerca() , canales.calor() } ;

		if (!sueltos.crear(gpu , uniforme , hlsl , de_sueltos , error)) { return false ; }

		const PinturaGota :: Lecturas lecturas = { campo.campo() , canales.mascara() , canales.alcance() , canales.cerca() , canales.calor() , sueltos.unido() } ;

		return pintura.crear(gpu , camara , uniforme , hlsl , lecturas , error) ;

	}

	void avanzar(IDeviceContext * ctx , const GotaGpu & datos , Medidor * banco , std :: size_t primera) {

		particulas.avanzar(ctx , static_cast<Uint32>(datos.ritmo[0]) , banco , primera) ;

		campo.avanzar(ctx , particulas.actual() , banco , primera + ParticulasGota :: tramos().size()) ;

		const std :: size_t de_canales = primera + ParticulasGota :: tramos().size() + CampoGota :: tramos().size() ;

		canales.avanzar(ctx , banco , de_canales) ;

		sueltos.avanzar(ctx , banco , de_canales + CanalesGota :: tramos().size()) ;

		// listo para leer antes de fijar el objetivo : dentro del render pass ya no
		// se puede cambiar de estado

		StateTransitionDesc listos[] = {

			{ campo.campo() , RESOURCE_STATE_UNKNOWN , RESOURCE_STATE_SHADER_RESOURCE , STATE_TRANSITION_FLAG_UPDATE_STATE } ,

			{ canales.alcance() , RESOURCE_STATE_UNKNOWN , RESOURCE_STATE_SHADER_RESOURCE , STATE_TRANSITION_FLAG_UPDATE_STATE } ,

			{ canales.mascara() , RESOURCE_STATE_UNKNOWN , RESOURCE_STATE_SHADER_RESOURCE , STATE_TRANSITION_FLAG_UPDATE_STATE } ,

			{ canales.cerca() , RESOURCE_STATE_UNKNOWN , RESOURCE_STATE_SHADER_RESOURCE , STATE_TRANSITION_FLAG_UPDATE_STATE } ,

			{ canales.calor() , RESOURCE_STATE_UNKNOWN , RESOURCE_STATE_SHADER_RESOURCE , STATE_TRANSITION_FLAG_UPDATE_STATE } ,

			{ sueltos.unido() , RESOURCE_STATE_UNKNOWN , RESOURCE_STATE_SHADER_RESOURCE , STATE_TRANSITION_FLAG_UPDATE_STATE }

		} ;

		ctx->TransitionResourceStates(6 , listos) ;

	}

} ;

PasadaGota :: PasadaGota(Reloj reloj) : reloj_(std :: move(reloj)) , estado_(estado_gota()) {}

PasadaGota :: ~PasadaGota() = default ;

IBuffer * PasadaGota :: distancia_gota() const { return recursos_ ? recursos_->canales.cerca() : nullptr ; }

std :: vector<std :: string> PasadaGota :: tramos() const {

	std :: vector<std :: string> todos = ParticulasGota :: tramos() ;

	for (const auto & lista : { CampoGota :: tramos() , CanalesGota :: tramos() , SueltosGota :: tramos() }) { todos.insert(todos.end() , lista.begin() , lista.end()) ; }

	return todos ;

}

// Sin avanzar nada : quien integra puede preparar y no subir (sin imagen libre, abandona el
// fotograma), y entonces la gota perdía ese tiempo y lo que se movieron los nodos. El estado
// avanza al subir

bool PasadaGota :: preparar(const Gpu & gpu , const Fotograma & f , CamaraGpu & , std :: string & error) {

	hay_ = f.escena.vortice != 0 ;

	if (!hay_) { return true ; }

	if (!recursos_ || recursos_->gpu.device != gpu.device) {

		recursos_ = std :: make_unique<Recursos>() ;

		if (!recursos_->crear(gpu , f.uniforme_camara , error)) {

			recursos_.reset() ;

			return hay_ = false ;

		}

	}

	return true ;

}

void PasadaGota :: subir(IDeviceContext * ctx , const Fotograma & f) {

	if (!hay_) { return ; }

	const rust :: Slice<const Nodo> nodos(f.nodos->data() , f.nodos->size()) ;

	const rust :: Slice<const std :: uint32_t> aristas(f.aristas->data() , f.aristas->size()) ;

	hay_ = estado_->rellenar(f.escena , nodos , aristas , reloj_() , datos_) ;

	if (!hay_) { return ; }

	ctx->UpdateBuffer(recursos_->uniforme , 0 , sizeof(datos_) , &datos_ , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	recursos_->avanzar(ctx , datos_ , banco_ , primera_) ;

}

void PasadaGota :: dibujar(IDeviceContext * ctx , const Fotograma &) {

	if (!hay_) { return ; }

	recursos_->pintura.dibujar(ctx) ;

}

}  // namespace shd :: nodos
