//! La frontera con la GPU (C++), por cxx. Los cbuffers son estructuras compartidas: cxx los genera
//! iguales en los dos lenguajes y Rust los rellena en su sitio, sin bytes ni firmas escritas dos
//! veces. El lado C++ lo genera `build.rs` y lo compila quien integra `nucleo/`. Todo se llama desde
//! el hilo que pinta.

use super::camara::camara_de;
use super::fluido::{forma_fluido_vortice, huella, q_circulo};
use super::gota::{EstadoGota, desc_gota, estado_gota};
use super::liquido::rellenar_liquido_en_camara as rellenar_liquido;
use super::vortice::{EstadoVortice, diagnostico_vortice, estado_vortice};

#[cxx::bridge(namespace = "shd::nodos")]
mod ffi {
    /// Un nodo ya colocado, en unidades de mundo.
    #[derive(Clone, Copy, Debug, PartialEq)]
    struct Nodo {
        x: f32,
        y: f32,
        radio: f32,
        /// 0xRRGGBBAA.
        rgba: u32,
    }

    /// Lo que los shaders necesitan de la escena de un fotograma.
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    struct Escena {
        /// Coordenada de mundo en el centro de la vista.
        centro_x: f32,
        centro_y: f32,
        /// Píxeles por unidad de mundo.
        escala: f32,
        /// Radio que se ve / radio del mundo: los topes de zoom.
        factor_radio: f32,
        cuenta_nodos: u32,
        /// Índice del nodo resaltado + 1; 0 = ninguno.
        resaltado: u32,
        /// Índice del nodo vórtice + 1; 0 = ninguno.
        vortice: u32,
        /// Veces que se pulsó el vórtice: al cambiar, se reorganiza.
        pulsos: u32,
        /// Tamaño de la vista en píxeles, para sus bordes; 0, sin bordes.
        ancho: u32,
        alto: u32,
        /// El vórtice es un líquido suelto (`PasadaGota`): su forma cambia, así que en el líquido de
        /// los enlaces su gota no asoma alrededor, o se vería un círculo fijo debajo.
        vortice_libre: bool,
    }

    /// El cbuffer «Camara» que comparten las pasadas.
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    struct CamaraGpu {
        /// (sx, sy, tx, ty): de mundo a NDC.
        transform: [f32; 4],
        /// (x0, y0, x1, y1): la caja del líquido en mundo.
        caja: [f32; 4],
        /// (resaltado, factor de radio, vórtice, 0).
        extra: [f32; 4],
    }

    /// El cbuffer «Liquido», con la misma disposición que en el shader.
    struct LiquidoGpu {
        /// gota, cuello, fusión, opacidad.
        forma: [f32; 4],
        /// oscurecer, nodos, enlaces, filas de la rejilla.
        cuenta: [f32; 4],
        /// x0, y0, lado de celda (mundo), columnas.
        rejilla: [f32; 4],
        /// x, y, radio que se ve (mundo), lo que se hunde su gota (mundo).
        nodos: [[f32; 4]; 256],
        /// rgba.
        colores: [[f32; 4]; 256],
        /// Dos enlaces por elemento.
        aristas: [[u32; 4]; 256],
        /// Por celda: inicio (16 bits bajos) y cuántos (16 altos).
        celdas: [[u32; 4]; 256],
        /// Dos índices de 16 bits por uint: nodo i, o enlace nodos + e.
        indices: [[u32; 4]; 3068],
    }

    /// El cbuffer «Vortice»: lo leen su forzamiento en el solver y su pintado.
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    struct VorticeGpu {
        /// x, y (mundo), radio que se ve (mundo), tiempo (s).
        centro: [f32; 4],
        /// Cuántos, fase, brillo, semilla de la última pulsación.
        brazos: [f32; 4],
        /// Segundos desde la pulsación, duración, aceleración de los chorros, turbulencia.
        agitar: [f32; 4],
        /// rgba del nodo.
        color: [f32; 4],
        /// Velocidad, núcleo, arrastre, calentar.
        giro: [f32; 4],
        /// Por celda: entrada en el borde, salida en el ojo (negativa); q del borde, q del ojo.
        fuentes: [f32; 4],
    }

