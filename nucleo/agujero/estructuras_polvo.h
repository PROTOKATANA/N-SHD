# pragma once

namespace shd {

// Las hebras y los cúmulos del polvo (ver PolvoForma) : qué partículas van
// en cada una, cuánto vive cada estructura, dónde nace y cómo se reparten en
// ella sus partículas (una hebra, en un arco de círculo menor; un cúmulo, en
// una gaussiana redonda); y el kernel que lleva sus centros por el flujo.
//
// Pieza de HLSL, sin estado : el polvo (polvo_cielo) la incluye en sus
// kernels. Necesita de quien la incluye : el cbuffer «Polvo» (paso, centro,
// antes, hebra, cumulo), TAU, azar(), en_casquete(); y los define CADA_HEBRA,
// CURVAS, LARGO_MIN, LARGO_MAX, CURVA_MIN, CURVA_MAX, CADA_CUMULO, CUMULOS,
// ESTRUCTURAS y RADIO_CUMULO. El kernel de los centros, además, flujo_en(),
// al_casquete() y radio_casquete()

// de_hebra, hebra_de, de_cumulo, cumulo_de, vida_estructura, rotar,
// largo_hebra, a_lo_largo, en_su_hebra, en_hebra, centro_cumulo, en_cumulo,
// nace_estructura y LUZ_IGUAL

const char * estructuras_hlsl() ;

// Los centros de las estructuras (las hebras, y luego los cúmulos) : nacen
// con su estructura, se mueven con el flujo (y pasan al otro lado del
// casquete con la cámara), y guardan su velocidad : sus partículas se mueven
// con ella (la cohesión : no se deshacen). Con paso.z, sembrar : todos en su
// sitio de nacer

const char * centros_hlsl() ;

}  // namespace shd
