# RAYO y LEGO sobre los nodos

Pasada del 2026-11-03 sobre todo `nodos` (la gota, el líquido de los enlaces y el vórtice rígido),
siguiendo `RAYO`, `RAYO-CX`, `RAYO-RS` y `LEGO`. Objetivo: menos tiempo de GPU **sin cambiar lo
que se ve**, y piezas que se puedan cambiar por separado.

Máquina: GTX 1070, ventana de 800 × 600. La GPU estaba compartida con otras ventanas (del 26 al
82 % de uso sin nada del banco), así que lo que se compara son tandas alternas con el mismo banco,
mínimos y medianas.

## Resumen

| Qué (medianas) | Antes | Después |
| --- | --- | --- |
| Gota: la simulación | 0,86 ms | 0,62 ms |
| · presión de las partículas | 0,40 | 0,21 |
| · velocidad y cohesión | 0,097 | 0,056 |
| · campo | 0,100 | 0,083 |
| · vecinas | 0,008 | 0,052 |
| · canales: cerca, alcance y presión | 0,041 + 0,053 + 0,079 | 0,034 + 0,041 + 0,068 |
| Gota: pintar | 0,183 | 0,091 |
| Líquido de los enlaces, 9 nodos | 0,189 | 0,095 |
| **Escena de la gota** | **1,24** | **0,81** |
| Vórtice rígido (presión: 0,061 → 0,042) | 0,077 | 0,056 |
| **Escena del vórtice rígido** | **0,27** | **0,16** |
| **Malla de 16 × 16 nodos** (líquido 4,7 → 0,56) | **4,8** | **0,62** |
| La imagen | — | la misma (ver «Prueba») |

## Cómo se mide (los bancos que quedan)

- `SHD_BANCO_GPU=1` en las escenas de nodos del visor: lo que tarda en la GPU cada pasada y cada
  tramo que mide por dentro (`Pasada :: tramos` y `medir`), con consultas de duración. Mínimo,
  mediana y p95 cada 300 fotogramas. Un tramo que se abre varias veces por fotograma (los subpasos)
  se suma. Espera a la GPU al final de cada fotograma: solo para medir.
- `SHD_NODOS_MALLA=CxF`: un grafo grande (el vórtice y una malla de C × F nodos), para medir el
  líquido con tantos nodos como un grafo de verdad.
- El oro: con `AGUJERO_RELOJ` y volcados, la gota ahora se repite bit a bit (ver «Determinista»).
  16 fotogramas: los cuatro guiones de la gota (`1`, `arrastre`, `pared`, `cerca`) y el vórtice
  rígido, quieto y con su guion. Cada cambio que no debía cambiar la imagen se comparó con él byte
  a byte.
- La caché de shaders: el nombre de cada archivo es el hash de su HLSL. Desde una caché vacía, los
  mismos nombres son el mismo texto en cada kernel.

## Optimizaciones aplicadas

### 1 · Vecinas ordenadas por celda (§8, contiguo antes que disperso; §2, la estructura)

- **Cuello**: la presión era casi la mitad de la gota (0,41 ms en 12 lanzamientos). Cada celda de la
  rejilla de vecinas tenía sitio para 24 y cada partícula leía la posición de cada vecina con un
  salto por la memoria.
- **Cambio** (`gota_particulas.cc`): contar, sumar los prefijos (un grupo, en enteros), repartir,
  ordenar cada celda por el número de la partícula y reunir las posiciones en ese orden. La presión y
  la velocidad se calculan por puesto de la lista: las vecinas de un grupo de hilos están juntas.
- **Medida**: presión 0,40 → 0,21 ms; velocidad 0,097 → 0,056; las vecinas pasan de 0,008 a 0,052
  (seis lanzamientos por subpaso). La simulación, 0,86 → 0,66.
