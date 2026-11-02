# pragma once

# include <cstddef>

# include <cstdint>

# include <string>

# include "Buffer.h"

# include "DeviceContext.h"

# include "RefCntAutoPtr.hpp"

# include "RenderDevice.h"

# include "ShaderResourceBinding.h"

# include "medidor.h"

# include "computo.h"

namespace shd {

// El mapa de rayos : por cada píxel, lo que encuentra su rayo trazado hacia
// atrás alrededor del agujero de Kerr (kerr.h, el mismo cálculo en float) :
// hasta 3 cruces con el disco (r, φ̃) y la dirección del cielo en la que
// escapa (θ, φ), o que cae. Se traza con azimut 0 (el azimut de la cámara
// solo gira el disco y el cielo, ver orbita.h) : cambia con el tamaño de la
// ventana y con la inclinación.
//
// Dos mapas : el alto (la resolución entera) y el bajo (1/reduccion). Al
// cambiar de tamaño, el alto se traza entero si la inclinación está quieta
// (moviéndose, solo el bajo, el que se ve). Mientras la inclinación se
// mueve, se traza el bajo (rápido) y el alto se da por perdido; parada, el
// alto se vuelve a trazar por bandas, unas filas en cada fotograma, y
// componer usa las filas listas del alto y el bajo para el resto.
//
// Dos float4 por píxel : (r₁, φ₁, r₂, φ₂) y (r₃, φ₃, θ, φ); r = 0, sin
// cruce; θ = −10, cae

class RayosKerr {

public:

	// `uniforme` : el cbuffer «Agujero» (cbuffer_agujero.h), ya rellenado con
	// este tamaño antes de ajustar

	bool crear(const Gpu & gpu , Diligent :: IBuffer * uniforme , std :: string & error) ;

	// Lo de este fotograma : el tamaño, si la inclinación cambió, cuántas
	// filas del alto trazar si está parada y la reducción del bajo. `trazado`
	// : si trazó el alto entero (un tamaño nuevo)

	bool actualizar(Diligent :: IDeviceContext * ctx , std :: uint32_t ancho , std :: uint32_t alto , bool inclinacion_cambio , std :: uint32_t filas_por_fotograma , std :: uint32_t reduccion , bool * trazado , std :: string & error) ;

	Diligent :: IBuffer * mapa_alto() const { return alto_m_ ; }

	Diligent :: IBuffer * mapa_bajo() const { return bajo_m_ ; }

	// las filas del alto trazadas con la inclinación de ahora

	std :: uint32_t filas_listas() const { return filas_listas_ ; }

	std :: uint32_t reduccion() const { return reduccion_ ; }

	// Los bordes (ver rayos_kerr.cc) : de qué mapa son (0 : ninguno, 1 : el
	// bajo, 2 : el alto), el índice de cada píxel de ese mapa en la lista
	// (SIN_BORDE si no es borde) y sus 4 subrayos (2 float4 cada uno)

	std :: uint32_t bordes_de() const { return bordes_de_ ; }

	Diligent :: IBuffer * indice_bordes() const { return indice_ ; }

	Diligent :: IBuffer * bordes() const { return bordes_ ; }

	static constexpr std :: uint32_t capacidad_bordes = 65536 ;

	// Medir cada kernel en `banco`, en sus pasadas `primera` a `primera` + 2
	// (trazar, detectar los bordes, trazar sus subrayos). nullptr : sin medir

	void medir(Medidor * banco , std :: size_t primera) { banco_ = banco ; primera_medida_ = primera ; }

private:

	Gpu gpu_ ;

	Kernel trazar_ , detectar_ , trazar_bordes_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> banda_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> alto_m_ , bajo_m_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> alto_b_ , bajo_b_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> indice_ , contador_ , posicion_ , bordes_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> detectar_alto_b_ , detectar_bajo_b_ , trazar_bordes_b_ ;

	std :: uint32_t bordes_de_ = 0 ;

	Medidor * banco_ = nullptr ;

	std :: size_t primera_medida_ = 0 ;

	// Busca los bordes del mapa (`cual` : 1, el bajo; 2, el alto) y les traza
	// sus subrayos

	void bordes_de_mapa(Diligent :: IDeviceContext * ctx , std :: uint32_t cual) ;

	std :: uint32_t ancho_ = 0 ;

	std :: uint32_t alto_ = 0 ;

	std :: uint32_t reduccion_ = 1 ;

	// lo que caben los mapas (en píxeles del alto y del bajo) : crecen con
	// holgura y no encogen, así redimensionar no reserva en cada paso

	Diligent :: Uint64 capacidad_alto_ = 0 , capacidad_bajo_ = 0 ;

	std :: uint32_t filas_listas_ = 0 ;

	// Traza las filas [desde, desde + filas) de un mapa de reducción r

	void trazar(Diligent :: IDeviceContext * ctx , Diligent :: IShaderResourceBinding * b , std :: uint32_t r , std :: uint32_t desde , std :: uint32_t filas) ;

} ;

}  // namespace shd
