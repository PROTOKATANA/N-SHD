# pragma once

# include <cstddef>

# include "DeviceContext.h"

# include "RenderDevice.h"

namespace shd {

// Cómo medir cada etapa de cada pasada. Lo pone quien integra, si quiere : sin
// medidor, no cuesta nada

enum class Etapa { preparar , subir , dibujar } ;

class Medidor {

public:

	virtual ~Medidor() = default ;

	// Con contexto (subir y dibujar), también en la GPU

	virtual void abrir(std :: size_t pasada , Etapa etapa , Diligent :: IRenderDevice * device , Diligent :: IDeviceContext * ctx) = 0 ;

	virtual void cerrar(std :: size_t pasada , Etapa etapa , Diligent :: IDeviceContext * ctx) = 0 ;

} ;

// Una etapa medida : se abre al crearse y se cierra al salir del ámbito
// (también en un return temprano)

class Tramo {

public:

	Tramo(

		Medidor * medidor ,

		std :: size_t pasada ,

		Etapa etapa ,

		Diligent :: IRenderDevice * device = nullptr ,

		Diligent :: IDeviceContext * ctx = nullptr

	) : medidor_(medidor) , pasada_(pasada) , etapa_(etapa) , ctx_(ctx) {

		if (medidor_) { medidor_->abrir(pasada_ , etapa_ , device , ctx_) ; }

	}

	~Tramo() {

		if (medidor_) { medidor_->cerrar(pasada_ , etapa_ , ctx_) ; }

	}

	Tramo(const Tramo &) = delete ;

	Tramo & operator=(const Tramo &) = delete ;

private:

	Medidor * medidor_ ;

	std :: size_t pasada_ ;

	Etapa etapa_ ;

	Diligent :: IDeviceContext * ctx_ ;

} ;

}  // namespace shd
