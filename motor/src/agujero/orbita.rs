//! La cámara que orbita el agujero: arrastrando con el ratón gira libre en
//! todos los ejes, y al soltar sigue con inercia hasta pararse.
//!
//! - Botón izquierdo: orbitar en x e y (la cámara da la vuelta al agujero sobre
//!   sus propios ejes: pasa por encima de los polos sin pararse).
//! - Botón derecho, en horizontal: rodar sobre la línea de vista (z: la imagen
//!   gira sobre sí misma).
//! - Sola (deriva): avanza en x e y a velocidad fija, con un rumbo (en el plano
//!   de la imagen) que gira al azar, despacio: no repite camino y, a la larga,
//!   ve el agujero desde todos los ángulos por igual (nunca rueda). Al orbitar
//!   con el botón izquierdo se para (la mueves tú) y al soltar vuelve poco a
//!   poco; mientras ruedas con el derecho sigue, por el mismo camino (solo gira
//!   la imagen).
//!
//! La orientación es una base (derecha, arriba, atrás; atrás, del agujero a la
//! cámara). El agujero es simétrico alrededor de su eje de giro (z), así que se
//! parte en dos: el azimut (el giro alrededor de z, que no obliga a volver a
//! trazar los rayos: basta girar el disco y el cielo al componer) y la base de
//! trazado (la de la cámara llevada a azimut 0; si cambia, hay que volver a
//! trazar: ver [`Orbita::moviendose`]).

use crate::agujero::puente::{BaseCamara, Cursor, DescOrbita};

type Vector3 = [f64; 3];

/// Por debajo, la inercia se da por parada (rad/s).
const QUIETA: f64 = 1e-3;

/// Lo que tarda la deriva en volver del todo tras soltar (s).
const VUELTA_DERIVA: f64 = 2.0;

/// Cada cuánto cambia de idea su rumbo (s): el ruido, un valor al azar cada
/// tanto, interpolado suave.
const IDEA_RUMBO: f64 = 3.0;

/// Un valor al azar, de −1 a 1, para cada entero (splitmix64: siempre el mismo,
/// la prueba y el oro se repiten).
fn al_azar(i: i64) -> f64 {
    let mut z = (i as u64).wrapping_add(0x9E37_79B9_7F4A_7C15);
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    z ^= z >> 31;
    (z >> 11) as f64 / 4503599627370496.0 - 1.0
}

/// Ruido suave de −1 a 1: al azar en cada entero, y entre medias, la curva
/// suave que los une.
fn ruido(x: f64) -> f64 {
    let b = x.floor();
    let f = x - b;
    let s = f * f * (3.0 - 2.0 * f);
    let i = b as i64;
    al_azar(i) + (al_azar(i + 1) - al_azar(i)) * s
}

