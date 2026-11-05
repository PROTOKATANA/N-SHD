//! La gota: el vórtice como un líquido, sin pared. Son partículas que resuelven Navier-Stokes
//! en la GPU (`nucleo/nodos/pasada_gota`, Position Based Fluids): el volumen se conserva porque cada
//! partícula es un trozo de líquido, y la forma la deciden la presión, la cohesión y lo que choca.
//!
//! Aquí está lo que se calcula en la CPU: el reloj, las pulsaciones, lo que el arrastre deja atrás, y
//! con qué choca el líquido y con qué fuerza. Todo lo que llega a la GPU va relativo al centro del
//! nodo y en múltiplos de su radio, salvo donde se diga.
//!
//! El líquido vive en el marco de su nodo: arrastrar el nodo lo lleva entero, al instante, y por eso
//! va siempre con su enlace, sea cual sea la velocidad. El arrastre solo le cambia la forma.
//!
//! Las piezas: `contactos` (con qué choca el líquido y con qué fuerza), `retraso` (lo que el
//! arrastre deja atrás) y `cercano` (la silueta del líquido de los enlaces cerca del vórtice).
//! `EstadoGota` las une en lo que lee la GPU en cada fotograma.

mod cercano;
mod contactos;
mod retraso;

use super::liquido::FORMA as LIQUIDO;
use super::puente::{DescGota, Escena, GotaGpu, Nodo};
pub use cercano::MAX_TUBOS;
use cercano::liquido_cercano;
use contactos::Contactos;
pub use contactos::{MAX_CONTACTOS, MAX_OBSTACULOS};
use retraso::Retraso;

/// Al cabo de un día el reloj vuelve a cero: en `f32`, los segundos de varios días ya no distinguen
/// un fotograma del siguiente.
const PERIODO_RELOJ: f64 = 86400.0;

/// El tamaño de la simulación.
pub const DESC: DescGota = DescGota {
    particulas: 4096,
    subpasos: 4,
    iteraciones: 3,
    lado_vecinos: 64,
    lado_campo: 384,
    alcance: 0.07,
    vista: 3.0,
};

const _: () = assert!(
    DESC.iteraciones % 2 == 1,
    "la última escritura va a la segunda prevista"
);

/// s: cada subpaso dura siempre esto, vaya la pantalla a la velocidad que vaya. Con un número fijo
/// por fotograma, la gota cambiaba con los fotogramas por segundo: a 30, más pequeña y sin brazos.
pub const SUBPASO: f64 = 1.0 / 120.0;

const _: () = assert!(
    DESC.subpasos as f64 * SUBPASO >= 1.0 / 30.0 - 1e-9,
    "el paso más largo cabe en un fotograma"
);

/// Lo que el líquido mide de más al chocar, en radios: media partícula. Sin esto, dos nodos que se
/// tocan dejan entre ellos un punto por el que se cuela.
pub const MARGEN: f32 = DESC.alcance * 0.5;

/// La forma de la gota.
pub struct FormaGota {
    /// 1/s²: lo que devuelve el líquido a su nodo tras un choque.
    pub ancla: f32,
    /// 1/s: lo que frena ese tirón, para que no oscile sin fin.
    pub freno: f32,
    /// 1/s²: lo que cae cada partícula hacia el centro del líquido: lo mantiene unido.
    pub gravedad: f32,
    /// radios/s: la velocidad del remolino en el borde de su núcleo.
    pub giro: f32,
    /// 1/s: lo que tira el remolino del líquido hacia su giro.
    pub arrastre: f32,
    /// radios/s²: lo que atraen las partículas a sus vecinas: la tensión superficial.
    pub cohesion: f32,
    /// De 0 a 1: lo que cada partícula se iguala a sus vecinas por subpaso.
    pub viscosidad: f32,
    /// radios/s: tope de velocidad: con más, atravesarían a sus vecinas.
    pub velocidad_max: f32,
    /// radios/s²: los chorros suaves que nunca paran.
    pub turbulencia: f32,
    /// radios/s²: los chorros al pulsar una vez.
    pub chorro: f32,
    /// Pulsaciones seguidas: cada una suma a la racha, hasta este tope.
    pub racha_max: f32,
    /// Segundos en que la racha cae a 1/e.
    pub olvido: f32,
    /// radios/s² por radio/s de velocidad del choque.
    pub choque: f32,
    /// Tope de esa aceleración.
    pub choque_max: f32,
    /// Tope del paso de tiempo (s): tras una pausa no da un salto.
    pub paso_max: f32,
    /// 1/s²: lo que vuelve el borde que el arrastre dejó atrás.
    pub vuelta: f32,
    /// 1/s: lo que frena esa vuelta: con poco, el líquido se mece un par de veces al parar.
    pub calma: f32,
    /// radios: lo más que se queda atrás el borde.
    pub retraso_max: f32,
    /// radios/s² por radio de retraso: lo que tira eso del borde.
    pub estiron: f32,
}

