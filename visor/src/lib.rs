//! Lo que el visor enlaza de Rust. Sin este `use`, el enlazador se dejaría fuera el motor entero,
//! porque aquí nadie lo llama: quien lo llama es el C++, por cxx.

pub use shd::agujero::motor::motor_nuevo;
