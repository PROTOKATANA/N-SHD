# pragma once

# include <string>

# include <vector>

# include "computo.h"

namespace shd {

// El puerto del cielo que se ve detrás del agujero : quien pinta (componer)
// le pregunta, para cada píxel que ve el cielo, su luz, y no sabe qué hay
// dentro (el polvo, o un cielo fijo para probar).
//
// definiciones() es el HLSL para leerlo : declara sus buffers (los de
// recursos(), que quien pinta ata con esos nombres) y la función
// `float3 color_del_cielo(float3 d, float3 jx, float3 jy, float omega)` : d,
// la dirección (unitaria, en el sistema del agujero : z, el eje de giro) por
// la que escapa el rayo del centro del píxel; jx y jy, cuánto cambia esa
// dirección de un píxel al siguiente en x y en y (la lente : así sabe qué
// cae en el píxel aunque el cielo esté estirado o encogido); omega, el
// ángulo sólido del píxel visto desde la cámara (para el aumento). Va detrás
// del cbuffer «Agujero» (puede usar su tiempo)

class CampoCielo {

public:

	virtual ~CampoCielo() = default ;

	virtual std :: vector<Recurso> recursos() const = 0 ;

	virtual std :: string definiciones() const = 0 ;

} ;

}  // namespace shd
