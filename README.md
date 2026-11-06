# N-SHD

Galería de shaders sobre la GPU, sin depender de ninguna interfaz. Entra un dispositivo Vulkan de
DiligentCore y salen píxeles en el objetivo que tenga fijado el contexto.

**Estado:** dos familias, el agujero negro y los nodos, probadas en Linux con Vulkan. `visor/` las
enseña en una ventana propia, y el primer integrador real es xozen (CORTEX).

N-SHD es una biblioteca: no sabe nada de Flutter, de Dart ni de `rinf`. No crea el dispositivo ni conoce
la ventana. Cada proyecto pone el pegamento que lo une a su interfaz.

```
motor/    la lógica, en Rust: lo que no necesita GPU y se prueba con `cargo test`.
nucleo/   la GPU, en C++ sobre DiligentCore: compute shaders y pasadas de dibujo.
visor/    la galería en una ventana Vulkan, sin Flutter: el pegamento mínimo.
```

Entre `motor` y `nucleo`, `cxx`: las estructuras y las firmas se declaran una vez, en los
`src/*/puente.rs`, y cxx genera los dos lados.

## La galería

**`agujero`:** un agujero negro de Kerr. Traza los rayos con la métrica real, simula el fluido del disco
y el polvo del cielo, y los compone con su tono. La cámara orbita con inercia y el doble clic agita el
disco. Cómo funciona, en `nucleo/agujero/README.md`.

**`nodos`:** dos pasadas para dibujar un grafo.

- `PasadaLiquido`: los enlaces como un líquido. Es un campo de distancia evaluado en un quad, con una
  rejilla de culling de celdas tan pequeñas como quepan. Admite 256 nodos y 512 enlaces, porque es para un menú.
- `PasadaGota`: el vórtice, un nodo que es un remolino de líquido, sin pared; al pulsarlo, se agita. Son 4.096 partículas que resuelven
  Navier-Stokes (Position Based Fluids): se estira, choca con otros nodos y con los bordes de la vista, y se
  recompone. El líquido vive en el marco de su nodo: arrastrarlo no lo separa del enlace, y solo se mece el borde. El
  color lo lleva el propio líquido: un golpe o un clic lo mezclan, y se asienta poco a poco. Un nodo que se acerca la
  atrae y la gota lo envuelve, sin cubrir su círculo. Sus canales son solo sus enlaces: los llena un segundo fluido, en
  una rejilla, con la forma elástica del líquido de los enlaces, y entra por cada uno hasta donde el canal lo deja. Lo
  ajeno que pasa cerca o por debajo no entra. Cada canal se funde con la gota como el líquido de los enlaces con sus
  nodos, y sale con el color de su borde, para que se vean como un mismo líquido. Solo se pinta el líquido.
  La gota va envuelta: `PasadaLiquido` le pinta alrededor un anillo que sigue su forma, en los bordes, en las esquinas
  y cuando un nodo la atraviesa, porque lee de la gota lo lejos que queda (`DistanciaGota`). Los bordes salen de
  `Escena`: sin `ancho` y `alto`, no hay, y la gota choca con ellos a lo que asoma el anillo.
  Va en piezas (partículas, campo, canales y pintura; `nucleo/nodos/gota_*`), y con `AGUJERO_RELOJ` se repite bit a
  bit. Cómo se midió y qué se cambió, en `nucleo/nodos/RAYO-LEGO.md`.

Los nodos no se tocan: `separar_nodos` (`shd::nodos::choque`, también desde C++) aparta cada par que queda más cerca
que la suma de sus radios por `SEPARACION` (1,5), con las gotas de los dos apenas rozándose. El arrastrado no se mueve, y
un nodo que estaba en la vista no sale de ella por un choque. El vórtice no choca: los nodos atraviesan la gota y la
deforman. Quien integra lo llama en cada fotograma, después de mover los nodos y antes de preparar las pasadas.

**La física de las pestañas** (`shd::nodos::fisica`): la de fuerzas del grafo de CORTEX, copiada tal cual de su rama
de las pestañas. Es la de d3-force (Barnes-Hut, muelles, centro y un calor que se enfría) con una atracción de cada nodo
al centro de su grupo, y fuerzas que se ajustan al número de nodos (`fuerzas_para`). Sale asentada de un tirón
(`colocacion::colocar`, o donde se guardó cada nodo). Al arrastrar un nodo (`ordenes::llevar`) se queda clavado donde
se suelta y los demás apenas se mueven; `liberar` lo devuelve a la física. Ningún nodo sale de los bordes que se le
dan (`Limites`), y no se tocan, con su propio choque. Lo de la aplicación se queda en ella: el hilo que la hace correr
y dónde se guarda la disposición. Desde C++, `FisicaGrafo` da un paso cada vez que se le pide (`visor/visor.cc`,
`mover_pestanas`). Da lo mismo que en CORTEX, bit a bit.

## Integrar N-SHD

- **Rust:** depende del crate `shd` (`motor/`). Si la interfaz va por otro lado, traduce lo que le llegue a
  `shd::agujero::entrada`: la vista, el cursor y los dobles clics.
