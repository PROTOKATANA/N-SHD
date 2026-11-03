# Cambios

## Sin publicar

- El agujero negro y los nodos (líquido, vórtice y fluido), sacados de CORTEX a una biblioteca sin
  interfaz: la lógica en Rust, la GPU en C++ y `cxx` entre ambos.
- El visor: la galería en una ventana Vulkan, sin Flutter.
- El banco del visor: `SHD_BANCO_GPU` y `SHD_NODOS_MALLA`; `Pasada` y `Fluido` dicen qué miden por dentro.
- El líquido de los enlaces, con las celdas de su rejilla tan pequeñas como quepan: la misma imagen, de 0,18 a 0,09 ms con 9 nodos y de 4,6 a 0,54 ms con una malla de 16 × 16.
