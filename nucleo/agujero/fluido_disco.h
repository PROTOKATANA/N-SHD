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

# include "campos_disco.h"

# include "computo.h"

namespace shd {

// El fluido del disco de acreción : Navier-Stokes incompresible (Stable
// Fluids de Stam, como el vórtice de nodos) en una rejilla de ángulo × ln r,
// conforme (los remolinos siguen redondos), que da la vuelta en el ángulo y
// tiene pared en la ISCO y en el borde exterior. Encima, el giro de Kepler en
// Kerr como advección de fondo, con una caída en espiral hacia el ojo; un
// viento turbulento que cambia despacio lo mantiene arremolinado, y el
// vorticity confinement refuerza los remolinos pequeños. Lleva un escalar,
// la temperatura, que tiende a su patrón (caliente en el ojo, fría en el
// borde) mientras el flujo la arrastra. Agitado (el doble clic), unos
// chorros lo revuelven (ver agitacion.rs).
//
// En cada paso : advección de la velocidad (semilagrangiana) con el viento,
// divergencia, presión (Jacobi, empezando por la del paso anterior),
// proyección y transporte de la temperatura. Los parámetros, en
// forma_agujero.h (FluidoForma, DiscoForma, AgitacionForma).
//
// Todo en el hilo que pinta

class FluidoDisco : public CamposDisco {

public:

	bool crear(const Gpu & gpu , std :: string & error) ;

	// Un paso de `dt` s; `t` (s) mueve el viento y el patrón. El primero,
	// además, asienta el fluido antes (ver FluidoForma :: asentar)

	void avanzar(Diligent :: IDeviceContext * ctx , float dt , float t) ;

	// Lo agitado que está (ver agitacion.rs; 0 : en calma) y cuántos dobles
	// clics lleva (cada uno, otros chorros) : para los pasos siguientes

	void agitar(float nivel , std :: uint32_t golpes) { agitado_ = nivel ; golpes_ = golpes ; }

	// Medir cada kernel del paso en `banco`, en sus pasadas `primera` a
	// `primera` + 4 (advectar, divergir, Jacobi, proyectar, transportar).
	// nullptr : sin medir

	void medir(Medidor * banco , std :: size_t primera) { banco_ = banco ; primera_medida_ = primera ; }

	// CamposDisco

	Diligent :: IBuffer * temperatura(int k) const override { return temperatura_[k] ; }

	int actual() const override { return actual_ ; }

	std :: string definiciones() const override ;

private:

	Gpu gpu_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> uniforme_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> velocidad_[2] ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> temperatura_[2] ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> presion_[2] ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> divergencia_ ;

	Kernel advectar_ ;

	Kernel divergir_ ;

	Kernel jacobi_ ;

	Kernel proyectar_ ;

	Kernel transportar_ ;

	Kernel iniciar_ ;

	// Los bindings, preparados una vez : por la paridad del paso (de qué
	// buffer a cuál), y los de Jacobi por la de la iteración

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> advectar_b_[2] ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> divergir_b_[2] ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> jacobi_b_[2] ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> proyectar_b_[2] ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> transportar_b_[2] ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> iniciar_b_ ;

	int actual_ = 0 ;

	bool iniciado_ = false ;

	float agitado_ = 0 ;

	std :: uint32_t golpes_ = 0 ;

	Medidor * banco_ = nullptr ;

	std :: size_t primera_medida_ = 0 ;

	bool crear_campos(std :: string & error) ;

	bool crear_kernels(std :: string & error) ;

	void atar() ;

	// El cbuffer de un paso de `dt` s en el instante `t`, y el paso entero
	// (medido en `banco`, si no es nullptr)

	void subir(Diligent :: IDeviceContext * ctx , float dt , float t) ;

	void paso(Diligent :: IDeviceContext * ctx , float dt , float t , Medidor * banco) ;

} ;

}  // namespace shd
