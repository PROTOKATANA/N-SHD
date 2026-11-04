# pragma once

# include <cstddef>

# include <string>

# include <vector>

# include "Buffer.h"

# include "DeviceContext.h"

# include "RefCntAutoPtr.hpp"

# include "ShaderResourceBinding.h"

# include "computo.h"

# include "gpu.h"

# include "medidor.h"

namespace shd :: nodos {

// Los trozos sueltos de la gota : el líquido que no está unido al cuerpo, ni por la gota ni
// por los canales, no se pinta. Sobre la rejilla de los canales, qué celdas tienen líquido
// (con la misma cuenta que la pintura, gota_liquido_hlsl.h) y cuáles llegan, de líquido en
// líquido, al centro de la gota. Las partículas de un trozo siguen en la simulación y se
// ven otra vez al volver a la gota

class SueltosGota {

public:

	// Lo que dejan el campo (CampoGota) y los canales (CanalesGota)

	struct Lecturas {

		Diligent :: IBuffer * campo ;

		Diligent :: IBuffer * mascara ;

		Diligent :: IBuffer * alcance ;

		Diligent :: IBuffer * cerca ;

		Diligent :: IBuffer * calor ;

	} ;

	bool crear(

		const Gpu & gpu ,

		Diligent :: IBuffer * uniforme ,

		const std :: string & hlsl ,

		const Lecturas & lecturas ,

		std :: string & error

	) ;

	// Una vez por fotograma, tras los canales. Mide en `banco`, en `primera`

	void avanzar(Diligent :: IDeviceContext * ctx , Medidor * banco , std :: size_t primera) ;

	// Lo que lee la pintura : un bit por celda de los canales, fila a fila en palabras de 32,
	// a 1 en el líquido unido al cuerpo; y detrás, otros tantos a 1 donde hay líquido

	Diligent :: IBuffer * unido() const { return unido_ ; }

	static std :: vector<std :: string> tramos() ;

private:

	Gpu gpu_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IBuffer> bits_ , unido_ ;

	Kernel marcar_ , unir_ ;

	Diligent :: RefCntAutoPtr<Diligent :: IShaderResourceBinding> marcar_b_ , unir_b_ ;

} ;

}  // namespace shd :: nodos
