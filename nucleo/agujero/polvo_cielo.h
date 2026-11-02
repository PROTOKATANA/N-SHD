# pragma once

# include <cstddef>

# include <string>

# include <vector>

# include "Buffer.h"

# include "DeviceContext.h"

# include "RefCntAutoPtr.hpp"

# include "RenderDevice.h"

# include "ShaderResourceBinding.h"

# include "medidor.h"

# include "campo_cielo.h"

# include "computo.h"

# include "forma_agujero.h"

namespace shd {

// El polvo del fondo, como el de liquido (puntos de un píxel, de un blanco
// cálido, que flotan en remolinos), pero en el cielo, así que se curva con
// el agujero : arcos y anillo al pasar detrás de la sombra, y el disco y la
// sombra lo tapan.
//
// Partículas : cada una es una dirección, que se mueve por la esfera con un
// flujo sin fuentes (x × ∇ψ, con ψ un ruido 3D que cambia despacio, como el
// rotacional de liquido), vive su vida (con un fundido al nacer y al
// apagarse) y renace en otro sitio. Hay tres clases : el polvo suelto; las
// hebras, trazos curvos que nacen enteros; y los cúmulos, grupos apretados
// que se ven como estrellas lejanas. Las de una hebra o un cúmulo se mueven
// con el centro de su estructura (un kernel lo lleva por el flujo) : no se
// deshacen; y pasan enteras al otro lado del casquete solo cuando ya no se
// ve nada de ellas (el mover mide hasta dónde llegan). Viven en el casquete del cielo que la
// cámara puede ver a través de la lente (alrededor de la dirección que queda
// detrás del agujero; sigue a la cámara al orbitar) : la que sale por un
// borde entra por el otro, fuera de lo que se ve, como el polvo de liquido
// por los lados de la ventana. Cada fotograma se ordenan por celdas de una
// rejilla en el plano tangente al casquete : se cuentan las de cada celda,
// se suman los prefijos y se reparten, seguidas, las de cada celda. Así, quien pinta
// encuentra las de un píxel mirando solo las celdas que cubre (ver
// color_del_cielo en campo_cielo.h) : cada una, un punto de un píxel
// repartido entre los cuatro que toca (como el polvo de liquido), del mismo
// tamaño en todas partes; la lente solo la cambia de sitio y de brillo.
//
// Los parámetros, en forma_agujero.h (PolvoForma). Todo en el hilo que pinta

class PolvoCielo : public CampoCielo {

public:

	explicit PolvoCielo(const PolvoForma & forma) : forma_(forma) {}

	bool crear(const Gpu & gpu , std :: string & error) ;

	// Mueve las partículas `dt` s (en el instante `t`) y las ordena por
	// celdas. `centro` : la dirección del cielo que queda detrás del agujero
	// visto desde la cámara (unitaria, en el sistema del agujero); el polvo
	// vive en un casquete alrededor (ver PolvoForma :: radio). La primera vez,
	// antes, lo siembra

	void avanzar(Diligent :: IDeviceContext * ctx , float dt , float t , const float (& centro)[3]) ;

	// Medir sus kernels en `banco`, de `primera` a `primera` + 2 (mover,
	// sumar los prefijos, repartir). nullptr : sin medir

	void medir(Medidor * banco , std :: size_t primera) { banco_ = banco ; primera_medida_ = primera ; }

	// CampoCielo : las partículas ordenadas y dónde empieza cada celda

	std :: vector<Recurso> recursos() const override ;

	std :: string definiciones() const override ;

private:

	PolvoForma forma_ ;

	Gpu gpu_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> uniforme_ ;

	// estado : la dirección y la luz de ahora; en_celda : su celda y su
	// puesto en ella; cuentas, inicio y prefijos : el orden; puntos : las
	// partículas ordenadas por celda

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> estado_ , en_celda_ , cuentas_ , inicio_ , sumas_ , prefijos_ , puntos_ , centros_ , alcance_ ;

	Kernel centros_k_ , sembrar_ , limpiar_ , mover_ , escanear_ , escanear_sumas_ , repartir_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> centros_b_ , sembrar_b_ , limpiar_b_ , mover_b_ , escanear_b_ , escanear_sumas_b_ , repartir_b_ ;

	bool sembrado_ = false ;

	// el centro del casquete del fotograma anterior (ver avanzar)

	float antes_[3] = { 0 , 0 , -1 } ;

	Medidor * banco_ = nullptr ;

	std :: size_t primera_medida_ = 0 ;

} ;

}  // namespace shd
