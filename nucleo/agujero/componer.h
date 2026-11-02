# pragma once

# include <cstdint>

# include <string>

# include "Buffer.h"

# include "DeviceContext.h"

# include "RefCntAutoPtr.hpp"

# include "RenderDevice.h"

# include "ShaderResourceBinding.h"

# include "campo_cielo.h"

# include "campos_disco.h"

# include "computo.h"

namespace shd {

// Cada fotograma : de cada píxel, su mapa de rayos (rayos_kerr) y la
// temperatura del disco donde lo cruza (por el puerto CamposDisco) → la luz
// del píxel, en el acumulado que pinta el tono. El disco, como el vórtice de
// nodos : por bandas, opaco y sin Doppler. Detrás, el cielo (por el puerto
// CampoCielo : el polvo), en la dirección por la que escapa cada rayo : se
// curva con el agujero

class Componer {

public:

	// `uniforme` : el cbuffer «Agujero» (cbuffer_agujero.h)

	bool crear(const Gpu & gpu , Diligent :: IBuffer * uniforme , const CamposDisco & campos , const CampoCielo & cielo , std :: string & error) ;

	// El acumulado de este tamaño (tres uint por píxel)

	bool ajustar(std :: uint32_t ancho , std :: uint32_t alto , std :: string & error) ;

	// `mapa` y `bajo` : los de rayos_kerr (el alto, del mismo tamaño; el
	// bajo, para las filas que el alto aún no tiene), y sus bordes (con sus
	// subrayos : ver rayos_kerr.h)

	void dibujar(Diligent :: IDeviceContext * ctx , Diligent :: IBuffer * mapa , Diligent :: IBuffer * bajo , Diligent :: IBuffer * indice_bordes , Diligent :: IBuffer * bordes) ;

	Diligent :: IBuffer * acumulado() const { return acumulado_ ; }

private:

	Gpu gpu_ ;

	const CamposDisco * campos_ = nullptr ;

	const CampoCielo * cielo_ = nullptr ;

	Kernel componer_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> acumulado_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> componer_b_[2] ;

	Diligent :: IBuffer * atado_a_ = nullptr ;

	Diligent :: IBuffer * atado_bajo_ = nullptr ;

	Diligent :: IBuffer * atado_indice_ = nullptr ;

	std :: uint32_t ancho_ = 0 ;

	std :: uint32_t alto_ = 0 ;

	// lo que cabe el acumulado, en píxeles : crece con holgura y no encoge

	Diligent :: Uint64 capacidad_ = 0 ;

} ;

}  // namespace shd
