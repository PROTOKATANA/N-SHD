//! La frontera con la GPU (C++), por cxx. Las estructuras son compartidas: cxx
//! las genera iguales en los dos lenguajes, sin bytes ni firmas escritas dos
//! veces. El lado C++ lo genera `build.rs` y lo compila quien integra `nucleo/`. Todo se llama desde el
//! hilo que pinta, salvo [`motor_nuevo`], que se llama desde el que lo crea, antes de usarlo.

use crate::agujero::entrada::{cursor, doble_clic, vista, vista_pedida};
use crate::agujero::kerr::{horizonte, isco};
use crate::agujero::motor::{Motor, motor_nuevo};

#[cxx::bridge(namespace = "shd::agujero")]
mod ffi {
    /// El cursor en píxeles lógicos de la vista.
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    struct Cursor {
        x: f32,
        y: f32,
        /// El botón izquierdo: orbitar.
        pulsado: bool,
        dentro: bool,
        /// El botón derecho: rodar sobre la vista.
        rodar: bool,
    }

    /// El tamaño de la vista en píxeles físicos; 0 × 0, si aún no hay.
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    struct Vista {
        ancho: u32,
        alto: u32,
    }

    /// Una orientación de la cámara.
    #[derive(Clone, Copy, Debug, PartialEq)]
    struct BaseCamara {
        derecha: [f64; 3],
        arriba: [f64; 3],
        /// Del agujero a la cámara.
        atras: [f64; 3],
    }

    /// La órbita: ángulos en radianes.
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    struct DescOrbita {
        /// La de partida, desde +z.
        inclinacion: f64,
        azimut: f64,
        /// Radianes por píxel arrastrado.
        sensibilidad: f64,
        /// 1/s: lo que se frena la inercia al soltar.
        frenado: f64,
        /// rad/s: lo que avanza sola (0: quieta).
        deriva: f64,
        /// rad/s: lo que gira su rumbo, como mucho.
        rumbo: f64,
    }

    /// Lo que el motor necesita de la forma del agujero.
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    struct DescMotor {
        orbita: DescOrbita,
        /// s: lo agitado cae a 1/e en este tiempo.
        calma: f64,
        /// Lo agitado, como mucho.
        tope: f64,
    }

    /// Lo que la GPU necesita para pintar un fotograma.
    #[derive(Clone, Copy, Debug, PartialEq)]
    struct Fotograma {
        /// La cámara llevada a azimut 0: con ella se trazan los rayos.
        base_trazado: BaseCamara,
        /// Radianes: gira el disco y el cielo al componer.
        azimut: f64,
        /// La base de trazado cambió: hay que volver a trazar.
        moviendose: bool,
        /// Lo agitado que está el disco.
        agitado: f64,
        /// Los dobles clics que ha habido.
        dobles_clics: u32,
    }

    extern "Rust" {
        /// El estado de un agujero, que se queda C++.
        type Motor;

        fn motor_nuevo(d: &DescMotor) -> Box<Motor>;

        /// Lo que llega de la interfaz, para quien la lleve en C++: la vista en píxeles físicos.
        fn vista(ancho: u32, alto: u32);

        /// El cursor, en píxeles lógicos de la vista.
        fn cursor(c: Cursor);

        /// Un doble clic.
        fn doble_clic();

        /// La vista que pidió la interfaz.
        fn vista_pedida() -> Vista;

        /// El fotograma de ahora, sin avanzar.
        fn fotograma(self: &Motor) -> Fotograma;

        /// Pasan `paso` s con la entrada de ahora. `antes`: los segundos del
        /// fotograma anterior, para los ensayos sin ratón.
        fn avanzar(self: &mut Motor, antes: f64, paso: f64) -> Fotograma;

        /// Sin segundos de volcado: se vuelca cada fotograma.
        fn volcar_siempre(self: &Motor) -> bool;

        /// Queda algún volcado por hacer.
        fn volcados_pendientes(self: &Motor) -> bool;

        /// Los archivos de los volcados que han llegado hasta `t` y aún no se
        /// hicieron.
        fn volcados_hasta(self: &mut Motor, t: f64) -> Vec<String>;

        fn horizonte(a: f64) -> f64;

        fn isco(a: f64) -> f64;
    }
}

pub use ffi::{BaseCamara, Cursor, DescMotor, DescOrbita, Fotograma, Vista};