- **Precio**: seis kernels en vez de dos; 3 buffers más.
- **Prueba**: cambia el resultado en los últimos decimales (otro orden de suma) y quita el tope de 24
  por celda. Medido con varios tamaños de ventana: trozos sueltos al arrastrar, antes 4, 12 y 11, y
  después 12, 11 y 8; el rojo mínimo con 12 clics seguidos, antes 2,1-3,0 % y después 2,2-2,9 %; el
  reparto de colores, igual. Es ruido de una simulación caótica.

### 2 · Determinista (RAYO, «el determinismo no se cambia por velocidad»)

- Con las vecinas ordenadas, el orden de llegada de los atómicos ya no cambia las sumas. Quedaba una
  carrera: el calor de cada partícula se mezclaba con el de sus vecinas mientras ellas lo
  escribían. Ahora se lee el del fotograma anterior (una pareja de buffers).
- **Prueba**: dos ejecuciones, 0 píxeles distintos en los 16 fotogramas del oro, también con clics y
  arrastres. Antes, 26.140 píxeles distintos al fotograma 100.
- Es lo que permite comprobar byte a byte todo lo que viene después.

### 3 · 64 hilos por grupo en las partículas (§10, reparto del trabajo)

- **Cuello**: 4.096 partículas en grupos de 256 son 16 grupos para 15 unidades: una hace el doble.
- **Cambio**: grupos de 64.
- **Medida**: presión 0,411 → 0,389 ms; campo 0,109 → 0,088.
- **Prueba**: el mismo cálculo por hilo.

### 4 · Los canales y el Jacobi del vórtice, en bloques (§8, bloques que caben)

- **Cuello**: cerca y alcance hacían 8 pasadas por la rejilla de 256², la presión de los canales 20
  y la del vórtice 30 (128²): casi todo era lo fijo de cada lanzamiento.
- **Cambio** (`comun/bloque_hlsl.h`, como el Jacobi del disco del agujero): cada grupo carga su
  bloque con un borde de K en memoria compartida, hace ahí K iteraciones y escribe el centro.
  Canales: bloques de 32, K = 4 (cerca y alcance) y 5 (presión). Vórtice: bloques de 16, K = 5.
- **Medida**: cerca 0,042 → 0,035 ms, alcance 0,054 → 0,042, presión de los canales 0,080 → 0,070,
  presión del vórtice 0,061 → 0,042.
- **Prueba**: cada celda suma en el mismo orden: el oro, idéntico.
- **Variantes medidas**: canales con bloques de 16, 0,039 / 0,043 / 0,076; presión de los canales con
  K = 10, 0,076. Vórtice: K = 3, lo mismo que 5; K = 6, 0,050; K = 10, 0,061; bloques de 32 con
  K = 6, 0,045, y con K = 10, 0,050.

### 5 · Pintar solo lo que cuenta (§7, ¿debe ejecutarse?)

- **Cuello**: cada píxel del cuadrado de la vista calculaba la silueta de los enlaces (hasta 12 gotas
  y 16 tubos, dos veces) y la masa de alrededor, aunque no hubiera nada.
- **Cambio** (`gota_pintura.cc`): sin celdas de líquido alrededor el canal es 0 exacto (su frente
  queda a más de 998 radios), y sin gota la masa de alrededor no cambia nada (0 por lo que sea es
  0): no se calculan.
- **Medida**: pintar la gota, 0,183 → 0,091 ms.
- **Prueba**: el oro, idéntico.

### 6 · Las celdas del líquido de los enlaces, las más pequeñas que caben (§2 y §8)

- **Cuello**: el líquido de los enlaces (también el del grafo de CORTEX) tenía celdas del tamaño de la
  influencia del nodo mayor: con el vórtice cerca, cada píxel recorría casi todo.
- **Cambio** (`motor/src/nodos/liquido/rejilla.rs`): primero 32 × 32 celdas; si las listas no caben
  en el cbuffer, el doble de grandes. Las celdas de un enlace se calculan fila a fila con el corte de
  su cápsula (antes, una raíz por celda), y una sola pasada por la geometría apunta los pares celda y
  elemento en un búfer de su hilo que se reutiliza.
