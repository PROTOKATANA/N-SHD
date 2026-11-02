# include "copia_cpu.h"

# include <cstring>

using namespace Diligent ;

namespace shd {

void CopiaCpu :: copiar(const Gpu & gpu , IDeviceContext * ctx , std :: initializer_list<IBuffer *> buffers) {

	copias_.resize(buffers.size()) ;

	std :: size_t i = 0 ;

	for (IBuffer * b : buffers) {

		const Uint64 bytes = b->GetDesc().Size ;

		RefCntAutoPtr<IBuffer> & copia = copias_[i++] ;

		if (!copia || copia->GetDesc().Size != bytes) {

			BufferDesc d ;

			d.Name = "Nodos copia para la CPU" ;

			d.Usage = USAGE_STAGING ;

			d.CPUAccessFlags = CPU_ACCESS_READ ;

			d.Size = bytes ;

			copia.Release() ;

			d.ImmediateContextMask = gpu.mascara ;

			gpu.device->CreateBuffer(d , nullptr , &copia) ;

		}

		ctx->CopyBuffer(b , 0 , RESOURCE_STATE_TRANSITION_MODE_TRANSITION , copia , 0 , bytes , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

		// listo otra vez para los shaders, antes de fijar el objetivo

		StateTransitionDesc listo{ b , RESOURCE_STATE_UNKNOWN , RESOURCE_STATE_SHADER_RESOURCE , STATE_TRANSITION_FLAG_UPDATE_STATE } ;

		ctx->TransitionResourceStates(1 , &listo) ;

	}

	lista_ = true ;

}

bool CopiaCpu :: leer(IDeviceContext * ctx , std :: vector<std :: vector<std :: uint8_t>> & datos) {

	if (!lista_) { return false ; }

	lista_ = false ;

	ctx->WaitForIdle() ;

	datos.assign(copias_.size() , {}) ;

	for (std :: size_t i = 0 ; i < copias_.size() ; i++) {

		void * p = nullptr ;

		ctx->MapBuffer(copias_[i] , MAP_READ , MAP_FLAG_NONE , p) ;

		if (!p) { return false ; }

		const auto * b = static_cast<const std :: uint8_t *>(p) ;

		datos[i].assign(b , b + copias_[i]->GetDesc().Size) ;

		ctx->UnmapBuffer(copias_[i] , MAP_READ) ;

	}

	return true ;

}

}  // namespace shd