pub const FORMA: FormaGota = FormaGota {
    ancla: 60.0,
    freno: 9.0,
    gravedad: 30.0,
    giro: 0.7,
    arrastre: 2.5,
    cohesion: 320.0,
    viscosidad: 0.35,
    velocidad_max: 4.5,
    turbulencia: 2.4,
    chorro: 13.0,
    racha_max: 4.0,
    olvido: 1.0,
    choque: 12.0,
    choque_max: 40.0,
    paso_max: 1.0 / 30.0,
    vuelta: 64.0,
    calma: 5.6,
    retraso_max: 0.6,
    estiron: 45.0,
};

/// Ver el puente.
pub fn desc_gota() -> DescGota {
    DESC
}

fn menor(a: f64, b: f64) -> f64 {
    if b < a { b } else { a }
}

fn mayor(a: f64, b: f64) -> f64 {
    if a < b { b } else { a }
}

/// Lleva el reloj, las pulsaciones y lo que choca de un fotograma al siguiente. Cada pulsación agita
/// el líquido, y más fuerte cuanto más seguidas lleguen.
#[derive(Clone, Debug)]
pub struct EstadoGota {
    visto: bool,
    pulsos: u32,
    origen: f64,
    anterior: f64,
    pulso: f64,
    racha: f64,
    semilla: u32,
    /// Dónde estaba el nodo, en mundo.
    donde: [f32; 2],
    /// s: el tiempo que queda por simular, menos de un subpaso.
    acumulado: f64,
    retraso: Retraso,
    contactos: Contactos,
}

impl Default for EstadoGota {
    fn default() -> Self {
        Self {
            visto: false,
            pulsos: 0,
            origen: 0.0,
            anterior: 0.0,
            pulso: -1e9,
            racha: 0.0,
            semilla: 0,
            donde: [0.0; 2],
            acumulado: 0.0,
            retraso: Retraso::default(),
            contactos: Contactos::default(),
        }
    }
}

/// Ver el puente.
pub fn estado_gota() -> Box<EstadoGota> {
    Box::new(EstadoGota::default())
}

impl EstadoGota {
    /// Ver el puente.
    pub fn rellenar(&mut self, escena: &Escena, nodos: &[Nodo], aristas: &[u32], ahora: f64, g: &mut GotaGpu) -> bool {
        let indice = escena.vortice;
        let cabe = indice != 0 && indice <= escena.cuenta_nodos && nodos.len() >= indice as usize;
        let escala_buena = escena.escala > 0.0;
        if !cabe || !escala_buena {
            return false;
        }
        let i = indice as usize - 1;
        let n = nodos[i];
        let factor = if escena.factor_radio > 0.0 {
            escena.factor_radio
        } else {
            1.0
        };
        let radio = n.radio * factor;
        let bien = radio > 0.0 && radio.is_finite() && n.x.is_finite() && n.y.is_finite();
        if !bien {
            return false;
        }
        if !self.visto {
            self.visto = true;
            self.pulsos = escena.pulsos;
            self.origen = ahora;
            self.anterior = ahora;
            self.semilla = escena.pulsos;
            self.donde = [n.x, n.y];
        }
        if escena.pulsos != self.pulsos {
            let nuevas = escena.pulsos.wrapping_sub(self.pulsos);
            let queda = self.racha * (-mayor(ahora - self.pulso, 0.0) / f64::from(FORMA.olvido)).exp();
            self.racha = menor(queda + f64::from(nuevas), f64::from(FORMA.racha_max));
            self.pulsos = escena.pulsos;
            self.semilla = escena.pulsos;
            self.pulso = ahora;
        }
        let dt = menor(mayor(ahora - self.anterior, 0.0), f64::from(FORMA.paso_max)) as f32;
        self.anterior = ahora;
        // los subpasos que caben en lo que pasó, y lo que sobra, para el siguiente
        self.acumulado += f64::from(dt);
        let subpasos = menor((self.acumulado / SUBPASO + 1e-6).floor(), f64::from(DESC.subpasos));
        self.acumulado = menor(mayor(self.acumulado - subpasos * SUBPASO, 0.0), SUBPASO);
        g.ritmo = [subpasos as f32, dt, 0.0, 0.0];
        g.paso = [
            SUBPASO as f32,
            (mayor(ahora - self.origen, 0.0) % PERIODO_RELOJ) as f32,
            menor(ahora - self.pulso, 1e4) as f32,
            (self.semilla % 65536) as f32,
        ];
        let movido = [(n.x - self.donde[0]) / radio, (n.y - self.donde[1]) / radio];
        self.donde = [n.x, n.y];
        self.retraso.avanzar(movido, dt);
        g.inercia = [
            self.retraso.donde[0] * FORMA.estiron,
            self.retraso.donde[1] * FORMA.estiron,
            FORMA.chorro * self.racha as f32,
            0.0,
        ];
        g.fuerzas = [FORMA.ancla, FORMA.freno, FORMA.giro, FORMA.arrastre];
        g.superficie = [FORMA.cohesion, FORMA.viscosidad, FORMA.velocidad_max, FORMA.turbulencia];
        g.centro = [n.x, n.y, radio, FORMA.gravedad];
        g.tubo = [LIQUIDO.gota, LIQUIDO.fusion, LIQUIDO.cuello, 0.0];
        liquido_cercano(escena, nodos, aristas, i, factor, g);
        self.contactos.rellenar(escena, nodos, i, dt, g);
        true
    }
}