- **Medida (GPU)**: 9 nodos, 0,189 → 0,095 ms; malla de 12 × 9, 2,64 → 0,31; malla de 16 × 16, 4,8 →
  0,57.
- **Medida (CPU, el reparto)**: 9 nodos, de 0,8 a 27 µs; 150 nodos, de 31 a 83; 256, de 53 a 116.
- **Precio**: esos µs de CPU por fotograma.
- **Prueba**: el oro, idéntico; las mallas de 12 × 9 y 16 × 16, la misma imagen byte a byte; la prueba
  de que la rejilla no cambia lo que se pinta (12 configuraciones), igual; y una prueba nueva: el
  corte por filas coge todas las celdas que cogía probarlas una a una, en 3.000 segmentos.

## Descartado (con su medida)

- **Juntar kernels de las vecinas** (limpiar dentro de los prefijos; reunir dentro de ordenar): la
  gota, 0,670 frente a 0,664 ms en tres tandas alternas. Reunir dentro de ordenar copia en serie
  por celda.
- **Descartar los píxeles vacíos del líquido de los enlaces**: lo mismo (0,191 ms). El coste era
  el cálculo, no escribir.

## LEGO

| Pieza | Responsabilidad | Lee | Escribe |
| --- | --- | --- | --- |
| `gota_comun` | El HLSL de delante de cada shader de la gota: medidas, cbuffer, núcleos del SPH, choque, siluetas, vecinas | — | — |
| `gota_particulas` | El PBF: barrer, medir, predecir, vecinas, presión y velocidad | el cbuffer | estado, calor (al sembrar), medida, vecinas |
| `gota_campo` | El calor de cada partícula y su masa, calor y velocidad en el campo | partículas | campo, calor |
| `gota_canales` | El fluido de los canales | campo | máscara, alcance, cerca, calor |
| `gota_liquido_hlsl` | Dónde hay líquido: el contorno de la gota y el de los canales, la misma cuenta para la pintura y los trozos sueltos | — | — |
| `gota_sueltos` | Qué líquido está unido al cuerpo de la gota | campo y canales | unido |
| `gota_pintura` | El contorno, los canales y las bandas, sin los trozos sueltos | campo, canales y unido | la imagen |
| `pasada_gota` | Las une: el cbuffer, el orden y las transiciones | — | — |
| `comun/bloque_hlsl` | Iterar un estencil en bloques (los canales) | — | — |
| `gota/` (Rust) | `contactos`, `retraso` y `cercano`; `EstadoGota` los une | — | — |
| `liquido/rejilla` (Rust) | La rejilla de culling del líquido | — | — |
| `choque` (Rust) | Que los nodos no se toquen (el vórtice no choca) | los nodos | los nodos |
| `fisica` (Rust) | La física de las pestañas de CORTEX: layout, colocación y órdenes del ratón | los enlaces, grupos y radios | las posiciones |

- Ninguna pieza conoce a otra: la pasada les da los buffers que leen (`Lecturas`), y cada una dice
  los que escribe. Interfaces abstractas solo donde hay algo que sustituir (el fluido del vórtice ya
  lo tenía: `Fluido`); aquí, clases concretas con cabecera clara (LEGO, proporcionalidad).
- Los kernels se movieron tal cual: de 27 shaders de la gota cambian 2 claves de la caché, los de la
  pintura, que reciben ahora las medidas en el mismo orden que los kernels. El oro, idéntico.
- Cada cabecera nueva o tocada compila sola, incluida dos veces, con `-Wall -Wextra -Wpedantic
  -Werror` y la línea de órdenes del proyecto.
- En Rust, cada pieza con sus pruebas; las del líquido cercano prueban su pieza directamente.
  Las 68 pruebas pasan, y Clippy sin avisos.

## Prueba de que la imagen no cambió

- El oro (16 fotogramas): idéntico tras los bloques, la pintura, el líquido, la partición en piezas
  (C++ y Rust) y el Jacobi del vórtice.
