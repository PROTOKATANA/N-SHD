# pragma once

# include <functional>

# include <string>

# include "DeviceContext.h"

# include "RenderDevice.h"

namespace shd {

// Un mensaje para quien integra : lo que no compila y los diagnósticos

using Aviso = std :: function<void(const std :: string &)> ;

// Lo que presta quien integra. N-SHD no crea el dispositivo ni conoce la
// ventana : pinta con esto, en el hilo de `ctx`. Puede crear recursos desde
// otros hilos mientras se prepara, porque el dispositivo de Diligent lo admite

struct Gpu {

	Diligent :: IRenderDevice * device = nullptr ;

	Diligent :: IDeviceContext * ctx = nullptr ;

	// El ImmediateContextMask de lo que se crea. Diligent solo deja usar un
	// recurso en los contextos de su máscara, y lo libera cuando todos ellos
	// avanzan : con varios contextos, solo el bit de `ctx`

	Diligent :: Uint64 mascara = 1 ;

	// Dónde guardar el SPIR-V de cada shader, para no compilarlo en el
	// siguiente arranque. Vacío : sin caché

	std :: string cache ;

	Aviso aviso ;

} ;

// El primer error se queda; siempre false, para devolverlo

inline bool gpu_fallo(std :: string & error , const char * mensaje) {

	if (error.empty()) { error = mensaje ; }

	return false ;

}

}  // namespace shd
