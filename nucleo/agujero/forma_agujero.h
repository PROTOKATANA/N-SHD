# pragma once

# include <cstdint>

namespace shd {

// Los parámetros del agujero negro : el agujero (su giro) y el trazado de los
// rayos, la cámara (fija), el fluido del disco, su brillo y la luz. Solo
// datos (constexpr) : cambiar el aspecto es cambiar aquí. Todo en unidades
// de la masa del agujero (M = 1)

struct KerrForma {

	double giro ;  // a, de 0 (sin giro) a 1 (el máximo)

	double r_out ;  // el borde exterior del disco (el interior es la ISCO)

	double lejos ;  // a esta distancia del centro, un rayo ya escapó

	int max_pasos ;  // de un rayo

	double precision ;  // el paso, en fracción de lo lejos que está del horizonte

	// El rayo que pasa más cerca del centro que rozar × ISCO ya no recoge
	// disco : la esfera negra queda limpia (sin el anillo de fotones pegado,
	// que con el giro parecía un reflejo en su lado izquierdo). 0 : con él

	double rozar ;

} ;

struct CamaraForma {

	double distancia ;  // al centro

	double inclinacion ;  // grados desde el eje de giro (90 : de canto)

	double azimut ;  // grados alrededor del eje

	double campo ;  // grados de campo vertical

	// El ratón (ver orbita.rs) : arrastrar la mueve, como el globo

	double sensibilidad ;  // grados por píxel arrastrado

	double frenado ;  // 1/s : lo que se frena la inercia al soltar

	// Mientras la inclinación cambia, los rayos se trazan a 1/reduccion de
	// la resolución (rápido); al parar, la resolución entera, por bandas de
	// filas_por_fotograma filas

	std :: uint32_t reduccion ;

	std :: uint32_t filas_por_fotograma ;

	// Sola (ver orbita.rs) : la deriva, lo que avanza en x e y sin arrastrar
	// (°/s), y lo que gira su rumbo al azar, como mucho (°/s). A la larga ve
	// el agujero desde todos los ángulos por igual. Con deriva, la cámara
	// nunca está quieta : los rayos se trazan en cada fotograma, a 1/reduccion

	double deriva ;

	double rumbo ;

} ;

// El fluido del disco (ver fluido_disco.h) : una rejilla de ángulo (las
// columnas, dan la vuelta) por logaritmo del radio (las filas, de la ISCO al
// borde exterior)

struct FluidoForma {

	std :: uint32_t ancho ;  // columnas : el ángulo

	std :: uint32_t alto ;  // filas : ln r

	std :: uint32_t jacobi ;  // iteraciones de la presión (múltiplo de 6 : de 3 en 3, un número par de veces; ver fluido_disco.cc)

	float viento ;  // celdas/s : la velocidad del viento al que tiende el fluido

	std :: uint32_t escala ;  // celdas : el tamaño de sus remolinos (divide al ancho : el viento da la vuelta sin costura)

	float relajar ;  // 1/s : lo rápido que el fluido tiende al viento

	float cambio ;  // lo rápido que cambia el viento

	// La temperatura tiende, despacio, a su patrón (el perfil del ojo al
	// borde con manchas de ruido que giran con el disco), y el flujo la
	// arremolina : como la del vórtice, que entraba por el borde y caía en
	// espiral

	float relajar_temperatura ;  // 1/s

	std :: uint32_t escala_patron ;  // celdas : el tamaño de las manchas del patrón (divide al ancho)

	float deriva ;  // lo rápido que se desliza el patrón

	// Antes del primer fotograma el fluido se asienta `asentar` s, en pasos de
	// `paso_asentar` s : el primer fotograma ya es como los de después

	float asentar ;

	float paso_asentar ;

} ;

// El disco : su giro (kepleriano, de Kerr) y cómo brilla

struct DiscoForma {

	float periodo_interior ;  // s : lo que tarda en dar la vuelta el borde interior (el exterior, mucho más)

	float opacidad ;  // lo que tapa el disco, por su densidad (0 : transparente)

	float brillo ;  // la luz del disco donde más brilla

	// Como el vórtice de nodos : el fluido entra frío por el borde y cae en
	// espiral hacia el ojo calentándose; los remolinos pequeños se refuerzan

	float caida ;  // lo que cae hacia el ojo, en fracción de lo que gira (la apertura de la espiral)

	float confinamiento ;  // el refuerzo de los remolinos (vorticity confinement)

	float temperatura_borde ;  // de 0 (ultravioleta) a 1 (rojo), en la gama (ver componer.cc)

