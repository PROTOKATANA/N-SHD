//! El vórtice: el nodo que, en lugar de un disco, es un remolino de fluido de verdad. El fluido
//! entra frío por el borde, gira y cae hacia el ojo calentándose, y lleva una temperatura que se
//! pinta con una gama de calor. Siempre está agitado: unos chorros suaves que nunca paran lo
//! revuelven. Al pulsar el nodo, otros más fuertes lo sacuden: el fluido choca contra la pared del
//! círculo y se mezcla, y luego el remolino vuelve con otros brazos.
//!
//! Aquí está lo que se calcula en la CPU (el reloj, las pulsaciones, el cbuffer y el reparto de las
//! fuentes); las fuerzas y el pintado, en `nucleo/nodos/pasada_vortice`.

use std::f32::consts::TAU;

use super::color_rgba;
use super::fluido::{contar_celdas, diagnosticar, q_radio};
use super::puente::{DiagnosticoFluido, Escena, FormaFluido, FotogramaVortice, Nodo};

/// La forma del vórtice.
pub struct FormaVortice {
    /// La rejilla y el solver.
    pub fluido: FormaFluido,
    /// Velocidad tangencial máxima del remolino (celdas/s), en el borde del núcleo.
    pub giro: f32,
    /// Radio del núcleo del remolino, en múltiplos del radio.
    pub nucleo: f32,
    /// Velocidad con que el fluido cae hacia el ojo, a media distancia (celdas/s).
    pub entrada: f32,
    /// Cuánto tira el remolino del fluido hacia su giro (1/s).
    pub arrastre: f32,
    /// Cuánto se calienta el fluido al acercarse al ojo (1/s).
    pub calentar: f32,
    /// Dónde empieza la franja del borde por la que entra, en múltiplos del radio.
    pub borde: f32,
    /// Radio del ojo (por donde sale), en múltiplos del radio.
    pub ojo: f32,
    /// Segundos de agitación tras pulsar: después el remolino vuelve a tirar.
    pub agitar: f32,
    /// Aceleración de los chorros de la agitación al pulsar una vez (celdas/s²).
    pub chorro: f32,
    /// Pulsaciones seguidas: cada una suma uno a la racha, que multiplica los chorros, hasta este tope.
    pub racha_max: f32,
    /// Segundos en que la racha cae a 1/e: cuanto más seguidas las pulsaciones, más se agita.
    pub olvido: f32,
    /// Aceleración de los chorros que nunca paran (celdas/s²).
    pub turbulencia: f32,
    /// De la gama: siempre el vivo, haya o no cursor encima.
    pub brillo: f32,
    /// Tope del paso de tiempo (s): tras una pausa no da un salto.
    pub paso_max: f32,
}

pub const FORMA: FormaVortice = FormaVortice {
    fluido: FormaFluido {
        celdas: 128,
        jacobi: 30,
        confinamiento: 0.45,
    },
    giro: 55.0,
    nucleo: 0.15,
    entrada: 3.0,
    arrastre: 2.5,
    calentar: 0.35,
    borde: 0.85,
    ojo: 0.08,
    agitar: 1.8,
    chorro: 800.0,
    racha_max: 4.0,
    olvido: 1.0,
    turbulencia: 150.0,
    brillo: 1.25,
    paso_max: 1.0 / 30.0,
};

const _: () = assert!(
    FORMA.fluido.celdas.is_multiple_of(8),
    "los kernels van en grupos de 8 × 8"
);

/// Al cabo de un día el reloj vuelve a cero: en `f32`, los segundos de varios días ya no distinguen
/// un fotograma del siguiente.
const PERIODO_RELOJ: f64 = 86400.0;

/// El mismo hash que `azar` en los shaders: de 0 a 1, sin el 1.
fn azar(x: u32) -> f32 {
    let mut x = x;
    x ^= x >> 16;
    x = x.wrapping_mul(0x7feb_352d);
    x ^= x >> 15;
    x = x.wrapping_mul(0x846c_a68b);
    x ^= x >> 16;
    (x >> 8) as f32 * (1.0 / 16_777_216.0)
}

/// La disposición de los brazos (las corrientes frías y calientes que entran por el borde): cambia
/// en cada pulsación.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Brazos {
    pub cuantos: f32,
    /// Radianes.
    pub fase: f32,
}

