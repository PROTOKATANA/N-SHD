# pragma once

# include <string>

# include "Buffer.h"

namespace shd {

// El puerto de lo que pinta el disco : su temperatura (de 0, fría, a 1), una
// celda por ángulo y por fila de radio. Quien pinta (componer) solo la lee;
// quien la calcula (el fluido de verdad, o un campo fijo para probar) cumple
// esta interfaz y encaja sin tocar a quien pinta.
//
// Dos buffers (StructuredBuffer<float>, una celda por elemento) : el que
// tiene el último paso es actual()

class CamposDisco {

public:

	virtual ~CamposDisco() = default ;

	virtual Diligent :: IBuffer * temperatura(int k) const = 0 ;

	virtual int actual() const = 0 ;

	// Lo que los kernels que la leen necesitan saber de ella (HLSL :
	// REJILLA_ANCHO y REJILLA_ALTO, las celdas de la rejilla)

	virtual std :: string definiciones() const = 0 ;

} ;

}  // namespace shd
