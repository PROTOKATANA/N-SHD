# El agujero negro

Un agujero negro de **Kerr** (que gira, a = 0,9) con su **disco de acreción de fluido Navier-Stokes**.
Visto de frente, es un círculo de fluido que entra frío por el borde y cae en espiral hacia el ojo negro
calentándose, pintado por bandas con los colores de Google: de dentro afuera rojo, amarillo, verde y azul.
Visto desde el eje de giro, la sombra de Kerr es redonda.

## Cómo funciona

- **La luz** se traza hacia atrás desde la cámara, píxel a píxel, con la
  relatividad general de Kerr (Kerr-Schild cartesianas, hamiltoniano, RK4 con
  paso adaptativo): la sombra (aplanada por un lado por el giro), el anillo de
  fotones y el disco de detrás doblado por encima y por debajo. Con la cámara
  fija los rayos no cambian: el mapa se traza una vez por tamaño de ventana
  (unos 50 ms a 1600×853 con los subrayos de los bordes; con el gradiente
  exacto de la métrica y pasos más largos lejos del agujero) y cada
  fotograma solo lee el fluido en sus cruces.
- **El disco** es un fluido de verdad (Stable Fluids) en una rejilla de
  ángulo × logaritmo del radio (conforme: los remolinos siguen redondos), con
  la rotación kepleriana de Kerr encima: el giro diferencial estira los
  remolinos en vetas; además cae hacia el ojo (una espiral) y los remolinos
  pequeños se refuerzan (vorticity confinement), como en el fluido de nodos.
  Lleva la temperatura: fría en el borde y caliente junto al agujero, que
  tiende a su perfil mientras el flujo la arrastra.
