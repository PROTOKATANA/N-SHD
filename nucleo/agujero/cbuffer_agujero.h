# pragma once

# include <cstdint>

# include "shd/src/agujero/puente.rs.h"

namespace shd {

// El cbuffer «Agujero» de cada fotograma y su HLSL : el cbuffer, el azar y los rayos de Kerr en float (lo mismo que
// kerr.rs). Sin Diligent

struct AgujeroGpu {

	float tiempo[4] ;  // desde el arranque (s) , para el ruido (vuelve a 0 cada día) , paso (s) , 0

	std :: uint32_t tam[4] ;  // ancho , alto (px) , 0 , 0

	float camara[4] ;  // posición (xyz) , el giro a

	float derecha[4] ;  // la base de la cámara (xyz) , tan(campo vertical / 2)

	float arriba[4] ;  // (xyz) , ancho / alto

	float adelante[4] ;  // (xyz) , 0

	float kerr[4] ;  // horizonte , borde interior del disco (ISCO) , exterior , lejos

	float trazado[4] ;  // precisión , pasos máximos , margen del horizonte , rozar (ver DescTrazado)

	float disco[4] ;  // ln r interior , ln r exterior , opacidad , brillo

	float luz[4] ;  // 0 , 0 , el pico del brillo del disco (para normalizar) , exposición

	float orbita[4] ;  // el azimut de la cámara (rad) , la reducción del mapa bajo , las filas listas del mapa alto , de qué mapa son los bordes (0, 1 : bajo, 2 : alto)

} ;

static_assert(sizeof(AgujeroGpu) == 11 * 16) ;

extern const char * const agujero_hlsl ;

// Lo de este fotograma

struct MomentoAgujero {

	double t ;  // s desde el arranque

	double paso ;  // s desde el fotograma anterior

	std :: uint32_t ancho ;  // px

	std :: uint32_t alto ;

	// la cámara (ver orbita.rs) : los rayos se trazan con la base de trazado
	// (azimut 0) y el azimut gira el disco y el cielo al componer

	agujero :: BaseCamara camara ;

	double azimut ;

	// los mapas de rayos (ver rayos_kerr.h)

	std :: uint32_t reduccion ;

	std :: uint32_t filas_listas ;

} ;

void rellenar_agujero(const MomentoAgujero & m , AgujeroGpu & g) ;

}  // namespace shd
