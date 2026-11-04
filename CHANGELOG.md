# Cambios

## Sin publicar

- El agujero negro y los nodos (líquido, vórtice y fluido), sacados de CORTEX a una biblioteca sin
  interfaz: la lógica en Rust, la GPU en C++ y `cxx` entre ambos.
- El visor: la galería en una ventana Vulkan, sin Flutter.
- `PasadaGota`: el vórtice como un líquido suelto, sin pared, que choca con otros nodos y con los bordes de la
  vista y va siempre con su enlace. Va en piezas (partículas, campo, canales y pintura) y se repite bit a bit. El visor
  deja arrastrar los nodos, tiene guiones para probar los choques sin ratón y vuelca fotogramas de los nodos.
- El banco del visor: `SHD_BANCO_GPU` y `SHD_NODOS_MALLA`; `Pasada` y `Fluido` dicen qué miden por dentro.
- El líquido de los enlaces, con las celdas de su rejilla tan pequeñas como quepan: la misma imagen, de 0,18 a 0,09 ms con 9 nodos y de 4,6 a 0,54 ms con una malla de 16 × 16.
- El Jacobi del vórtice, en bloques de 16 × 16 con 5 iteraciones por lanzamiento (`nucleo/comun/bloque_hlsl.h`): el mismo resultado bit a bit, de 0,062 a 0,042 ms.
- El informe de RAYO y LEGO sobre los nodos, con cada medida: `nucleo/nodos/RAYO-LEGO.md`.
- La gota sin trozos sueltos (`SueltosGota`): solo se pinta el líquido unido a su cuerpo, por la gota o por los canales.
  Lo suelto sigue en la simulación y se ve otra vez al volver. En los cuatro guiones del visor, de 10 fotogramas con
  trozos a ninguno (el contador aún marca 3, el borde suavizado del líquido sobre el canto de un nodo); la gota sin
  trozos, idéntica byte a byte. Cuesta 0,18 ms en la GPU a 1280 × 800 (la escena de la gota, de 0,76 a 0,94).
- `Pasada :: preparar` no avanza nada que dependa del tiempo: la gota y el vórtice avanzan al subir. Un integrador que
  prepara y abandona el fotograma (sin imagen libre) les quitaba tiempo: en CORTEX, con los nodos moviéndose, la gota
  simulaba 41-90 subpasos por segundo de 120. La misma imagen en el visor.
