# pragma once

# include <cstddef>

# include <cstdint>

# include <string>

# include <vector>

# include "Buffer.h"

# include "DeviceContext.h"

# include "gpu.h"

# include "medidor.h"

# include "shd/src/nodos/puente.rs.h"

namespace shd :: nodos {

// Lo que un fotograma da a cada pasada

struct Fotograma {

	Escena escena ;

	// Los nodos de la escena. Los que pase del tope no se dibujan en el líquido

	const std :: vector<Nodo> * nodos ;

	// Los pares de aristas, aplanados

	const std :: vector<std :: uint32_t> * aristas ;

	// El cbuffer «Camara» del dispositivo, para atarlo a los pipelines

	Diligent :: IBuffer * uniforme_camara ;

} ;

// Una cosa que se dibuja (el líquido, el vórtice…), en el orden en que se le
// pasen a quien las recorra. Cada pasada se ocupa de sus pipelines y de sus
// buffers, y una nueva encaja sin tocar las demás

class Pasada {

public:

	virtual ~Pasada() = default ;

	// Antes de pedir el turno a la GPU : pipelines (una vez por dispositivo),
	// buffers y cálculos de CPU. Puede ajustar la cámara compartida (el
	// líquido pone ahí su caja). Quien integra puede preparar y no seguir
	// (sin imagen libre, abandona el fotograma) : aquí no avanza nada que
	// dependa del tiempo ni de lo que se movió desde el fotograma anterior.
	// Eso, al subir

	virtual bool preparar(const Gpu & gpu , const Fotograma & f , CamaraGpu & camara , std :: string & error) = 0 ;

	// Con el turno : sube a la GPU lo que cambió

	virtual void subir(Diligent :: IDeviceContext * ctx , const Fotograma & f) = 0 ;

	// Con el objetivo fijado : dibuja

	virtual void dibujar(Diligent :: IDeviceContext * ctx , const Fotograma & f) = 0 ;

	// Lo que dibuja se mueve solo (el vórtice) : hay que redibujar en cada
	// fotograma aunque la escena no cambie

	virtual bool animada(const Escena &) const { return false ; }

	// Para los bancos y los registros

	virtual const char * nombre() const = 0 ;

	// Lo que mide por dentro, si quiere : los nombres de sus tramos, y el banco
	// donde medirlos, en `primera`, `primera` + 1… Sin banco no mide nada

	virtual std :: vector<std :: string> tramos() const { return {} ; }

	virtual void medir(Medidor * , std :: size_t) {}

} ;

}  // namespace shd :: nodos
