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
- La gota envuelta (sin vórtice libre): el líquido de los enlaces le pinta un anillo
  que sigue su forma, leyendo de la gota lo lejos que queda (`DistanciaGota`, la distancia de sus canales); sus canales
  son solo sus enlaces, un 70 % más cortos, y choca con la vista a lo que asoma el anillo. Más barata que la libre
  (simular 0,754 ms frente a 0,83; pintar 0,097 frente a 0,13); el líquido, de 0,133 a 0,139. En el visor, con
  `SHD_GOTA_ENVUELTA`. Ver RAYO-LEGO.md.
- Quitado el vórtice rígido (`PasadaVortice`, su fluido `Fluido` y `fluido_stam`), una versión de prueba: el vórtice de
  los nodos es la gota. El visor deja una sola escena de nodos, `nodos` (tecla `2`); `gota` y la tecla `3` ya no existen.
- Quitada la gota libre (`Escena :: vortice_libre`), una versión de prueba: la gota va siempre envuelta, sus canales son
  solo sus enlaces y `PasadaLiquido` exige la gota (`PasadaLiquido(const DistanciaGota & , bool con_rejilla)`). El
  visor la abre así sin variables; `SHD_GOTA_ENVUELTA` ya no existe. Fuera también lo que solo usaba la libre: las gotas
  ajenas y la fusión gradual con los tubos ajenos en el cbuffer de la gota, y lo que se hundía la gota del vórtice en el
  líquido de los enlaces. La imagen, idéntica byte a byte.
- `PasadaLiquido` pinta los enlaces de un grafo sin vórtice: esperaba la distancia de la gota, que sin vórtice no se
  crea nunca, y el grafo salía vacío. Ahora ata una vacía en su lugar; con vórtice, la imagen no cambia.
- Los nodos no se tocan (`separar_nodos`, `shd::nodos::choque`): el choque del antiguo motor de fuerzas de CORTEX, a
  1,5 veces la suma de sus radios. El arrastrado no cede, salvo contra un nodo que el borde acaba de devolver, y un
  choque no saca a un nodo de la vista. El vórtice no choca: los nodos siguen atravesando la gota. El visor lo aplica
  en cada fotograma, y la pared de su guion `pared` queda tan junta como deja el choque.
- La física de las pestañas de CORTEX (`shd::nodos::fisica`), copiada tal cual de su rama de las pestañas: el layout
  de d3 con Barnes-Hut y cúmulos por grupo, `fuerzas_para`, la colocación asentada de un tirón y las órdenes del ratón
  (llevar clavado, soltar, liberar). Sin rayon, con el mismo resultado bit a bit; el hilo y la disposición guardada se
  quedan en la aplicación. `FisicaGrafo` la da a C++, y el visor la enseña en la escena `pestanas` (tecla `3`), con las
  40 pestañas de la demostración de CORTEX.
