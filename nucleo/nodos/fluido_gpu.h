# pragma once

# include <cstdint>

# include <functional>

# include <memory>

# include <string>

# include "Buffer.h"

# include "DeviceContext.h"

# include "RenderDevice.h"

# include "gpu.h"

# include "shd/src/nodos/puente.rs.h"

namespace shd :: nodos {

// Un fluido incompresible 2D dentro de un círculo, en la GPU. La interfaz :
// el vórtice (o cualquier otro efecto) la usa sin saber qué solver hay
// detrás, y el solver se puede cambiar u optimizar sin tocarlo (hoy,
// FluidoStam : Stable Fluids con Jacobi).
//
// El efecto pone sus fuerzas con un forzamiento : un trozo de HLSL que
// declara su propio cbuffer y define estas funciones (con `fluido.x` = dt) :
//
//   float2 velocidad_inicial(int2 c)   al arrancar
//   float escalar_inicial(int2 c)
//   float2 forzar(int2 c, float2 v)    la velocidad ya advectada, con sus fuerzas
//   float transportar(int2 c, float t) el escalar ya advectado, con sus fuentes
//   float acotar(float t)              lo que se guarda del escalar
//   float fuente(int2 c)               la divergencia que pide la celda (entra > 0, sale < 0)
//
// Lo común (la rejilla N, RADIO, MEDIO, q_de, es_fluido, indice, azar,
// MUESTREADOR y el cbuffer «Fluido») está en fluido_hlsl(), que también
// sirve a los shaders que leen los campos

class Fluido {

public:

	virtual ~Fluido() = default ;

	// Kernels y campos en `device`; `cbuffer_efecto`, el del forzamiento

	virtual bool crear(const Gpu & gpu , Diligent :: IBuffer * cbuffer_efecto , std :: string & error) = 0 ;

	// Un paso de `dt` s (con el turno de la GPU). Al terminar, la velocidad
	// (float2 por celda) y el escalar (float por celda) quedan listos para
	// leerlos en un shader

	virtual void avanzar(Diligent :: IDeviceContext * ctx , float dt) = 0 ;

	virtual Diligent :: IBuffer * velocidad() const = 0 ;

	virtual Diligent :: IBuffer * escalar() const = 0 ;

	virtual std :: uint64_t pasos() const = 0 ;

} ;

// Lo que pide un efecto : la rejilla y el solver, su forzamiento y el
// nombre de su cbuffer

struct PedidoFluido {

	FormaFluido forma ;

	std :: string forzamiento ;

	std :: string efecto ;

} ;

// Cómo se crea un fluido : se inyecta en el efecto, y quien lo crea elige el
// solver

using FabricaFluido = std :: function<std :: unique_ptr<Fluido>(const PedidoFluido &)> ;

// El cbuffer «Fluido» del solver : dt (s), confinamiento, q del círculo, 0

struct FluidoGpuDatos {

	float fluido[4] ;

} ;

static_assert(sizeof(FluidoGpuDatos) == 16) ;

// Lo común a los shaders del fluido para una rejilla de `celdas`

std :: string fluido_hlsl(std :: uint32_t celdas) ;

}  // namespace shd :: nodos