- La ordenación de las vecinas cambia los últimos decimales de la gota (ver 1): medido como ruido.
- Cuatro guiones de 100 s simulados sin errores; sin trozos sueltos a 30 y a 144 fps; el vórtice
  rígido, en el oro.

## Errores míos por el camino

- La primera versión de la ordenación dejó la GPU en negro: `&&` en HLSL evalúa las dos partes, y
  al ordenar una celda que empieza en el puesto 0 se leía `lista[-1]`. Se vio en el primer volcado.
- Al comprobar que las cabeceras compilan solas, la primera vez el estado de salida era el de un
  `head` y no el del compilador: decía que compilaban cuando fallaban por las rutas. Repetido con
  la línea de órdenes completa.

## Después: sin trozos sueltos (2026-11-03 y 04)

Un cambio que no es una optimización (cambia lo que se ve), con RAYO aplicado a lo que cuesta. Banco del visor (`SHD_BANCO_GPU`, guion `arrastre`, 1280 × 800), tandas alternas.

### Sin trozos sueltos (`gota_sueltos`)

- **Qué**: solo se pinta el líquido unido al cuerpo de la gota. `marcar` mira en la rejilla de los
  canales qué celdas tienen líquido (con la cuenta de la pintura, `gota_liquido_hlsl.h`) y `unir`
  lo une desde el centro en un solo grupo, con una celda por bit (§2, conjunto de bits) y el relleno
  de tramos de Kogge-Stone (§9, instrucciones de bits): 0,031 ms.
- **Coste**: la escena de la gota, de 0,759 a 0,940 ms (simular 0,637 → 0,810; pintar 0,122 → 0,130).
- **Pintar, la vía rápida** (§17, haz rápido el caso común): casi siempre todo el líquido de
  alrededor es del cuerpo, o no hay; entonces no se pesan 16 celdas. De 0,172 a 0,130 ms.
- **`marcar`, los cuatro puntos solo donde el centro no tiene líquido** (§7): de 0,161 a 0,139 ms.
- **Prueba**: los 811 fotogramas de los cuatro guiones, idénticos byte a byte a la versión sin
  ninguna de las dos (pintado lento, cinco puntos en cada celda); el oro, idéntico a antes de los
  trozos. Trozos, contados con la gota pintada en blanco (con colores, el contador ve trozos falsos
  en los anillos de los huecos y en las líneas de las bandas): de 10 fotogramas a 0; quedan 3 marcas
  que son el borde suavizado sobre el canto de un nodo, y que con colores no se ven.

### Descartado (con su medida)

- **Saltar en `marcar` también lo lejano, con una cota a ojo** (poca masa y sin alcance en el
  centro): 0,118 ms, pero cambiaba 7 píxeles en 2 de 811 fotogramas. No es una optimización.
- **La misma, con cotas exactas** (la mayor masa de las 3 × 3 celdas que se mezclan, y ninguna
  celda de canal alcanzada en las que lee `canal_en`): el mismo resultado, pero 0,144 ms frente a
  0,139: leer las cotas cuesta más que los cuatro puntos que ahorra.

### Errores míos por el camino

- La primera máscara añadía un borde de una celda alrededor del cuerpo para no cortar su
  contorno, y dejaba motas tenues y una línea de 1 px junto a él. Se vio pintando lo cortado en
  magenta. Ahora cada píxel decide por las celdas de líquido de alrededor.
- Mirar cuatro puntos por celda también para unir juntaba al cuerpo un punto a 1-2 px por el hueco:
  para unir cuenta solo el centro.
- La primera vía rápida del pintado devolvía 1 donde la cuenta entera devuelve 0 (las únicas
  celdas de líquido, en las esquinas, pesan menos de 1e-5): un píxel distinto en 811 fotogramas. Se
  vio al verificar con RAYO, comparando con la versión sin atajos.
## Después: `preparar` sin avanzar (2026-11-04)