    /// El cbuffer «Gota». Lo que no sea `centro` va relativo al centro del nodo y en múltiplos de su
    /// radio: en esas unidades vive el líquido.
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    struct GotaGpu {
        /// dt de cada subpaso (s), tiempo (s), segundos desde la pulsación, semilla.
        paso: [f32; 4],
        /// Lo que tira el arrastre del borde (x, y), la fuerza de la pulsación, cuántos obstáculos.
        inercia: [f32; 4],
        /// Ancla, freno del ancla, giro, arrastre del giro.
        fuerzas: [f32; 4],
        /// Cohesión, viscosidad, velocidad máxima, chorros que nunca paran.
        superficie: [f32; 4],
        /// Los bordes de la vista: x0, y0, x1, y1.
        bordes: [f32; 4],
        /// El nodo en el mundo: x, y, radio que se ve; lo que se atrae el líquido a sí mismo.
        centro: [f32; 4],
        /// x, y, radio con el margen de una partícula, radio de su gota en el líquido de los enlaces.
        obstaculos: [[f32; 4]; 8],
        /// Empujones: de dónde vienen (x, y), su fuerza (radios/s²), 0.
        contactos: [[f32; 4]; 4],
        /// Subpasos de este fotograma, lo que duró el fotograma (s), 0, 0.
        ritmo: [f32; 4],
        /// Lo que barrió cada obstáculo respecto al vórtice desde el fotograma anterior: dos por
        /// elemento, (x0, y0, x1, y1).
        barrido: [[f32; 4]; 4],
        /// La forma del líquido de los enlaces: gota, fusión, cuello, 0.
        tubo: [f32; 4],
        /// Cuántas gotas y cuántos tubos del líquido de los enlaces hay cerca, 0, 0.
        cuenta_liquido: [f32; 4],
        /// Las gotas del líquido de los enlaces cerca del vórtice, en su orden: x, y, radio que se ve,
        /// fusión.
        gotas: [[f32; 4]; 12],
        /// Sus tubos: un extremo (x, y) y el otro (x, y).
        tubos: [[f32; 4]; 16],
        /// La media anchura de cada tubo, cuatro por elemento.
        anchos: [[f32; 4]; 4],
        /// Lo que se funde la gota con cada tubo, de 0 a 1, cuatro por elemento.
        funde: [[f32; 4]; 4],
    }

    /// Lo que fija el tamaño de la simulación: va en el HLSL.
    #[derive(Clone, Copy, Debug, PartialEq)]
    struct DescGota {
        particulas: u32,
        /// Lo más que da un fotograma: cada subpaso dura siempre lo mismo.
        subpasos: u32,
        /// Iteraciones de la presión por subpaso: impares.
        iteraciones: u32,
        /// Lado de la rejilla de vecinos: una potencia de 2.
        lado_vecinos: u32,
        /// Lado del campo que se pinta.
        lado_campo: u32,
        /// Alcance de cada partícula, en radios.
        alcance: f32,
        /// Lo que mide el campo desde el centro, en radios.
        vista: f32,
    }

    /// Lo que da el estado del vórtice para un fotograma.
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    struct FotogramaVortice {
        cbuffer: VorticeGpu,
        /// s.
        dt: f32,
    }

    /// La rejilla y el solver de un fluido.
    #[derive(Clone, Copy, Debug, PartialEq)]
    struct FormaFluido {
        /// Lado de la rejilla.
        celdas: u32,
        /// Iteraciones de presión por paso.
        jacobi: u32,
        /// Refuerzo de los remolinos pequeños (vorticity confinement).
        confinamiento: f32,
    }

    /// Si es un fluido incompresible (la divergencia, que la presión deja en casi cero fuera de las
    /// fuentes) y estable (sin NaN ni velocidades locas).
    #[derive(Clone, Copy, Debug, PartialEq)]
    struct DiagnosticoFluido {
        /// |∇·v| en las celdas de dentro (sin fuentes ni pared al lado).
        divergencia_max: f32,
        divergencia_media: f32,
        /// celdas/s.
        velocidad_max: f32,
        escalar_min: f32,
        escalar_max: f32,
        nan: u32,
    }

