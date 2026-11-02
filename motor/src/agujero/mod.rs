//! La lógica del agujero negro:
//!
//! - [`orbita`]: la cámara que se arrastra con inercia y se mueve sola.
//! - [`agitacion`]: lo agitado del disco por los dobles clics.
//! - [`kerr`]: la física de los rayos, la misma que el HLSL de `rayos_kerr`.
//! - [`guion`]: los ensayos sin ratón de los bancos y los volcados.
//! - [`entrada`]: lo último que llegó de la interfaz.
//! - [`motor`]: lo anterior, avanzado una vez por fotograma.
//!
//! Quien integra escribe en [`entrada`] lo que le llegue de su interfaz.

pub mod agitacion;
pub mod entrada;
pub mod guion;
pub mod kerr;
pub mod motor;
pub mod orbita;
pub mod puente;
