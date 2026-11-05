//! La lógica de los shaders de un grafo de nodos:
//!
//! - [`camara`]: el cbuffer que comparten las pasadas, y la vista.
//! - [`choque`]: los nodos no se tocan; el vórtice no choca, porque es la gota.
//! - [`liquido`]: los enlaces como un líquido, con su rejilla de culling.
//! - [`gota`]: el vórtice como un líquido, envuelto en el anillo del líquido de los enlaces: su reloj,
//!   lo que choca con él y con qué fuerza.
//!
//! La escena la pone quien integra: un grafo, o cualquier lista de nodos y enlaces.

pub mod camara;
pub mod choque;
pub mod gota;
pub mod liquido;
pub mod puente;

/// 0xRRGGBBAA, de 0 a 1.
fn color_rgba(rgba: u32) -> [f32; 4] {
    [
        ((rgba >> 24) & 255) as f32 / 255.0,
        ((rgba >> 16) & 255) as f32 / 255.0,
        ((rgba >> 8) & 255) as f32 / 255.0,
        (rgba & 255) as f32 / 255.0,
    ]
}
