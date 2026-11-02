//! La lógica de la galería de shaders: lo que no necesita GPU y se prueba con `cargo test`.
//!
//! - [`agujero`]: el agujero negro de Kerr, con su cámara, su física y su entrada.
//! - [`nodos`]: los shaders de un grafo de nodos, el líquido y el vórtice, con su fluido.
//!
//! No sabe nada de Flutter, de Dart ni de `rinf`. La GPU (`nucleo/`, en C++) lee de aquí por cxx.

pub mod agujero;
pub mod nodos;
