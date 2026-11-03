# pragma once

# include <string>

# include "BasicTypes.h"

# include "shd/src/nodos/puente.rs.h"

// Lo que comparten las piezas de la gota (PasadaGota) : las partículas, el campo, los
// canales y la pintura. Ninguna pieza conoce a otra : la pasada les da los buffers que
// leen, y cada una dice los que escribe

namespace shd :: nodos {

// Hilos por grupo de los kernels de partículas : con 256, las 4096 partículas eran 16 grupos
// para las 15 unidades de la GPU, y una hacía el doble mientras las demás esperaban. Medido
// (banco del visor) : la presión, de 0,411 a 0,389 ms, y el campo, de 0,109 a 0,088

constexpr Diligent :: Uint32 hilos_gota = 64 ;

constexpr Diligent :: Uint32 grupos_gota(Diligent :: Uint32 n) { return (n + hilos_gota - 1) / hilos_gota ; }

// Lado de la rejilla del fluido de los canales : 256 cubren los 6 radios de la vista con
// celdas de 0,023 radios, unas cinco a lo ancho del canal más estrecho

constexpr Diligent :: Uint32 lado_canal = 256 ;

// Lo que va delante de cada shader de la gota : sus medidas (N, LADO, G, H, VISTA y M),
// el cbuffer «Gota», los núcleos del SPH, el choque, la silueta del líquido de los enlaces,
// recorrer las vecinas y lo común de la rejilla de los canales

std :: string hlsl_gota(const DescGota & desc) ;

}  // namespace shd :: nodos
