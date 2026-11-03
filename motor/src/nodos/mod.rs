//! La lógica de los shaders de un grafo de nodos:
//!
//! - [`camara`]: el cbuffer que comparten las pasadas.
//! - [`liquido`]: los enlaces como un líquido, con su rejilla de culling.
//! - [`vortice`]: el nodo que es un remolino, con su reloj y sus pulsaciones.
//! - [`gota`]: el vórtice como un líquido suelto: su reloj, lo que choca con él y con qué fuerza.
//! - [`fluido`]: la rejilla del fluido del vórtice, su diagnóstico y su huella.
//!
//! La escena la pone quien integra: un grafo, o cualquier lista de nodos y enlaces.

pub mod camara;
pub mod fluido;
pub mod gota;
pub mod liquido;
pub mod puente;
pub mod vortice;

/// 0xRRGGBBAA, de 0 a 1.
fn color_rgba(rgba: u32) -> [f32; 4] {
    [
        ((rgba >> 24) & 255) as f32 / 255.0,
        ((rgba >> 16) & 255) as f32 / 255.0,
        ((rgba >> 8) & 255) as f32 / 255.0,
        (rgba & 255) as f32 / 255.0,
    ]
}