	float temperatura_ojo ;  // más de 1 : el rojo cubre una franja junto al ojo

	// La temperatura baja del ojo al borde como u^perfil (u : de 0 en la ISCO
	// a 1 en el borde, en ln r) : cuanto mayor, más lejos llega el calor

	float perfil ;

} ;

struct LuzAgujero {

	float exposicion ;  // la del tono (1 − e^−luz·exposición)

} ;

// El polvo del fondo (ver polvo_cielo.h) : el de liquido, pero en el cielo,
// así que se curva con el agujero

// Las hebras del polvo (ver PolvoForma)

struct HebrasPolvo {

	// las hebras, ya formadas : 1 de cada `cada` partículas va en una
	// de `curvas` hebras, trazos curvos (arcos de largo_min a largo_max °, de
	// círculos menores de curva_min a curva_max ° de radio) que nacen juntos,
	// se doblan con el flujo y viven de vida a vida +
	// vida_extra s, con fundido s al nacer y al apagarse; las
	// demás, polvo suelto. Sus partículas se reparten en el largo máximo (en
	// las más cortas, las que sobran no se pintan) : todas las hebras, con la
	// misma densidad, brillan igual

	std :: uint32_t cada ;

	std :: uint32_t curvas ;

	float largo_min , largo_max ;

	float curva_min , curva_max ;

	float vida , vida_extra , fundido ;

	float luz ;  // la luz de sus partículas, en veces la del polvo suelto

	// la cohesión de las hebras : cuánto se mueven sus partículas con el
	// centro de la hebra y no con su propio remolino (1 : rígidas; 0 : cada
	// una a lo suyo, y las hebras se estiran y se deshilachan)

	float cohesion ;

} ;

// Los cúmulos del polvo : estrellas lejanas (ver PolvoForma)

struct CumulosPolvo {

	// los cúmulos, estrellas lejanas : 1 de cada `cada` partículas (de
	// las que no van en hebras) va en uno de `cuantos` grupos apretados, una
	// gaussiana redonda de radio °, que se ven como un punto más
	// grande y brillante (la luz de sus partículas, × luz). Todos
	// iguales : brillan lo mismo. Van rígidos con su centro (no se deshacen)
	// y viven de vida a vida + vida_extra s, con
	// fundido s

	std :: uint32_t cada ;

	std :: uint32_t cuantos ;

	float radio ;

	float luz ;

	float vida , vida_extra , fundido ;

} ;

// Los colores del polvo (ver PolvoForma y paleta_polvo.h) : el blanco y
// colores_polvo − 1 más

constexpr int colores_polvo = 8 ;

struct ColoresPolvo {

	float blanco[3] ;  // el blanco del polvo : el de las hebras y el primero de la paleta

	// los colores del universo, para las partículas sueltas y los cúmulos
	// (no las hebras, blancas) : cada partícula suelta y cada cúmulo (en cada
	// vida), uno al azar con estos pesos (el blanco y los de la paleta;
	// suman 1). En valor de pantalla; todos se llevan a la luminancia del
	// blanco (en luz lineal) : ninguno brilla más que otro

	float paleta[colores_polvo - 1][3] ;

	float pesos[colores_polvo] ;

} ;

struct PolvoForma {

	std :: uint32_t particulas ;

	// el casquete del cielo donde vive (alrededor de lo que queda detrás del
	// agujero) : su radio (°), y las celdas por lado de su rejilla (para
	// encontrarlas)

	float radio ;

	std :: uint32_t celdas ;

	float frecuencia ;  // celdas del ruido del flujo por radián : el tamaño de sus remolinos

	float cambio ;  // lo rápido que cambian los remolinos

	float velocidad ;  // rad/s por unidad del gradiente del ruido (que anda entre 1 y 3)

	HebrasPolvo hebras ;

	CumulosPolvo cumulos ;

	// cada partícula vive entre vida y vida + vida_extra s, aparece y se
	// apaga en fundido s, y renace en otro sitio

	float vida ;

	float vida_extra ;

	float fundido ;

	float luz ;  // la de una partícula, entera en un píxel

	float aumento_max ;  // tope del aumento de la lente (cerca del anillo, el físico no tiene)

	// la aureola de la lente (el anillo de Einstein, donde la lente amplía el
	// cielo y los pocos puntos que caen parecen girar alrededor) : limpia,
	// sin polvo donde el aumento pasa de aureola_hasta, y con fundido desde
	// aureola_desde

	float aureola_desde ;

	float aureola_hasta ;

	ColoresPolvo colores ;