Un arreglo, no una optimización, con RAYO aplicado a lo que mide.

- **Qué**: quien integra puede preparar y abandonar el fotograma (sin imagen libre). La gota y el
  vórtice avanzaban su reloj y lo que se movieron los nodos en `preparar`: ese tiempo se perdía.
  Ahora avanza al subir, y el contrato lo dice en `pasada.h`.
- **Medida** (CORTEX, release, con la física moviendo los nodos): se abandonan 126 de 217 fotogramas
  preparados en 2 s; la gota simulaba 41-90 subpasos por segundo, y ahora 119-120, los de siempre.
- **Prueba**: el oro, idéntico (en el visor nada se abandona).

## Después: la gota envuelta (2026-11-04)

La gota con su física, envuelta en un anillo del líquido de los enlaces que sigue su forma (sin
vórtice libre). Banco del visor (`SHD_BANCO_GPU`, `SHD_GOTA_ENVUELTA`, guion `arrastre`,
1280 × 800), tres tandas alternas; exactitud, los 811 fotogramas de los cuatro guiones, envuelta y
libre, byte a byte contra la versión anterior.

### LEGO

- **El anillo sigue a la gota por una interfaz** (`DistanciaGota`, distancia_gota.h): el líquido de
  los enlaces solo conoce «lo lejos que queda la gota», no la gota; `PasadaGota` la da con la
  distancia que ya calculan sus canales (`cerca`). Se inyecta al crear el líquido (gpu_graph.cc, el
  visor); sin ella, el líquido es el de siempre, el mismo shader.
- **Cada cosa, una vez**: el grosor del anillo es `ANILLO` (liquido); lo usan el relleno del
  líquido y los bordes de la gota (contactos). La vista se calcula en contactos, el único que la usa.
- **Fuera lo que el anillo hizo inútil**: el círculo aplastado contra la vista (su caja, las
  esquinas redondeadas, el centrado dentro de ella) y la pared que contenía la gota.
- **Sin pruebas de C++**: N-SHD no tiene arnés con GPU; un falso de `DistanciaGota` exigiría montar
  uno, y no compensa por una interfaz de un método. Lo de la CPU, con pruebas de Rust (cercano y
  contactos: canales solo propios, círculo del vórtice hundido, bordes a lo que asoma el anillo).

### RAYO

- **Coste de la gota envuelta** (frente a la libre): simular 0,754 ms (0,83), pintar 0,097 (0,13),
  el líquido 0,139 (0,133). Menos canales: solo los suyos, y un 70 % más cortos.
- **Ir directo al anillo** (§7, no hacer trabajo inútil): el nodo envuelto calculaba su círculo y lo
  tiraba. El líquido, de 0,140 a 0,139 ms; la misma imagen.
- **Descartado, saltar los bloques sin líquido** en Jacobi: un bloque sin canal ni gota da 0, y se
  podía escribir sin iterar (la misma imagen bit a bit). Envuelta, 0,070 → 0,070 ms; libre, 0,076 →
  0,078. Lo pesan los lanzamientos, no esos bloques. Fuera; y por lo mismo, no se probó en `alcance`.
- **Donde está el tiempo**: la presión de las partículas (0,212 ms), el líquido de los enlaces
  (0,139), los trozos sueltos (0,103) y el campo (0,082). La presión es la física de la gota, ya
  medida antes en este informe.

## Después: solo la gota envuelta (2026-11-05)

Los nodos quedan con una sola versión, la definitiva: la gota envuelta. Las de prueba se quitan.
Lo que este informe cuenta antes del vórtice rígido y de la gota libre es historia: esas piezas ya no
existen.

### Sin el vórtice rígido

- **Qué**: fuera `PasadaVortice`, su fluido (`fluido_gpu`, la interfaz `Fluido`, y `fluido_stam`)
  y, en Rust, `vortice` y `fluido`, con su parte del puente (`VorticeGpu`, `EstadoVortice` y el
  diagnóstico y la huella del fluido). Unas 1800 líneas. `comun/bloque_hlsl` se queda, porque lo
  usan los canales de la gota.