pub fn brazos_de(pulsos: u32) -> Brazos {
    // dos y tres brazos, alternos, con su fase: cada pulsación rehace el remolino de otra forma
    Brazos {
        cuantos: 2.0 + (pulsos % 2) as f32,
        fase: azar(pulsos.wrapping_mul(977).wrapping_add(13)) * TAU,
    }
}

/// Las zonas del vórtice en la rejilla del fluido: la franja del borde (por donde entra) y el ojo
/// (por donde sale), con sus celdas, para que la entrada y la salida se compensen exactas (si no, la
/// presión derivaría).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RejillaVortice {
    /// q ≤ esto: fluido (el resto, pared).
    pub q_fluido: f32,
    /// q > esto (y fluido): franja del borde.
    pub q_borde: f32,
    /// q ≤ esto: el ojo.
    pub q_ojo: f32,
    pub celdas_borde: u32,
    pub celdas_ojo: u32,
}

impl RejillaVortice {
    /// Donde el vórtice mete o saca fluido.
    pub fn con_fuentes(&self, q: f32) -> bool {
        q > self.q_borde || q <= self.q_ojo
    }
}

pub fn rejilla_de(celdas: u32) -> RejillaVortice {
    let q_fluido = q_radio(1.0, celdas);
    let q_borde = q_radio(FORMA.borde, celdas);
    let q_ojo = q_radio(FORMA.ojo, celdas);
    RejillaVortice {
        q_fluido,
        q_borde,
        q_ojo,
        celdas_borde: contar_celdas(celdas, q_borde, q_fluido),
        celdas_ojo: contar_celdas(celdas, -1.0, q_ojo),
    }
}

/// Ver el puente.
pub fn diagnostico_vortice(vel: &[f32], escalar: &[f32], celdas: u32) -> DiagnosticoFluido {
    let r = rejilla_de(celdas);
    diagnosticar(vel, escalar, celdas, |q| r.con_fuentes(q))
}

/// Lleva el reloj y las pulsaciones de un fotograma al siguiente. Al arrancar no agita nada. Cada
/// pulsación agita de nuevo, con otros chorros y otros brazos, y más fuerte cuanto más seguidas
/// lleguen (la racha).
#[derive(Clone, Copy, Debug)]
pub struct EstadoVortice {
    visto: bool,
    pulsos: u32,
    origen: f64,
    anterior: f64,
    pulso: f64,
    /// La racha en la última pulsación: se olvida con [`FormaVortice::olvido`].
    racha: f64,
    brazos: Brazos,
    /// La pulsación que empezó la agitación: su semilla coloca los chorros, y no cambia a mitad.
    semilla: u32,
    rejilla: RejillaVortice,
}

/// Ver el puente.
pub fn estado_vortice() -> Box<EstadoVortice> {
    Box::new(EstadoVortice::default())
}

impl Default for EstadoVortice {
    fn default() -> Self {
        Self {
            visto: false,
            pulsos: 0,
            origen: 0.0,
            anterior: 0.0,
            pulso: -1e9,
            racha: 0.0,
            brazos: Brazos::default(),
            semilla: 0,
            rejilla: rejilla_de(FORMA.fluido.celdas),
        }
    }
}

