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
| `comun/bloque_hlsl` | Iterar un estencil en bloques | — | — |
| `gota/` (Rust) | `contactos`, `retraso` y `cercano`; `EstadoGota` los une | — | — |
| `liquido/rejilla` (Rust) | La rejilla de culling del líquido | — | — |

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

## Lo que pide una decisión

1. **El reparto del líquido en la CPU** (6): de 26 a 63 µs más por fotograma a cambio de hasta
   4,2 ms de GPU. Si pesara, se puede tener una celda algo más grande (con celdas 3 veces menores que
   antes, 0,124 ms de GPU y menos CPU).
2. **El rojo mínimo** con 12 clics seguidos sigue entre el 2 y el 3 %, antes y después.
3. **Un banco de regresión de los trozos**: el contador y los guiones que los cuentan no están en el
   repositorio, y la puerta (`scripts/check.ps1`) no corre la GPU. Llevarlos al visor (una opción
   que cuente los trozos de la gota en blanco y falle si hay alguno) los vigilaría, a cambio de más
   código de pruebas en el visor.