    extern "Rust" {
        /// La cámara de la escena para una textura de `ancho` × `alto` px, con el centro de la
        /// cámara en el de la textura. La caja la pone el líquido.
        fn camara_de(escena: &Escena, ancho: u32, alto: u32) -> CamaraGpu;

        /// Rellena `liquido` con los nodos y los pares de aristas, aplanados, y pone en
        /// `camara.caja` la del líquido en mundo, con el margen de la gota y la fusión. False si no
        /// hay nodos, o algo no es finito. Sin rejilla, una sola celda con la lista completa.
        fn rellenar_liquido(
            escena: &Escena,
            nodos: &[Nodo],
            aristas: &[u32],
            liquido: &mut LiquidoGpu,
            camara: &mut CamaraGpu,
            con_rejilla: bool,
        ) -> bool;

        /// El reloj y las pulsaciones de un vórtice, de un fotograma al siguiente.
        type EstadoVortice;

        fn estado_vortice() -> Box<EstadoVortice>;

        /// Rellena `f` para el fotograma de `escena`. `ahora`: segundos de un reloj monótono.
        /// False si la escena no tiene vórtice.
        fn rellenar(
            self: &mut EstadoVortice,
            escena: &Escena,
            nodos: &[Nodo],
            ahora: f64,
            f: &mut FotogramaVortice,
        ) -> bool;

        /// El reloj, la pulsación y lo que choca con la gota, de un fotograma al siguiente.
        type EstadoGota;

        fn estado_gota() -> Box<EstadoGota>;

        /// Rellena `g` para el fotograma de `escena`. `ahora`: segundos de un reloj monótono. False
        /// si la escena no tiene vórtice.
        fn rellenar(
            self: &mut EstadoGota,
            escena: &Escena,
            nodos: &[Nodo],
            aristas: &[u32],
            ahora: f64,
            g: &mut GotaGpu,
        ) -> bool;

        fn desc_gota() -> DescGota;

        /// La rejilla y el solver del fluido del vórtice.
        fn forma_fluido_vortice() -> FormaFluido;

        /// La q del círculo de una rejilla de `celdas`.
        fn q_circulo(celdas: u32) -> f32;

        /// El diagnóstico de un estado del fluido del vórtice: `celdas`² velocidades (x, y) y
        /// `celdas`² valores del escalar.
        fn diagnostico_vortice(vel: &[f32], escalar: &[f32], celdas: u32) -> DiagnosticoFluido;

        /// FNV-1a de 64 bits. `base`, para encadenar varios búferes: la de partida es
        /// [`super::fluido::HUELLA_INICIAL`].
        fn huella(datos: &[u8], base: u64) -> u64;
    }
}

pub use ffi::{
    CamaraGpu, DescGota, DiagnosticoFluido, Escena, FormaFluido, FotogramaVortice, GotaGpu, LiquidoGpu, Nodo,
    VorticeGpu,
};

const _: () = assert!(size_of::<CamaraGpu>() == 48);
const _: () = assert!(size_of::<VorticeGpu>() == 96);
const _: () = assert!(size_of::<GotaGpu>() == 288 + 16 + 64 + 16 * 2 + 12 * 16 + 16 * 16 + 4 * 16 * 2);
const _: () = assert!(size_of::<LiquidoGpu>() == 48 + 256 * 16 * 3 + 1024 * 4 + 24544 * 2);
const _: () = assert!(size_of::<LiquidoGpu>() <= 65536, "un cbuffer mide como mucho 64 KiB");

impl LiquidoGpu {
    /// A ceros, en el montón: mide casi 64 KiB. `[T; 256]` no tiene `Default`.
    pub fn vacio() -> Box<Self> {
        Box::new(Self {
            forma: [0.0; 4],
            cuenta: [0.0; 4],
            rejilla: [0.0; 4],
            nodos: [[0.0; 4]; 256],
            colores: [[0.0; 4]; 256],
            aristas: [[0; 4]; 256],
            celdas: [[0; 4]; 256],
            indices: [[0; 4]; 3068],
        })
    }
}