impl EstadoVortice {
    /// Ver el puente.
    pub fn rellenar(&mut self, escena: &Escena, nodos: &[Nodo], ahora: f64, f: &mut FotogramaVortice) -> bool {
        let indice = escena.vortice;
        let cabe = indice != 0 && indice <= escena.cuenta_nodos && nodos.len() >= indice as usize;
        let escala_buena = escena.escala > 0.0;
        if !cabe || !escala_buena {
            return false;
        }
        let n = nodos[indice as usize - 1];
        if !self.visto {
            self.visto = true;
            self.pulsos = escena.pulsos;
            self.origen = ahora;
            self.anterior = ahora;
            self.brazos = brazos_de(escena.pulsos);
            self.semilla = escena.pulsos;
        }
        if escena.pulsos != self.pulsos {
            // cada pulsación nueva suma uno a la racha, que se olvida con el tiempo (varias en un
            // mismo fotograma suman todas)
            let nuevas = escena.pulsos.wrapping_sub(self.pulsos);
            let queda = self.racha * (-mayor(ahora - self.pulso, 0.0) / f64::from(FORMA.olvido)).exp();
            self.racha = menor(queda + f64::from(nuevas), f64::from(FORMA.racha_max));
            self.pulsos = escena.pulsos;
            self.brazos = brazos_de(escena.pulsos);
            self.semilla = escena.pulsos;
            self.pulso = ahora;
        }
        // el paso de la simulación: lo que pasó desde el fotograma anterior, con tope (tras una
        // pausa, o con la vista oculta, no da un salto)
        f.dt = menor(mayor(ahora - self.anterior, 0.0), f64::from(FORMA.paso_max)) as f32;
        self.anterior = ahora;
        let factor = if escena.factor_radio > 0.0 {
            escena.factor_radio
        } else {
            1.0
        };
        let v = &mut f.cbuffer;
        v.centro = [
            n.x,
            n.y,
            n.radio * factor,
            (mayor(ahora - self.origen, 0.0) % PERIODO_RELOJ) as f32,
        ];
        v.brazos = [
            self.brazos.cuantos,
            self.brazos.fase,
            FORMA.brillo,
            (self.semilla % 65536) as f32,
        ];
        v.agitar = [
            menor(ahora - self.pulso, 1e4) as f32,
            FORMA.agitar,
            FORMA.chorro * self.racha as f32,
            FORMA.turbulencia,
        ];
        v.color = color_rgba(n.rgba);
        v.giro = [FORMA.giro, FORMA.nucleo, FORMA.arrastre, FORMA.calentar];
        // lo que entra por el borde sale por el ojo: el caudal que da la velocidad de entrada a
        // media distancia, repartido entre sus celdas
        let r = &self.rejilla;
        let radio = (FORMA.fluido.celdas as f32 - 2.0) * 0.5;
        let caudal = FORMA.entrada * TAU * radio * 0.5;
        v.fuentes = [
            if r.celdas_borde > 0 {
                caudal / r.celdas_borde as f32
            } else {
                0.0
            },
            if r.celdas_ojo > 0 {
                -caudal / r.celdas_ojo as f32
            } else {
                0.0
            },
            r.q_borde,
            r.q_ojo,
        ];
        true
    }
}

fn menor(a: f64, b: f64) -> f64 {
    if b < a { b } else { a }
}