	// antes del primer fotograma, el polvo se asienta `asentar` s en pasos de
	// `paso_asentar` s (las hebras ya están formadas)

	float asentar ;

	float paso_asentar ;

} ;

// El doble clic revuelve el disco (ver agitacion.rs) : unos chorros (el
// rotacional de un ruido que cambia deprisa, sin fuentes ni sumideros) se
// suman al viento al que tiende el fluido, con fuerza ∝ lo agitado que esté.
// Al calmarse, el giro y la caída lo vuelven a ordenar hacia el agujero

struct AgitacionForma {

	float calma ;  // s : lo agitado cae a 1/e en este tiempo

	float tope ;  // lo agitado, como mucho (cada doble clic suma 1)

	float fuerza ;  // celdas/s : la velocidad de los chorros por cada doble clic

	std :: uint32_t escala ;  // celdas : el tamaño de sus remolinos (divide al ancho)

	float cambio ;  // lo rápido que cambian los chorros

	float agarre ;  // 1/s por doble clic : lo rápido que el fluido los toma

} ;

// Cómo se pinta la temperatura del disco (ver pintura_disco.h) : cuatro
// colores, de fuera (frío) al ojo, cada uno en sus niveles (los escalones de
// la temperatura)

struct PinturaForma {

	float colores[4][3] ;  // de 0 a 1

	std :: uint32_t niveles[4] ;  // cuántos de cada color

	// Dónde acaba cada color (menos el último) en la temperatura, contada en
	// bandas (de 0 al total de niveles − 1) : la temperatura se estira a
	// trozos para que cada color caiga en sus niveles. Así se elige cuánto
	// disco cubre cada color (las bandas de fuera ocupan mucho más que las de
	// dentro)

	float cortes[3] ;

	// Lo que el color sigue a la temperatura dentro de su banda : 1, nada
	// (cada banda, su color puro); con menos, varía hacia el de la vecina

	float escalonado ;

} ;

struct FormaAgujero {

	KerrForma kerr ;

	CamaraForma camara ;

	FluidoForma fluido ;

	DiscoForma disco ;

	LuzAgujero luz ;

	PolvoForma polvo ;

	AgitacionForma agitacion ;

	PinturaForma pintura ;