fn punto(a: &Vector3, b: &Vector3) -> f64 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn cruz(a: &Vector3, b: &Vector3) -> Vector3 {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

fn mas(a: &Vector3, b: &Vector3, k: f64) -> Vector3 {
    [a[0] + b[0] * k, a[1] + b[1] * k, a[2] + b[2] * k]
}

fn por(a: &Vector3, k: f64) -> Vector3 {
    [a[0] * k, a[1] * k, a[2] * k]
}

fn unitario(a: &Vector3) -> Vector3 {
    let n = punto(a, a).sqrt();
    if n > 0.0 { [a[0] / n, a[1] / n, a[2] / n] } else { *a }
}

/// `v` girado el ángulo |w| alrededor de `w` (Rodrigues).
fn rodrigues(v: &Vector3, w: &Vector3) -> Vector3 {
    let angulo = punto(w, w).sqrt();
    if angulo < 1e-15 {
        return *v;
    }
    let k = [w[0] / angulo, w[1] / angulo, w[2] / angulo];
    let (c, s) = (angulo.cos(), angulo.sin());
    mas(&mas(&por(v, c), &cruz(&k, v), s), &k, punto(&k, v) * (1.0 - c))
}

/// Alrededor de z, el ángulo `a`.
fn girar_z(v: &Vector3, a: f64) -> Vector3 {
    [v[0] * a.cos() - v[1] * a.sin(), v[0] * a.sin() + v[1] * a.cos(), v[2]]
}

#[derive(Clone, Copy, Debug)]
pub struct Orbita {
    d: DescOrbita,
    base: BaseCamara,
    trazado: BaseCamara,
    azimut: f64,
    /// La inercia: la velocidad angular (rad/s, en el sistema del agujero).
    giro: Vector3,
    arrastrando: bool,
    moviendose: bool,
    ultimo: [f32; 2],
    /// La deriva: su reloj (s), su rumbo (rad, desde la derecha de la imagen
    /// hacia arriba) y cuánto ha vuelto tras soltar (de 0 a 1).
    reloj: f64,
    rumbo: f64,
    peso_deriva: f64,
}

impl Orbita {
    pub fn nueva(d: DescOrbita) -> Self {
        // la de partida: como la cámara de antes (con azimut 0, derecha = +y,
        // también sobre el eje), girada el azimut
        let i = d.inclinacion;
        let atras = [i.sin(), 0.0, i.cos()];
        let derecha = [0.0, 1.0, 0.0];
        let arriba = cruz(&derecha, &[-atras[0], -atras[1], -atras[2]]);
        let base = BaseCamara {
            derecha: girar_z(&derecha, d.azimut),
            arriba: girar_z(&arriba, d.azimut),
            atras: girar_z(&atras, d.azimut),
        };
        let cero = [0.0; 3];
        let mut orbita = Orbita {
            d,
            base,
            trazado: BaseCamara {
                derecha: cero,
                arriba: cero,
                atras: cero,
            },
            azimut: 0.0,
            giro: cero,
            arrastrando: false,
            moviendose: false,
            ultimo: [0.0; 2],
            reloj: 0.0,
            rumbo: 0.0,
            peso_deriva: 1.0,
        };
        orbita.descomponer();
        orbita
    }

    /// Gira la base el vector de giro `w` (eje × ángulo) y la deja ortonormal.
    fn girar(&mut self, w: &Vector3) {
        self.base.atras = unitario(&rodrigues(&self.base.atras, w));
        self.base.derecha = rodrigues(&self.base.derecha, w);
        // ortonormal otra vez (los errores de redondeo se acumulan)
        let d = self.base.derecha;
        self.base.derecha = unitario(&mas(&d, &self.base.atras, -punto(&d, &self.base.atras)));
        self.base.arriba = cruz(&self.base.atras, &self.base.derecha);
    }

    /// El azimut y la base de trazado, de la base.
    fn descomponer(&mut self) {
        let antes = self.trazado;
        let b = self.base.atras;
        self.azimut = if b[0].hypot(b[1]) > 1e-12 {
            b[1].atan2(b[0])
        } else {
            0.0
        };
        self.trazado = BaseCamara {
            derecha: girar_z(&self.base.derecha, -self.azimut),
            arriba: girar_z(&self.base.arriba, -self.azimut),
            atras: girar_z(&self.base.atras, -self.azimut),
        };
        let mut cambio: f64 = 0.0;
        for k in 0..3 {
            cambio = cambio.max((self.trazado.derecha[k] - antes.derecha[k]).abs());
            cambio = cambio.max((self.trazado.arriba[k] - antes.arriba[k]).abs());
            cambio = cambio.max((self.trazado.atras[k] - antes.atras[k]).abs());
        }
        self.moviendose = cambio > 1e-9;
    }

    /// Un fotograma de `paso` s con el cursor así.
    pub fn avanzar(&mut self, c: Cursor, paso: f64) {
        let agarrada = c.dentro && (c.pulsado || c.rodar);
        if agarrada {
            self.arrastrar(c, paso);
            self.ultimo = [c.x, c.y];
        } else {
            self.soltar(paso);
        }
        // la deriva: avanza hacia su rumbo (gira alrededor de atrás × rumbo, en
        // el plano de derecha y arriba: nunca rueda), y el rumbo gira al azar.
        // Sigue también mientras ruedas (botón derecho); al orbitar con el
        // izquierdo la mueves tú: se para, y al soltar vuelve poco a poco
        if agarrada && c.pulsado {
            self.peso_deriva = 0.0;
        } else {
            self.derivar(paso);
        }
        self.reloj += paso;
        self.descomponer();
    }

    fn arrastrar(&mut self, c: Cursor, paso: f64) {
        // al agarrarlo se para en seco: lo estás sujetando
        if !self.arrastrando {
            self.arrastrando = true;
            self.giro = [0.0; 3];
            return;
        }
        let dx = f64::from(c.x - self.ultimo[0]) * self.d.sensibilidad;
        let dy = f64::from(c.y - self.ultimo[1]) * self.d.sensibilidad;
        let w = if c.pulsado {
            // orbitar: la cámara se mueve hacia su izquierda al arrastrar a la
            // derecha y hacia abajo al arrastrar hacia abajo (el agujero sigue
            // al ratón), girando alrededor de atrás × ese sentido
            let sentido = mas(&por(&self.base.derecha, -dx), &self.base.arriba, -dy);
            cruz(&self.base.atras, &sentido)
        } else {
            // rodar: alrededor de la vista; a la derecha, la imagen gira como
            // las agujas del reloj
            por(&self.base.atras, dx)
        };
        // lo que rueda no cambia el camino de la deriva (su rumbo va en el
        // plano de la imagen, que gira con lo rodado)
        self.rumbo -= punto(&w, &self.base.atras);
        self.girar(&w);
        // la inercia que deja al soltar: lo de los últimos fotogramas
        if paso > 0.0 {
            self.giro = mas(&por(&self.giro, 0.5), &w, 0.5 / paso);
        }
    }

    fn soltar(&mut self, paso: f64) {
        self.arrastrando = false;
        self.rumbo -= punto(&self.giro, &self.base.atras) * paso;
        let giro = por(&self.giro, paso);
        self.girar(&giro);
        let frena = (-self.d.frenado * paso).exp();
        self.giro = por(&self.giro, frena);
        if punto(&self.giro, &self.giro).sqrt() < QUIETA {
            self.giro = [0.0; 3];
        }
    }

    fn derivar(&mut self, paso: f64) {
        self.peso_deriva = (self.peso_deriva + paso / VUELTA_DERIVA).min(1.0);
        self.rumbo += self.d.rumbo * ruido(self.reloj / IDEA_RUMBO) * paso;
        let k = self.d.deriva * self.peso_deriva * paso;
        let sentido = mas(
            &por(&self.base.derecha, self.rumbo.cos()),
            &self.base.arriba,
            self.rumbo.sin(),
        );
        let w = cruz(&self.base.atras, &sentido);
        if k != 0.0 {
            self.girar(&por(&w, k));
        }
    }

    /// La orientación de la cámara.
    pub fn base(&self) -> &BaseCamara {
        &self.base
    }

    /// El azimut (rad).
    pub fn azimut(&self) -> f64 {
        self.azimut
    }

    /// La base de la cámara girada −azimut alrededor de z: su «atrás» en el
    /// plano xz, con x ≥ 0.
    pub fn base_trazado(&self) -> &BaseCamara {
        &self.trazado
    }

    /// Si la base de trazado cambió en el último paso (hay que volver a trazar).
    pub fn moviendose(&self) -> bool {
        self.moviendose
    }

    pub fn arrastrando(&self) -> bool {
        self.arrastrando
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::{FRAC_PI_2, PI, TAU};

    const GRADO: f64 = PI / 180.0;

    fn cerca(a: f64, b: f64, margen: f64) -> bool {
        (a - b).abs() < margen
    }

    /// El ángulo entre dos vectores unitarios.
    fn angulo(a: &Vector3, b: &Vector3) -> f64 {
        punto(a, b).clamp(-1.0, 1.0).acos()
    }

    fn ortonormal(b: &BaseCamara) -> bool {
        cerca(punto(&b.derecha, &b.derecha), 1.0, 1e-9)
            && cerca(punto(&b.arriba, &b.arriba), 1.0, 1e-9)
            && cerca(punto(&b.atras, &b.atras), 1.0, 1e-9)
            && cerca(punto(&b.derecha, &b.arriba), 0.0, 1e-9)
            && cerca(punto(&b.derecha, &b.atras), 0.0, 1e-9)
            && cerca(punto(&b.arriba, &b.atras), 0.0, 1e-9)
    }

    fn cursor(x: f32, y: f32, pulsado: bool, dentro: bool, rodar: bool) -> Cursor {
        Cursor {
            x,
            y,
            pulsado,
            dentro,
            rodar,
        }
    }

    fn agarrado(x: f32, y: f32) -> Cursor {
        cursor(x, y, true, true, false)
    }

    fn suelto(x: f32, y: f32) -> Cursor {
        cursor(x, y, false, true, false)
    }

    const FUERA: Cursor = Cursor {
        x: 0.0,
        y: 0.0,
        pulsado: false,
        dentro: false,
        rodar: false,
    };

    fn con_deriva() -> DescOrbita {
        DescOrbita {
            sensibilidad: 0.01,
            frenado: 2.0,
            deriva: 8.0 * GRADO,
            rumbo: 25.0 * GRADO,
            ..DescOrbita::default()
        }
    }

    fn sin_deriva() -> DescOrbita {
        DescOrbita {
            sensibilidad: 0.01,
            frenado: 2.0,
            ..DescOrbita::default()
        }
    }

    /// Sola, la cámara nunca rueda (el eje de cada paso, sin nada sobre la
    /// vista), no se para, y en dos horas ve el agujero desde todos los ángulos
    /// por igual: desde una esfera de puntos de vista uniforme, z (el coseno de
    /// la inclinación) reparte por igual en [−1, 1] y el azimut también.
    #[test]
    fn la_orbita_deriva_por_todos_los_angulos() {
        let dt = 1.0 / 30.0;
        let mut o = Orbita::nueva(con_deriva());
        let (mut rueda, mut moviendose, mut pasos) = (0.0_f64, 0, 0);
        let (mut z, mut azimut) = ([0_u32; 4], [0_u32; 4]);
        for _ in 0..2 * 3600 * 30 {
            let antes = *o.base();
            o.avanzar(FUERA, dt);
            let b = *o.base();
            // el eje del giro de este paso: ½ Σ antes × ahora (= sen(ángulo) · eje)
            let mut w = [0.0; 3];
            for (p, q) in [
                (antes.derecha, b.derecha),
                (antes.arriba, b.arriba),
                (antes.atras, b.atras),
            ] {
                w = mas(&w, &cruz(&p, &q), 0.5);
            }
            let n = punto(&w, &w).sqrt();
            if n > 1e-12 {
                rueda = rueda.max(punto(&w, &antes.atras).abs() / n);
            }
            moviendose += u32::from(o.moviendose());
            pasos += 1;
            z[(((b.atras[2] + 1.0) * 2.0) as usize).min(3)] += 1;
            azimut[(((b.atras[1].atan2(b.atras[0]) + PI) / FRAC_PI_2) as usize).min(3)] += 1;
        }
        let parejo = |cuenta: u32| (f64::from(cuenta) / f64::from(pasos) - 0.25).abs() < 0.04;
        assert!(rueda < 1e-3, "sola no rueda: {rueda}");
        assert!(moviendose > pasos * 99 / 100, "sola no se para");
        assert!(z.iter().chain(&azimut).all(|&c| parejo(c)), "{z:?} {azimut:?}");
    }

    /// Rodando con el botón derecho sigue, por el mismo camino que sin rodar
    /// (solo gira la imagen); orbitando con el izquierdo, sin mover el ratón,
    /// se para.
    #[test]
    fn rodando_sigue_y_orbitando_se_para() {
        let dt = 1.0 / 30.0;
        let mut r = Orbita::nueva(con_deriva());
        for _ in 0..60 {
            r.avanzar(FUERA, dt);
        }
        let (mut rodando, mut sin_rodar, mut sujeta) = (r, r, r);
        let antes = r.base().atras;
        for k in 0..=60_u16 {
            rodando.avanzar(cursor(f32::from(2 * k), 0.0, false, true, true), dt);
            sin_rodar.avanzar(FUERA, dt);
            sujeta.avanzar(agarrado(0.0, 0.0), dt);
        }
        assert!(angulo(&rodando.base().atras, &sin_rodar.base().atras) < 1e-9);
        assert!(angulo(&rodando.base().atras, &antes) > 0.1);
        assert!(cerca(
            angulo(&rodando.base().derecha, &sin_rodar.base().derecha),
            1.2,
            1e-6
        ));
        assert!(
            angulo(&sujeta.base().atras, &antes) < 1e-12,
            "orbitando con el izquierdo se para"
        );
    }

    #[test]
    fn de_frente_la_base_de_antes() {
        let o = Orbita::nueva(sin_deriva());
        let b = o.base();
        assert!(cerca(b.atras[2], 1.0, 1e-9) && cerca(b.derecha[1], 1.0, 1e-9) && cerca(b.arriba[0], -1.0, 1e-9));
        assert!(cerca(o.azimut(), 0.0, 1e-9));
    }

    #[test]
    fn hacia_abajo_se_inclina_y_hay_que_trazar() {
        let mut o = Orbita::nueva(sin_deriva());
        let dt = 1.0 / 60.0;
        o.avanzar(agarrado(0.0, 0.0), dt);
        o.avanzar(agarrado(0.0, 100.0), dt);
        assert!(cerca(angulo(&o.base().atras, &[0.0, 0.0, 1.0]), 1.0, 1e-9));
        assert!(o.moviendose());
    }

    /// Sigue hasta pasar por encima del polo de abajo y volver: una vuelta
    /// entera son 2π / 0,01 ≈ 628 px.
    #[test]
    fn una_vuelta_entera_sin_pararse_en_los_polos() {
        let dt = 1.0 / 60.0;
        let mut v = Orbita::nueva(sin_deriva());
        v.avanzar(agarrado(0.0, 0.0), dt);
        for k in 1..=100 {
            v.avanzar(agarrado(0.0, (f64::from(k) * TAU / 0.01 / 100.0) as f32), dt);
        }
        assert!(cerca(angulo(&v.base().atras, &[0.0, 0.0, 1.0]), 0.0, 1e-4));
        assert!(cerca(angulo(&v.base().derecha, &[0.0, 1.0, 0.0]), 0.0, 1e-4));
    }

    #[test]
    fn el_boton_derecho_rueda_sobre_la_vista() {
        let dt = 1.0 / 60.0;
        let mut r = Orbita::nueva(sin_deriva());
        r.avanzar(cursor(0.0, 0.0, false, true, true), dt);
        r.avanzar(cursor(50.0, 0.0, false, true, true), dt);
        assert!(cerca(angulo(&r.base().atras, &[0.0, 0.0, 1.0]), 0.0, 1e-9));
        assert!(cerca(angulo(&r.base().derecha, &[0.0, 1.0, 0.0]), 0.5, 1e-9));
    }

    /// La base, siempre ortonormal, y el azimut y la base de trazado la
    /// rehacen: la de trazado tiene atrás en el plano xz (x ≥ 0).
    #[test]
    fn la_base_sigue_ortonormal_y_se_rehace() {
        let dt = 1.0 / 60.0;
        let mut m = Orbita::nueva(sin_deriva());
        m.avanzar(agarrado(0.0, 0.0), dt);
        for k in 1..=300_u16 {
            m.avanzar(
                cursor(
                    f32::from(37 * k % 211),
                    f32::from(53 * k % 197),
                    k % 3 != 0,
                    true,
                    k % 3 == 0,
                ),
                dt,
            );
        }
        let (b, t, a) = (m.base(), m.base_trazado(), m.azimut());
        assert!(ortonormal(b) && ortonormal(t));
        assert!(cerca(t.atras[1], 0.0, 1e-9) && t.atras[0] >= -1e-9);
        assert!(cerca(angulo(&girar_z(&t.atras, a), &b.atras), 0.0, 1e-9));
    }

    /// Girar solo alrededor del eje de giro (z) no obliga a trazar: de canto,
    /// con la cámara derecha, arrastrar en horizontal.
    #[test]
    fn de_canto_en_horizontal_solo_el_azimut() {
        let dt = 1.0 / 60.0;
        let mut c = Orbita::nueva(DescOrbita {
            inclinacion: FRAC_PI_2,
            ..sin_deriva()
        });
        c.avanzar(agarrado(0.0, 0.0), dt);
        c.avanzar(agarrado(30.0, 0.0), dt);
        assert!(cerca(c.azimut(), -0.3, 1e-9));
        assert!(!c.moviendose());
    }

    #[test]
    fn suelta_sigue_con_inercia_y_se_para_sola() {
        let dt = 1.0 / 60.0;
        let mut o = Orbita::nueva(sin_deriva());
        o.avanzar(agarrado(0.0, 0.0), dt);
        o.avanzar(agarrado(0.0, 100.0), dt);
        o.avanzar(suelto(0.0, 100.0), dt);
        let suelta = o.base().atras;
        o.avanzar(suelto(0.0, 100.0), dt);
        assert!(angulo(&o.base().atras, &suelta) > 1e-6 && !o.arrastrando());
        for _ in 0..600 {
            o.avanzar(suelto(0.0, 100.0), dt);
        }
        let parada = o.base().atras;
        o.avanzar(suelto(0.0, 100.0), dt);
        assert!(cerca(angulo(&o.base().atras, &parada), 0.0, 1e-12) && !o.moviendose());
    }

    #[test]
    fn al_agarrarla_se_para() {
        let dt = 1.0 / 60.0;
        let mut p = Orbita::nueva(sin_deriva());
        p.avanzar(agarrado(0.0, 0.0), dt);
        p.avanzar(agarrado(0.0, 30.0), dt);
        p.avanzar(suelto(0.0, 30.0), dt);
        p.avanzar(agarrado(0.0, 30.0), dt);
        let sujeta = p.base().atras;
        p.avanzar(agarrado(0.0, 30.0), dt);
        assert!(cerca(angulo(&p.base().atras, &sujeta), 0.0, 1e-12));
    }
}