fn mayor(a: f64, b: f64) -> f64 {
    if a < b { b } else { a }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f32::consts::PI;

    fn cerca(a: f32, b: f32) -> bool {
        (a - b).abs() < 1e-4
    }

    /// El nodo 2 (índice + 1 = 2) de radio 30 en (50, 60).
    fn con_vortice() -> (Escena, Vec<Nodo>) {
        let nodo = |x, y, radio, rgba| Nodo { x, y, radio, rgba };
        let escena = Escena {
            centro_x: 10.0,
            centro_y: -5.0,
            escala: 2.0,
            cuenta_nodos: 3,
            vortice: 2,
            factor_radio: 0.5,
            ..Escena::default()
        };
        (
            escena,
            vec![
                nodo(0.0, 0.0, 10.0, 0),
                nodo(50.0, 60.0, 30.0, 0x74c7_ecff),
                nodo(9.0, 9.0, 10.0, 0),
            ],
        )
    }

    #[test]
    fn toma_su_nodo_y_su_reloj() {
        let mut estado = EstadoVortice::default();
        let mut f = FotogramaVortice::default();
        let (mut e, nodos) = con_vortice();

        e.vortice = 0;
        assert!(!estado.rellenar(&e, &nodos, 100.0, &mut f), "sin vórtice, nada");
        e.vortice = 4;
        assert!(!estado.rellenar(&e, &nodos, 100.0, &mut f), "índice fuera de la escena");

        let (mut e, nodos) = con_vortice();
        assert!(estado.rellenar(&e, &nodos, 100.0, &mut f));
        let v = f.cbuffer;
        assert!(cerca(v.centro[0], 50.0) && cerca(v.centro[1], 60.0) && cerca(v.centro[2], 15.0));
        assert!(cerca(v.centro[3], 0.0) && cerca(f.dt, 0.0), "empieza en cero, sin paso");
        assert!(cerca(v.color[0], 0x74 as f32 / 255.0) && cerca(v.color[3], 1.0));
        assert!(cerca(v.brazos[2], FORMA.brillo));
        assert!(v.agitar[0] >= FORMA.agitar, "no se agita al arrancar");

        e.resaltado = 2;
        estado.rellenar(&e, &nodos, 100.02, &mut f);
        assert!(
            cerca(f.cbuffer.centro[3], 0.02) && cerca(f.dt, 0.02),
            "el paso es lo que pasó"
        );
        assert!(
            cerca(f.cbuffer.brazos[2], FORMA.brillo),
            "con el cursor encima, el mismo brillo"
        );

        estado.rellenar(&e, &nodos, 130.0, &mut f);
        assert!(cerca(f.dt, FORMA.paso_max), "tras una pausa, el paso con tope");
    }

    #[test]
    fn pulsar_lo_agita() {
        let mut estado = EstadoVortice::default();
        let mut f = FotogramaVortice::default();
        let (mut e, nodos) = con_vortice();
        estado.rellenar(&e, &nodos, 10.0, &mut f);
        assert!(cerca(brazos_de(0).cuantos, 2.0) && cerca(brazos_de(1).cuantos, 3.0));

        e.pulsos = 1;
        estado.rellenar(&e, &nodos, 11.0, &mut f);
        let v = f.cbuffer;
        assert!(cerca(v.agitar[0], 0.0), "la agitación empieza al pulsar");
        assert!(cerca(v.brazos[0], 3.0) && cerca(v.brazos[1], brazos_de(1).fase) && cerca(v.brazos[3], 1.0));
        assert!(cerca(v.agitar[2], FORMA.chorro), "una pulsación, los chorros de una");

        // otra pulsación a media agitación vuelve a agitar, con otros chorros, y más fuerte: la
        // racha de antes (1, medio segundo olvidándose) más uno
        e.pulsos = 2;
        estado.rellenar(&e, &nodos, 11.5, &mut f);
        let v = f.cbuffer;
        assert!(cerca(v.agitar[0], 0.0) && cerca(v.brazos[0], 2.0) && cerca(v.brazos[3], 2.0));
        let racha = 1.0 + (-0.5 / FORMA.olvido).exp();
        // en relativo: son cientos de celdas/s² y la racha va en f64
        assert!(
            (v.agitar[2] / (FORMA.chorro * racha) - 1.0).abs() < 1e-5,
            "dos seguidas agitan más"
        );

        // varias en un fotograma suman todas, hasta el tope
        e.pulsos = 12;
        estado.rellenar(&e, &nodos, 11.6, &mut f);
        assert!(
            cerca(f.cbuffer.agitar[2], FORMA.chorro * FORMA.racha_max),
            "la racha tiene tope"
        );

        // tras una pausa larga la racha se olvida
        e.pulsos = 13;
        estado.rellenar(&e, &nodos, 40.0, &mut f);
        let v = f.cbuffer;
        assert!(
            cerca(v.agitar[0], 0.0) && cerca(v.agitar[2], FORMA.chorro),
            "otra vez la de una"
        );
    }

    /// El círculo, la franja del borde y el ojo tienen las celdas que toca, y lo que entra por el
    /// borde sale por el ojo (si no, la presión derivaría).
    #[test]
    fn la_rejilla_cuadra() {
        let r = rejilla_de(FORMA.fluido.celdas);
        let radio = (FORMA.fluido.celdas as f32 - 2.0) * 0.5;
        let ojo = PI * (FORMA.ojo * radio) * (FORMA.ojo * radio);
        let borde = PI * radio * radio * (1.0 - FORMA.borde * FORMA.borde);
        assert!((r.celdas_ojo as f32 - ojo).abs() < 0.15 * ojo, "el ojo");
        assert!(
            (r.celdas_borde as f32 - borde).abs() < 0.05 * borde,
            "la franja del borde"
        );

        let mut estado = EstadoVortice::default();
        let mut f = FotogramaVortice::default();
        let (e, nodos) = con_vortice();
        estado.rellenar(&e, &nodos, 1.0, &mut f);
        let v = f.cbuffer;
        let neto = v.fuentes[0] * r.celdas_borde as f32 + v.fuentes[1] * r.celdas_ojo as f32;
        assert!(v.fuentes[0] > 0.0 && v.fuentes[1] < 0.0);
        assert!(
            neto.abs() < 1e-3 * v.fuentes[0] * r.celdas_borde as f32,
            "entra lo que sale"
        );
        assert!(cerca(v.fuentes[2], r.q_borde) && cerca(v.fuentes[3], r.q_ojo));
        assert!(cerca(v.agitar[3], FORMA.turbulencia));
    }
}