- **C++:** añade DiligentCore, dice en `SHD_CXX` dónde deja `cxx` lo que genera y hace `add_subdirectory` de
  `nucleo`. `shd_nucleo` debe compilar después de cargo, que corre el `build.rs` de `motor`. Rust se enlaza:
  el crate que lo use debe nombrar algo de `shd`, o el enlazador lo dejará fuera (`visor/src/lib.rs`).
- **`shd::Gpu`:** lo que presta quien integra.

  | Campo | Qué es |
  |---|---|
  | `device`, `ctx` | El dispositivo y el contexto. Se pinta en el hilo de `ctx`. |
  | `mascara` | El `ImmediateContextMask` de lo que se crea. Con varios contextos, solo el bit de `ctx`. |
  | `cache` | La carpeta del SPIR-V de cada shader. Vacía: se compila en cada arranque. |
  | `aviso` | Por dónde llegan lo que no compila y los diagnósticos. |

- **Formato:** los shaders pintan en un objetivo `RGBA8_UNORM` de una muestra. Una ventana suele querer
  otro formato: pinta en una textura y cópiala a la ventana (`visor/visor.cc`, `Copia`).

Un fotograma del agujero (`nucleo/agujero/agujero.h`):

```cpp
shd :: Agujero agujero(shd :: reloj_monotono()) ;

agujero.crear(gpu , error) ;                // una vez por dispositivo
agujero.ajustar(ancho , alto , error) ;
agujero.avanzar(ancho , alto , error) ;     // con el turno de la GPU
// fija tu objetivo y el viewport, de ancho × alto
agujero.pintar(ancho , alto) ;
```

Las pasadas de los nodos, en este orden: el líquido de los enlaces lee de la gota, y la gota se pinta encima.

```cpp
auto gota = std :: make_unique<shd :: nodos :: PasadaGota>(shd :: reloj_monotono()) ;
pasadas.push_back(std :: make_unique<shd :: nodos :: PasadaLiquido>(* gota)) ;   // la gota vive lo que la lista
pasadas.push_back(std :: move(gota)) ;
```

Un fotograma de los nodos: rellena `shd :: nodos :: Fotograma` y llama a cada `Pasada` en orden con
`preparar`, `subir` y, con el objetivo fijado, `dibujar` (`nucleo/nodos/pasada.h`). El cbuffer de la cámara
es tuyo: `camara_de` te lo da y lo subes tú.

Lo que un integrador no debe olvidar:

- **Medir:** `shd::Medidor` es opcional. Sin él no cuesta nada.
- **Prioridad:** si la interfaz comparte GPU, la prioridad de la cola la decide quien crea el contexto.
- **Varios contextos:** cada recurso lleva la máscara del suyo. Un `Gpu` con la máscara de otro contexto
  retrasaría la liberación de los recursos hasta que ese otro contexto avance.

## El visor

```bash
cmake -S . -B build -DDILIGENT_CORE_SOURCE_DIR=<checkout de DiligentCore v2.5.6>
cmake --build build --target shd_visor
build/visor/shd_visor agujero     # o nodos, o pestanas
```

Sin `DILIGENT_CORE_SOURCE_DIR`, CMake descarga DiligentCore fijado al commit. La ventana es XCB, y funciona
bajo XWayland.

Teclas: `1` el agujero, `2` los nodos, `3` las pestañas, `q` o `Esc` para salir. En el agujero, el botón izquierdo
orbita, el derecho rueda y el doble clic agita. En los nodos, arrastrar mueve cualquier nodo, también el vórtice,
contra otro o contra los bordes, y un clic en el vórtice lo agita. En las pestañas, las 40 de la demostración de CORTEX
con su física: arrastrar un nodo lo deja clavado donde se suelta, y el clic derecho lo devuelve a la física. Con
`SHD_NODOS_GUION`, uno se lleva a una esquina, se suelta y luego se libera.

## Comprobaciones

```bash
pwsh scripts/check.ps1     # puerta de calidad completa, antes de cada commit
```

**Volcados:** con `AGUJERO_RELOJ` el reloj avanza 1/60 s por fotograma y el agujero es repetible bit a bit.
Sirve para probar que un cambio no altera la imagen:

```bash
AGUJERO_RELOJ=1 AGUJERO_VOLCAR=<carpeta> AGUJERO_VOLCAR_TIEMPOS=2.5,8,20 SHD_TAM=605x933 \
  AGUJERO_ARRASTRE=150,200 AGUJERO_DOBLE_CLIC=3,3.3 build/visor/shd_visor agujero
```

Con volcados el visor no presenta en la ventana: presentando, el polvo deja de ser repetible a partir de
unos 10 s, por una causa sin identificar.

**Banco de la GPU:** con `SHD_BANCO_GPU=1`, en la escena de los nodos, lo que tarda cada pasada y cada tramo de dentro,
con el mínimo, la mediana y el p95 cada 300 fotogramas y al salir. `SHD_NODOS_MALLA=16x16` dibuja una malla de nodos para medir el
líquido con un grafo grande:

```bash
AGUJERO_RELOJ=1 AGUJERO_VOLCAR=<carpeta> SHD_NODOS_VOLCAR=900 SHD_TAM=800x600 SHD_BANCO_GPU=1 \
  build/visor/shd_visor nodos
```

## Licencias

El código propio es MIT (`LICENSE`). DiligentCore se descarga aparte y conserva la suya, Apache 2.0.