- **Visor**: una sola escena de nodos, `nodos`. La escena `gota` y la tecla `3` ya no existen.
- **Prueba**: la gota envuelta, idéntica byte a byte a main en los 811 fotogramas de los cuatro
  guiones.

### Sin la gota libre

- **Qué**: fuera `Escena :: vortice_libre` y `SHD_GOTA_ENVUELTA`. La gota va siempre envuelta, y
  `PasadaLiquido` exige la gota (`const DistanciaGota &`): su shader ya no tiene la variante sin
  anillo (`ANILLO_GOTA`).
- **Lo que solo usaba la libre**, también fuera:
  - En el cbuffer de la gota, las 12 gotas de los nodos cercanos (queda una, el círculo del vórtice,
    `circulo`) y `funde`, la fusión gradual con un tubo ajeno. Envuelta, sus canales son solo sus
    enlaces, y cada uno se funde del todo.
  - En `cercano`, la búsqueda de lo ajeno cercano: `distancia_al_centro`, `CERCA_TUBO`, `FUNDE_HASTA` y
    `FUNDE_BANDA`.
  - En el líquido de los enlaces, lo que se hundía la gota del vórtice (`nodos[].w`, ahora 0).
    `gota_vortice_libre` se llama ahora `gota_vortice_hundida`: es lo que se hunde el círculo del
    vórtice en la silueta de sus canales, para que su líquido no llene el anillo.
- **Prueba**: los 811 fotogramas de los cuatro guiones, idénticos byte a byte a la gota envuelta de
  main, también con todo el C++ de los nodos recompilado sobre la cabecera nueva de cxx. 63 pruebas de
  Rust (las de la libre, fuera; nuevas: sin vórtice no hay anillo, y sin un vórtice bien no hay
  canales).
- **Para quien integra**: `Escena` pierde su último campo, `vortice_libre`, y `PasadaLiquido` recibe la gota
  primero: `PasadaLiquido(* gota , con_rejilla)`.

### Un grafo sin vórtice

- **Qué**: `PasadaLiquido` no se pintaba hasta tener atada la distancia de la gota, y sin vórtice
  la gota no la crea nunca: el grafo salía vacío (1 color en cada fotograma, solo el fondo). Ahora,
  sin vórtice, ata una distancia vacía de un elemento, que nadie lee porque ningún nodo es anillo.
- **Prueba**: con el vórtice a 0 en el visor (un cambio local, fuera del commit), de 1 color a 2918
  en cada fotograma, con los nueve nodos y sus enlaces. Con vórtice, los 811 fotogramas idénticos.
  La puerta no corre la GPU, así que esto no lo vigila ninguna prueba.

## Después: los nodos no se tocan (2026-11-05)

- **Qué**: `separar_nodos` (`choque`), el choque del antiguo motor de fuerzas de CORTEX: cada par de
  nodos más cerca que la suma de sus radios por 1,5 se aparta hasta esa distancia, en hasta 8 pasadas
  por llamada. El vórtice no choca. Dos cambios sobre el de CORTEX: contra un nodo que el borde acaba
  de devolver cede el otro entero (antes, a medias, y tras 8 pasadas seguían a 29,92 de 30), y una
  holgura de una milésima, porque apartar justo hasta tocarse dejaba redondeos que no acababan nunca.
- **Medida** (CPU, release, malla de 16 × 16 con el vórtice): 21 µs por fotograma sin nadie
  tocándose, y 168 µs (p95 200) con un nodo arrastrado encima de otro. Con los 9 nodos del menú, nada.
- **Prueba**: los guiones `1`, `arrastre` y `cerca`, idénticos byte a byte: ningún par se toca. En
  `pared` los tres nodos se tocaban; ahora la pared se coloca a 48, tan junta como deja el choque, y
  sus gotas siguen uniéndose. 9 pruebas de Rust nuevas.