	float paso_max ;  // tope del paso de tiempo (s) : tras una pausa no da un salto

} ;

constexpr FormaAgujero forma_agujero = {

	// a = 0,9 : la ISCO en 2,32 y el horizonte en 1,44; el disco hasta 18

	{ 0.9 , 18.0 , 2000.0 , 6000 , 0.03 , 1.0 } ,

	// la cámara de frente, sobre el eje de giro (como el vórtice de nodos : un
	// círculo; visto así, la sombra de Kerr es redonda), a 120, con 30° de
	// campo : el disco ocupa algo más de la mitad de la altura y se ve mucho
	// cielo. La zona donde la lente curva el fondo (el anillo de Einstein, unos
	// 10,5°) es mayor que el disco (unos 8,5°) y se ve también de frente. El
	// disco crece como 1/distancia y la lente como 1/√distancia : más cerca
	// de unas 80, el disco la vuelve a tapar (a 40, 24° frente a 18°); a 150,
	// el disco ocupa el 46 % y la franja de la lente es más ancha

	// mientras la inclinación se mueve, rayos a la mitad de resolución
	// (interpolados al componer); al parar, la entera en unos 4 fotogramas

	{ 120.0 , 0.0 , 0.0 , 30.0 , 0.3 , 2.0 , 2 , 256 , 8.0 , 25.0 } ,

	// 1024 columnas de ángulo × 256 filas de radio; remolinos de 1/16 de
	// vuelta, la temperatura en manchas de 1/32 que tiende a su perfil (0,6
	// por s : el fluido se calienta al caer) mientras la espiral y los
	// remolinos la arrastran

	{ 1024 , 256 , 30 , 15.0f , 64 , 0.8f , 0.1f , 0.6f , 32 , 0.0f , 20.0f , 1.0f / 15 } ,

	// el borde interior da la vuelta en 8 s; la espiral cae un 12 % de lo que
	// gira; frío en el borde (ultravioleta) y rojo junto al ojo, que llega
	// hasta r ≈ 4 (el naranja, hasta 5; el amarillo, hasta 8)

	{ 8.0f , 1.0f , 1.0f , 0.12f , 2.0f , 0.0f , 1.1f , 1.6f } ,

	{ 1.2f } ,

	// como el polvo de liquido : partículas de un blanco cálido, un punto de
	// un píxel, que flotan en remolinos de unos 11° (los de liquido) a
	// 0,6-1,7° por segundo. Viven de 30 a 60 s (aparecer y apagarse, 2 s,
	// cada una a su ritmo). 262 000 en un casquete de 32° (todo lo que se ve
	// con 30° de campo a través de la lente; más allá, junto a la sombra, su
	// luz media) : en pantalla, más o menos la densidad de liquido. Celdas de
	// 0,14° (unos 4 px) : 1 o 2 por celda. El flujo cruza el borde del
	// casquete : junto a él quedan un cordón denso y huecos, fuera de la
	// vista; al girar la cámara entran y la lente los curva en arcos (hebras).
	// Realzan la lente; y hay más por todo el cielo, ya
	// formadas desde el primer fotograma (12 hebras de 6 a 15°, la misma
	// forma que las de 20 a 50° de antes en pequeño : trazos curvos de 1 de
	// cada 24 partículas, unas 900 cada una, 1,5 veces más brillantes, que
	// se mueven en un 80 % con su centro : no se deshilachan); y 360
	// cúmulos, estrellas lejanas (1 de cada 4 partículas, unas 180 cada uno,
	// redondos, de 0,04°). Hebras y cúmulos brillan todos igual.
	// Colores : las sueltas y los cúmulos, del universo (blanco 30 %,
	// blanco azulado 22 %, azul 10 %, amarillo 8 %, naranja 10 %, rojo 8 %,
	// morado 8 %, rosa 4 %; sin verde); las hebras, blancas.
	// Hebras y cúmulos pasan enteros al otro lado del casquete. La luz de los puntos se reparte
	// en luz lineal (sin titileo al cruzar píxeles); 0,46 da la misma luz
	// total que el 0,65 de antes, que la repartía en valor de pantalla Aumento con tope de 10 (en el
	// anillo, el físico no tiene)

	{ 1u << 18 , 32.0f , 512 , 5.2f , 0.05f , 0.01f ,
		// hebras
		{ 24 , 12 , 6.0f , 15.0f , 7.5f , 18.0f , 40.0f , 40.0f , 3.0f , 1.5f , 0.8f } ,
		// cúmulos
		{ 4 , 360 , 0.04f , 1.0f , 60.0f , 60.0f , 3.0f } ,
		30.0f , 30.0f , 2.0f , 0.46f , 10.0f , 4.0f , 6.0f ,
		// colores : el blanco, la paleta y los pesos
		{ { 1.0f , 0.95f , 0.88f } , { { 0.80f , 0.88f , 1.0f } , { 0.45f , 0.62f , 1.0f } , { 1.0f , 0.93f , 0.68f } , { 1.0f , 0.62f , 0.30f } , { 1.0f , 0.40f , 0.36f } , { 0.72f , 0.48f , 1.0f } , { 1.0f , 0.50f , 0.82f } } , { 0.30f , 0.22f , 0.10f , 0.08f , 0.10f , 0.08f , 0.08f , 0.04f } } ,
		20.0f , 1.0f / 15 } ,

	// cada doble clic, chorros de 120 celdas/s (el viento de fondo, 15; el
	// giro junto a la ISCO, 128) en remolinos de 1/16 de vuelta; se calma en
	// 2,5 s; hasta 5 seguidos

	{ 2.5f , 5.0f , 120.0f , 64 , 1.5f , 4.0f } ,

	// los colores de Google (#4285F4 azul, #34A853 verde, #FBBC05 amarillo,
	// #EA4335 rojo), cada uno puro en su banda; con estos cortes, a los 6 s
	// cubren azul 29 %, verde 26 %, amarillo 25 %, rojo 20 % del disco

	{ { { 0.259f , 0.522f , 0.957f } , { 0.204f , 0.659f , 0.325f } , { 0.984f , 0.737f , 0.020f } , { 0.918f , 0.263f , 0.208f } } , { 2 , 1 , 2 , 5 } , { 1.5f , 2.87f , 5.38f } , 1.0f } ,

	1.0f / 30

} ;

static_assert(forma_agujero.fluido.ancho % 8 == 0 && forma_agujero.fluido.alto % 8 == 0 , "el fluido va en grupos de 8 × 8") ;

static_assert(forma_agujero.fluido.ancho % forma_agujero.fluido.escala == 0 , "el viento da la vuelta sin costura") ;

static_assert(forma_agujero.fluido.ancho % forma_agujero.fluido.escala_patron == 0 , "el patrón da la vuelta sin costura") ;

static_assert(forma_agujero.fluido.ancho % forma_agujero.agitacion.escala == 0 , "los chorros dan la vuelta sin costura") ;

}  // namespace shd