- **La pintura** (`pintura_disco`): 9 bandas con los colores de Google (azul
  #4285F4, verde #34A853, amarillo #FBBC05, rojo #EA4335), cada banda de su
  color puro y con un filo oscuro entre una y otra. Las bandas de fuera
  ocupan mucho más disco que las de dentro: al pintar, la temperatura se
  estira a trozos (`repartir`) para que cada color ocupe más o menos un cuarto
  (azul 29 %, verde 26 %, amarillo 25 %, rojo 20 %). Los colores, cuántos
  niveles tiene cada uno y los cortes están en `forma_agujero.h`
  (`PinturaForma`). La gama anterior (violeta índigo, azules de V3, amarillos,
  naranja y rojo, con `escalonado` 0,6 y sin estirar) se quitó.
- **La cámara, a 120 masas con 30° de campo**: el disco ocupa algo más de la
  mitad de la altura y se ve mucho cielo. La zona donde la lente curva el
  fondo (el anillo de Einstein, unos 10,5°) es mayor que el disco (unos
  8,5°) y se ve desde todos los ángulos. El disco crece como 1/distancia y
  la lente como 1/√distancia: más cerca de unas 80 masas el disco la vuelve
  a tapar de frente (a 40, la tapaba).
- **Detrás, el polvo de liquido, curvado por el agujero** (`polvo_cielo`):
  262 000 partículas de colores (ver abajo) que flotan en remolinos y viven de
  30 a 60 s (fundido de 2 s, cada una a su ritmo), en el casquete de cielo
  que la cámara ve a través de la lente (32° alrededor de lo que queda
  detrás del agujero). Sigue a la cámara: al girarla, las que se quedan
  atrás pasan a la parte que entra en la vista. El flujo cruza el borde del
  casquete y junto a él deja un cordón denso y huecos, fuera de la vista; al
  girar entran y la lente los curva en arcos alrededor de la sombra (las
  «hebras»): no es físico, pero realza la lente y se queda a propósito.
  Y hay más hebras por todo el cielo, ya formadas desde el primer
  fotograma: 1 de cada 24 partículas va en una de 12 hebras, trazos curvos
  cortos (arcos de 6-15°) que nacen enteros, viven de 40 a 80 s (fundido
  de 3 s, cada una a su ritmo) y brillan 1,5 veces más que el polvo
  suelto. Sus partículas se reparten en el largo máximo (en las más
  cortas, las que sobran no se pintan): todas tienen la misma densidad y
  brillan igual.
- **Estrellas lejanas** (cúmulos): 360 grupos apretados de unas 180
  partículas, una gaussiana redonda de 0,04°, todos iguales: se ven como
  puntos más grandes y brillantes, y brillan todos lo mismo; viven de 60 a
  120 s.
- **Colores del universo**: cada partícula suelta y cada cúmulo (en cada
  vida : una estrella de un color) toma uno al azar: blanco 30 %, blanco
  azulado 22 %, azul 10 %, amarillo 8 %, naranja 10 %, rojo 8 %, morado
  8 %, rosa 4 % (`polvo.colores`; sin verde). Todos con la
  luminancia del blanco, en luz lineal: ninguno brilla más que otro. Las
  hebras, blancas. La luz de cada píxel se suma por canal (en coma fija:
  el oro se repite); el índice del color va en 4 bits de cada punto.
- **Cohesión**: cada hebra y cada cúmulo tiene un centro que el flujo lleva
  (un kernel pequeño); sus partículas se mueven con él (los cúmulos, del
  todo; las hebras, en un 80 %, `polvo.cohesion`): se doblan, pero no se
  deshacen. Si la cámara se mueve, pasan enteros al otro lado del casquete,
  y solo cuando ya no se ve nada de ellos (el mover mide cada fotograma
  hasta dónde llegan sus partículas): ni se trocean ni aparecen o
  desaparecen en pantalla. El polvo se asienta 20 s antes del primer
  fotograma: ya hay hebras y estrellas de todas las edades.
  La aureola de la lente (el anillo de Einstein, donde amplía tanto que los
  pocos puntos parecen girar alrededor) queda limpia: sin polvo donde el
  aumento pasa de ×6, con fundido desde ×4 (`polvo.aureola_desde/hasta`):
  del ancho de la franja oscura que ya se veía, y la lente se sigue notando
  a los lados. Con ×2-3 salía el doble de ancha y se comía ese efecto.
  La luz de los puntos se reparte entre píxeles en luz lineal y pasa a
  valor de pantalla al final: un punto no titila al cruzar de un píxel a
  otro (repartiendo el valor de pantalla, entre cuatro píxeles se veía unas
  5 veces más tenue). Cada fotograma se ordenan por
  celdas de una rejilla sobre el casquete, y cada píxel busca las que caen
  en él con la dirección de su rayo y lo que esa dirección cambia de un
  píxel al siguiente (la lente): puntos de un píxel, nítidos y del mismo
  tamaño en todas partes, repartidos entre los cuatro que tocan como en
  liquido. La lente los cambia de sitio y de brillo (el aumento físico, con
  tope 10). Donde la lente encoge el cielo (aumento μ < 1) siguen siendo
  puntos: se ve una parte fija de ellos, √μ, cada uno √μ veces más tenue
  (1/√μ veces más puntos que sin lente y la misma luz media: la lente
  conserva el brillo por ángulo sólido). Antes era su luz media, un gris
  liso que se veía como una capa opaca entre el anillo de Einstein y el
  disco (la media de un campo de puntos sueltos se ve unas 4 veces más
  clara que el campo). El cielo de fuera del casquete (junto a la sombra,
  donde la lente lo encoge tanto que los puntos ya casi no se verían), en
  negro. El disco y la sombra lo tapan. Calibrado con liquido (brillo medio 11 frente
  a 10). Sustituye a las estrellas.
- **Sin dientes (antialiasing)**: cada píxel promedia 4 submuestras. Dentro
  de una forma salen de interpolar el mapa de rayos; en sus bordes (donde el
  mapa cambia de naturaleza entre vecinos: el borde de la esfera, los del
  disco) se trazan 4 rayos de verdad por píxel, también mientras se mueve. El
  rayo que ya rozó el agujero deja de trazarse (será sombra igual): los
  bordes de la esfera, que son los rayos más largos, cuestan menos.
- **La esfera negra, limpia**: el rayo que pasa más cerca del centro que la
  ISCO (`kerr.rozar`) cuenta como sombra, sin disco ni cielo. Quita el anillo
  de fotones pegado a la sombra, que con el giro se veía como un reflejo en
  su lado izquierdo al inclinarlo. El disco y su imagen doblada por encima y
  por debajo no cambian (esos rayos no bajan de la ISCO).
- **Se gira con el ratón en todos los ejes** (`orbita`): con el botón
  izquierdo, la cámara orbita el agujero sobre sus propios ejes (x e y) y
  pasa por encima de los polos sin pararse; con el derecho, en horizontal,
  rueda sobre la vista (z). La orientación es una base 3D con inercia
  (velocidad angular que se frena sola al soltar). Como el agujero es
  simétrico alrededor de su eje, se parte en el azimut (gratis: solo gira el
  disco y el cielo al componer) y la base de trazado (la cámara llevada a
  azimut 0); si esta cambia, se vuelven a trazar los rayos, a media
  resolución mientras se mueve y la entera en unos 4 fotogramas al parar.
  `AGUJERO_ARRASTRE=<vx>,<vy>[,1]` simula un arrastre de los 3 a los 4 s
  (con `,1`, con el botón derecho).
- **Se mueve sola** (deriva, en `orbita`): sin arrastrar, la cámara avanza
  en x e y a 8°/s con un rumbo que gira al azar (hasta 25°/s, cambiando de
  idea cada ~3 s); nunca rueda (z). No repite camino y ve el agujero desde
  todos los ángulos por igual: en 2 h simuladas, cada cuarto de la esfera de
  puntos de vista se lleva entre el 24 y el 27 % del tiempo; a los 5 min ya
  ha pasado a menos de 20° del 96 % de los ángulos. Al orbitar con el botón
  izquierdo se para (la mueves tú) y al soltar vuelve en 2 s; mientras
  ruedas con el derecho sigue, por el mismo camino (lo rodado se descuenta
  de su rumbo: solo gira la imagen). El azar es determinista (splitmix64): el oro se
  repite. Coste: la cámara nunca está quieta, así que los rayos se trazan
  en cada fotograma (a media resolución): ~9,5 ms de GPU por fotograma en
  vez de ~2,5.
- **Doble clic: se revuelve el disco** (`agitacion`). Cada doble clic suma uno
  a lo agitado (hasta 5), que se calma solo (a 1/e en 2,5 s). En el fluido de
  verdad: el viento al que tiende la velocidad suma unos chorros (el
  rotacional de un ruido rápido, sin fuentes, otros en cada doble clic) con
  fuerza ∝ lo agitado, y la temperatura vuelve más despacio a sus bandas. Al
  calmarse, el giro de Kerr y la caída en espiral lo reordenan hacia el
  agujero. `AGUJERO_DOBLE_CLIC=3,3.3` simula dobles clics en esos segundos.

| Pieza | Qué hace |
| --- | --- |
| `motor/src/agujero/kerr` | Rust. La física de los rayos, la referencia del HLSL (probada). |
| `motor/src/agujero/orbita` | Rust. La cámara que se arrastra con inercia y se mueve sola por todos los ángulos (probada). |
| `motor/src/agujero/agitacion` | Rust. Lo agitado por los dobles clics, que se calma solo (probada). |
| `motor/src/agujero/guion` | Rust. Los ensayos sin ratón: arrastre, dobles clics y volcados (probada). |
| `motor/src/agujero/entrada` | Rust. Lo último que llegó de la interfaz: lo escribe quien integra y lo lee el hilo que pinta (probada). |
| `agujero` | La entrada para quien lo integra: une todas las piezas de abajo. |
| `pintura_disco` | Pura. De la temperatura a su color: el HLSL y lo mismo en C++ (probada). |
| `forma_agujero.h` | Todos los parámetros: giro, cámara, fluido, pintura, brillo. |
| `cbuffer_agujero` | El cbuffer «Agujero» y el mismo Kerr en HLSL (float). |
| `rayos_kerr` | El mapa de rayos, por bandas de filas, con los subrayos de sus bordes. |
| `campos_disco.h` | **Puerto**: la temperatura del disco, para quien la pinta. |
| `fluido_disco` | El fluido del disco (cumple `CamposDisco`). |
| `campo_cielo.h` | **Puerto**: la luz del cielo en un píxel (su dirección y su lente), para quien pinta. |
| `polvo_cielo` | El polvo del fondo (cumple `CampoCielo`): partículas en un casquete, ordenadas por celdas. |
| `estructuras_polvo` | HLSL sin estado: las hebras y los cúmulos (quién va en cada uno, su vida, dónde nacen) y el kernel de sus centros. |
| `paleta_polvo` | Pura: los colores del polvo en luz lineal, con la luminancia del blanco, y sus pesos (probada). |
| `componer` | Mapa + disco + cielo → luz; `tono_gpu` la pinta (con su propio cbuffer). |

Coste por fotograma (GPU, 1600×853, mínimos, en una GTX 1070): fluido 0,58 ms (Jacobi 0,35), polvo 0,18,
componer 0,73, tono 0,09; quieto, unos 1,6 ms. Al girar, además, los rayos: ~4-5 ms. El mapa de rayos entero,
unos 55 ms.

## Pruebas

```sh
cargo test -p shd                       # Kerr, órbita, agitación, guion, entrada
nucleo/agujero/pruebas/probar.sh        # la pintura y la paleta del polvo
```