## Después: la física de las pestañas de CORTEX (2026-11-06)

- **Qué**: de las pestañas de CORTEX (rama `pestanas-grafo`) solo sobrevive la vista general en grafo; aquí llega su
  física, copiada tal cual de `N-CNX/grafo/motor/src/fisica`: `layout`, `colocacion`, `ordenes` y lo común de `mod`.
  Lo que cambia: la repulsión de Barnes-Hut va en orden y no en paralelo (sin rayon), `Lugar` vive en `colocacion`, y
  se quedan en CORTEX el hilo (tokio y el almacén de la disposición) y el trait del hub. `FisicaGrafo` hace lo del
  hilo, un paso cada vez, para C++. El líquido no cambia: como en CORTEX, el de un grupo se funde con el de otro.
- **Prueba**: las 13 pruebas de CORTEX de esas piezas pasan aquí (las 2 del hilo se quedan allí). Con las 40 pestañas
  de la demostración, la colocación y un arrastre de 60 pasos con su enfriado dan las mismas posiciones bit a bit que
  el motor de CORTEX (huellas `dad02733f061fac0` y `0c49aad48903c128` en los dos). Los cuatro guiones de los nodos,
  811 de 811 fotogramas idénticos a main. En el visor, la escena `pestanas` con el guion, mirada fotograma a fotograma.
- **Medida** (CPU, release): con las 40 pestañas, 6,2 µs por paso de mediana (p95 9,5) arrastrando un nodo; ver la
  pasada de RAYO que sigue.
- **Un primer intento descartado**: una física propia y un líquido por grupos (los enlaces entre grupos quedaban como
  rayas rectas). Se pidió la de CORTEX, con su mismo comportamiento.

### RAYO y LEGO sobre la física de las pestañas (2026-11-06)

Máquina: 32 hilos, release, un núcleo fijo (`taskset`). La CPU con un banco fuera del repositorio: colocar 15 veces, y
un arrastre de 600 pasos repetido 5 veces, cada paso medido, con un contador de reservas en el asignador. Grafos: la
demostración de CORTEX (40 nodos) y la misma seis veces (240, cerca del tope de 256 del líquido). La GPU, con
`SHD_BANCO_GPU` a 800 × 600, 900 fotogramas.

**Dónde está el tiempo** (§1):

| | 40 nodos | 240 nodos |
| --- | --- | --- |
| Un paso arrastrando (mín / mediana / p95) | 4,1 / 6,2 / 9,5 µs | 227 / 243 / 248 µs |
| Colocar, asentar de un tirón (mediana) | 2,1 ms | 58,6 ms |
| Reservas por paso | 14 | 16 |
| GPU: líquido de los enlaces / gota | 0,13 / 0,001 ms | — |

- Quieta, la física no da pasos: 0 µs. Con las 40 pestañas, un paso es el 0,04 % de un fotograma de 16,7 ms; por Amdahl
  (§1) no hay nada que ganar en el visor. Las reservas tampoco pesan.
- Con 240 nodos, `perf`: el choque (`apartar`, O(n²) en hasta 9 pasadas) se lleva el 61 % y la repulsión de Barnes-Hut
  el 34 %.
- La gota sin vórtice no simula (0,001 ms): ya se salta lo que no debe ejecutarse (§7).

**Medido y no aplicado: saltar de golpe a la siguiente pareja que se toca** (§9, vectorización; §7):

- **Cambio**: `apartar` busca el siguiente `j` que toca a `i` mirando de 8 en 8 sin salir a medias, para que el
  compilador lo vectorice, y trata esa pareja igual que antes. La búsqueda solo lee y hace las mismas operaciones, así
  que encuentra las mismas parejas en el mismo orden.
- **Medida**: con 240 nodos, un paso de 243 a 150 µs (−38 %) y colocar de 58,6 a 35,6 ms; con 40, nada (6,2 → 6,0 µs).
  Tres tandas alternas, dispersión por debajo del 2 %.
