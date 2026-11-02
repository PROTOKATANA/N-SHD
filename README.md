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
  rejilla de culling. Admite 256 nodos y 512 enlaces, porque es para un menú.
- `PasadaVortice`: un nodo que es un remolino de fluido de verdad, Stable Fluids en compute shaders. Al
  pulsarlo, se agita.

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
build/visor/shd_visor agujero     # o nodos
```

Sin `DILIGENT_CORE_SOURCE_DIR`, CMake descarga DiligentCore fijado al commit. La ventana es XCB, y funciona
bajo XWayland.

Teclas: `1` el agujero, `2` los nodos, `q` o `Esc` para salir. En el agujero, el botón izquierdo orbita, el
derecho rueda y el doble clic agita. En los nodos, un clic en el vórtice lo agita.

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

## Licencias

El código propio es MIT (`LICENSE`). DiligentCore se descarga aparte y conserva la suya, Apache 2.0.