/// Lo que comparten las pruebas de las piezas.
#[cfg(test)]
mod prueba {
    use super::super::puente::{Escena, Nodo};

    pub const DT: f32 = 1.0 / 60.0;

    pub fn nodo(x: f32, y: f32, radio: f32) -> Nodo {
        Nodo { x, y, radio, rgba: 0 }
    }

    pub fn escena() -> Escena {
        Escena {
            escala: 1.0,
            factor_radio: 1.0,
            cuenta_nodos: 2,
            vortice: 1,
            ..Escena::default()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::prueba::{escena, nodo};
    use super::*;

    #[test]
    fn el_liquido_va_donde_su_nodo_y_el_borde_se_queda_atras() {
        let mut estado = EstadoGota::default();
        let mut g = GotaGpu::default();
        let e = escena();
        assert!(estado.rellenar(&e, &[nodo(0.0, 0.0, 10.0), nodo(90.0, 0.0, 1.0)], &[], 1.0, &mut g));
        assert_eq!((g.inercia[0], g.inercia[1]), (0.0, 0.0), "el primero no se mueve");
        assert_eq!(g.paso[1], 0.0, "el reloj empieza en cero");
        assert!(estado.rellenar(&e, &[nodo(5.0, 0.0, 10.0), nodo(90.0, 0.0, 1.0)], &[], 1.02, &mut g));
        assert_eq!(g.centro, [5.0, 0.0, 10.0, FORMA.gravedad], "el líquido, con su nodo");
        assert!(g.inercia[0] < 0.0, "hacia la derecha, se queda atrás a la izquierda");
        assert_eq!(g.inercia[1], 0.0);
        assert_eq!(g.paso[0], SUBPASO as f32, "el subpaso no cambia");
        assert_eq!(g.ritmo[0], 2.0, "en 0,02 s caben dos");
    }

    #[test]
    fn la_gota_simula_lo_mismo_vaya_la_pantalla_como_vaya() {
        let nodos = [nodo(0.0, 0.0, 10.0), nodo(90.0, 0.0, 1.0)];
        for fps in [30.0, 60.0, 144.0, 1000.0] {
            let mut estado = EstadoGota::default();
            let mut g = GotaGpu::default();
            let mut subpasos = 0.0;
            let fotogramas = (fps * 10.0) as usize;
            for k in 0..=fotogramas {
                estado.rellenar(&escena(), &nodos, &[], 1.0 + k as f64 / fps, &mut g);
                if k > 0 {
                    subpasos += g.ritmo[0];
                }
                assert_eq!(g.paso[0], SUBPASO as f32);
            }
            // diez segundos son 1.200 subpasos, vaya a 30 o a 1.000 fotogramas por segundo
            assert!((subpasos - 1200.0_f32).abs() <= 1.0, "{fps} fps: {subpasos}");
        }
    }

    #[test]
    fn tras_una_pausa_el_paso_tiene_tope() {
        let mut estado = EstadoGota::default();
        let mut g = GotaGpu::default();
        let nodos = [nodo(0.0, 0.0, 10.0), nodo(90.0, 0.0, 1.0)];
        estado.rellenar(&escena(), &nodos, &[], 1.0, &mut g);
        estado.rellenar(&escena(), &nodos, &[], 50.0, &mut g);
        assert_eq!(g.ritmo[0], DESC.subpasos as f32);
        assert_eq!(g.paso[0], SUBPASO as f32);
    }

    #[test]
    fn sin_vortice_no_hay_gota() {
        let mut g = GotaGpu::default();
        let e = Escena { vortice: 0, ..escena() };
        assert!(!EstadoGota::default().rellenar(&e, &[nodo(0.0, 0.0, 10.0)], &[], 1.0, &mut g));
    }

    #[test]
    fn pulsar_la_agita_y_la_racha_tiene_tope() {
        let mut estado = EstadoGota::default();
        let mut g = GotaGpu::default();
        let nodos = [nodo(0.0, 0.0, 10.0), nodo(90.0, 0.0, 1.0)];
        let mut e = escena();
        estado.rellenar(&e, &nodos, &[], 1.0, &mut g);
        assert_eq!(g.inercia[2], 0.0, "al arrancar no agita");
        e.pulsos = 1;
        estado.rellenar(&e, &nodos, &[], 2.0, &mut g);
        assert_eq!((g.paso[2], g.inercia[2]), (0.0, FORMA.chorro));
        e.pulsos = 12;
        estado.rellenar(&e, &nodos, &[], 2.1, &mut g);
        assert_eq!(g.inercia[2], FORMA.chorro * FORMA.racha_max);
    }
}
