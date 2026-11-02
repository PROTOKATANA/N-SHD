# Contribuir

## Dónde va cada cosa

- **Rust (`motor/`):** la lógica, el estado y todo lo que se pueda probar con `cargo test`.
- **C++ (`nucleo/`):** solo lo que toca la GPU y el HLSL.
- **Entre los dos, `cxx`:** estructuras compartidas y slices, sin bytes, sin punteros y sin `unsafe` propio.

N-SHD no conoce ninguna interfaz. Nada de Flutter, Dart, `rinf`, GTK ni GLib: lo de la app va en el
pegamento de la app.

## Añadir un shader

1. La lógica en `motor/src/<familia>`, con sus pruebas y su `puente.rs`, añadido a `PUENTES` en `build.rs`.
2. La GPU en `nucleo/<familia>`, añadida a `nucleo/CMakeLists.txt`.
3. Una escena en `visor/visor.cc`, para poder verlo sin ninguna app.

## Estilo

- **Rust:** `rustfmt` y `clippy` sin avisos. `unwrap` y `expect` están prohibidos fuera de las pruebas.
- **C++:** tabulaciones, una línea vacía entre instrucciones, y espacios en ` ;`, ` :: ` y `# include`.
  Todos los avisos son errores.
- **Commits:** `tipo(ámbito): verbo en infinitivo`, en español.

## Antes de cada commit

```bash
pwsh scripts/check.ps1
```

Si el cambio toca lo que se pinta, compara además los volcados con los de antes (ver el README): la imagen
no debe cambiar salvo que ese sea el objetivo.