- **Prueba**: las mismas huellas bit a bit, la colocación (`dad02733f061fac0`, la de CORTEX) y el arrastre, con 40 y
  240 nodos.
- **Por qué no se aplica**: con las 40 pestañas no gana nada (§18, optimizar código frío), y aparta la copia del texto
  de CORTEX. Queda en «Lo que pide una decisión».

**LEGO**:

- **El visor, una escena por responsabilidad**: `EscenaNodos` llevaba también las pestañas, con un indicador y ramas en
  `avanzar`. Ahora son dos escenas, `EscenaNodos` (el vórtice y sus guiones) y `EscenaPestanas` (la demostración de
  CORTEX con su física), y lo que dibuja un grafo es una pieza aparte que usan las dos, `PintorGrafo`: las dos pasadas,
  el cbuffer de la cámara y el banco. Las dos escenas solo mueven los nodos y le dan el fotograma. La física ya no es un
  `std :: optional` que siempre está: se crea con los nodos asentados. `en_tramo` es el `tramo` de los guiones,
  común a las dos.
- **`caja`**, que solo usaban las pruebas, vive en ellas: la de verdad es de la cámara de CORTEX, que se queda allí.
- **No se toca lo copiado**: `Layout` deja sus campos públicos y `ordenes` los cambia directamente, y el choque de la
  física (`layout::apartar`) y el de los nodos (`choque`) son dos versiones del mismo choque del antiguo motor de CORTEX.
  Separarlo sería rediseñar la copia (ver «Lo que pide una decisión»).
- **Prueba**: los 811 fotogramas de los cuatro guiones de los nodos y 121 de las pestañas con su guion (cada 5 hasta
  600), idénticos byte a byte a antes de partir el visor. El visor compila sin avisos con `-Wall -Wextra -Wpedantic
  -Werror`, con GCC y con clang. La puerta, con sus 87 pruebas, pasa.

**Encontrado fuera de esta pasada**: con volcados (`AGUJERO_VOLCAR`, sin presentar), el bucle del visor no llama a
`FinishFrame`, así que el montón de subidas de Diligent no se vacía: 56,29 MB tras 900 fotogramas, justo 900 cbuffers
del líquido de 64 KiB. Pasa también en main. Solo afecta a los volcados y al banco, que miden con ese montón creciendo.

## Lo que pide una decisión

1. **El reparto del líquido en la CPU** (6): de 26 a 63 µs más por fotograma a cambio de hasta
   4,2 ms de GPU. Si pesara, se puede tener una celda algo más grande (con celdas 3 veces menores que
   antes, 0,124 ms de GPU y menos CPU).
2. **El rojo mínimo** con 12 clics seguidos sigue entre el 2 y el 3 %, antes y después.
3. **Un banco de regresión de los trozos**: el contador y los guiones que los cuentan no están en el
   repositorio, y la puerta (`scripts/check.ps1`) no corre la GPU. Llevarlos al visor (una opción
   que cuente los trozos de la gota en blanco y falle si hay alguno) los vigilaría, a cambio de más
   código de pruebas en el visor.
4. **El choque de la física de las pestañas, vectorizado**: con 240 nodos, de 243 a 150 µs por paso y de 58,6 a 35,6 ms
   al colocar, con el mismo resultado bit a bit; con 40, nada. Cuesta unas 30 líneas y que la copia ya no sea el texto
   de CORTEX (si CORTEX usara la de N-SHD, lo ganaría también). Compensa si un integrador pinta grafos de cientos de
   nodos en el hilo que pinta.
5. **`FinishFrame` en los volcados del visor**: una línea en el bucle, en su propia rama. Hay que comprobar que los
   volcados siguen idénticos byte a byte.
6. **Un solo choque**: `choque` (los nodos) y `layout::apartar` (las pestañas) vienen del mismo choque de CORTEX, con
   dos cambios en el primero. Unirlos tocaría la copia y cambiaría el resultado de uno de los dos.
