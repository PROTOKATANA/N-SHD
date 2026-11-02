# pragma once

# include <cstdint>

# include <initializer_list>

# include <vector>

# include "Buffer.h"

# include "DeviceContext.h"

# include "RefCntAutoPtr.hpp"

# include "RenderDevice.h"

# include "gpu.h"

namespace shd {

// Copiar buffers de la GPU a la CPU para leerlos (diagnósticos) : se copian
// en un fotograma, con el turno de la GPU, y se leen en el siguiente. Leer
// espera a la GPU : solo para comprobar, nunca en la app normal. Los buffers
// copiados vuelven a quedar listos para leerse en un shader

class CopiaCpu {

public:

	// Con el turno : copia los buffers (todos de `device`)

	void copiar(

		const Gpu & gpu ,

		Diligent :: IDeviceContext * ctx ,

		std :: initializer_list<Diligent :: IBuffer *> buffers

	) ;

	// Fuera del turno : si hay copia, espera a la GPU y da los bytes de cada
	// buffer, en el mismo orden. false si no hay

	bool leer(Diligent :: IDeviceContext * ctx , std :: vector<std :: vector<std :: uint8_t>> & datos) ;

private:

	std :: vector<Diligent :: RefCntAutoPtr<Diligent :: IBuffer>> copias_ ;

	bool lista_ = false ;

} ;

}  // namespace shd
