//! Los rayos de luz alrededor de un agujero negro de Kerr (que gira), en
//! coordenadas de Kerr-Schild cartesianas: sin singularidades en los polos, y
//! el horizonte se cruza sin problemas. Masa M = 1 (todo en unidades de M), eje
//! de giro z, firma (−, +, +, +). El giro `a` va de 0 (Schwarzschild) a 1.
//!
//! Un rayo se traza hacia atrás, desde la cámara: su momento tiene p_t = +1 (el
//! del fotón, −1), y el camino es el mismo que recorrió la luz. Las ecuaciones
//! de Hamilton se integran con RK4 y un paso que se acorta cerca del horizonte.
//!
//! La referencia del HLSL de los rayos (`rayos_kerr` y `cbuffer_agujero`), que
//! hace exactamente lo mismo en float. A la GPU solo le hacen falta
//! [`horizonte`] e [`isco`]; lo demás prueba la física que ese HLSL copia.

/// Ver [`trazar`]. La órbita de fotones a favor del giro está siempre más lejos
/// del horizonte (con a = 0,9, a 0,12; con a = 0,99, a 0,027).
const MARGEN_HORIZONTE: f64 = 0.01;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec3 {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}

/// Un vector, sin nombrar sus campos.
pub const fn vec3(x: f64, y: f64, z: f64) -> Vec3 {
    Vec3 { x, y, z }
}

impl Vec3 {
    fn punto(self, v: Vec3) -> f64 {
        self.x * v.x + self.y * v.y + self.z * v.z
    }

    fn mas(self, v: Vec3, k: f64) -> Vec3 {
        vec3(self.x + v.x * k, self.y + v.y * k, self.z + v.z * k)
    }

    fn unitario(self) -> Vec3 {
        let n = self.punto(self).sqrt();
        if n > 0.0 {
            vec3(self.x / n, self.y / n, self.z / n)
        } else {
            self
        }
    }
}

/// La métrica en un punto: g = η + f l l, con r el radio de Kerr-Schild y l el
/// vector nulo (su parte espacial, covariante).
struct Metrica {
    r: f64,
    f: f64,
    l: Vec3,
}

fn metrica(x: Vec3, a: f64) -> Metrica {
    let a2 = a * a;
    let b = x.punto(x) - a2;
    let r2 = 0.5 * (b + (b * b + 4.0 * a2 * x.z * x.z).sqrt());
    let r = r2.max(1e-30).sqrt();
    let d = r2 + a2;
    Metrica {
        r,
        f: 2.0 * r * r2 / (r2 * r2 + a2 * x.z * x.z),
        l: vec3((r * x.x + a * x.y) / d, (r * x.y - a * x.x) / d, x.z / r),
    }
}

/// F = f (l·p)², con l·p = −p_t + l_i p_i y p_t = +1: lo que varía de H con la
/// posición (dp/dλ = ½ ∇F).
pub fn efe(x: Vec3, p: Vec3, a: f64) -> f64 {
    let m = metrica(x, a);
    let lp = -1.0 + m.l.punto(p);
    m.f * lp * lp
}

/// ∇F exacto. De r⁴ − (R² − a²) r² − a² z² = 0: ∂r/∂x = r³ x / Q, ∂r/∂y =
/// r³ y / Q, ∂r/∂z = z r (r² + a²) / Q, con Q = r⁴ + a² z². Luego f = 2 r³ / Q
/// y l·p = −1 + N / (r² + a²) + z p_z / r, con N = r (x p_x + y p_y) + a (y
/// p_x − x p_y).
pub fn gradiente_efe(x: Vec3, p: Vec3, a: f64) -> Vec3 {
    let a2 = a * a;
    let r = metrica(x, a).r;
    let r2 = r * r;
    let q = r2 * r2 + a2 * x.z * x.z;
    let d = r2 + a2;
    let dr = [r2 * r * x.x / q, r2 * r * x.y / q, x.z * r * d / q];
    let s = x.x * p.x + x.y * p.y;
    let t = x.y * p.x - x.x * p.y;
    let n = r * s + a * t;
    let f = 2.0 * r2 * r / q;
    let lp = -1.0 + n / d + x.z * p.z / r;
    let ds = [p.x, p.y, 0.0];
    let dt = [-p.y, p.x, 0.0];
    let componente = |i: usize| {
        let dz = if i == 2 { 1.0 } else { 0.0 };
        let df = 2.0 * r2 * ((3.0 * q - 4.0 * r2 * r2) * dr[i] - 2.0 * a2 * r * x.z * dz) / (q * q);
        let dn = dr[i] * s + r * ds[i] + a * dt[i];
        let dlp = (dn * d - n * 2.0 * r * dr[i]) / (d * d) + p.z * (dz * r - x.z * dr[i]) / r2;
        df * lp * lp + 2.0 * f * lp * dlp
    };
    vec3(componente(0), componente(1), componente(2))
}

/// El horizonte (r₊).
pub fn horizonte(a: f64) -> f64 {
    1.0 + (1.0 - a * a).max(0.0).sqrt()
}

/// La órbita circular estable más interna, en el sentido del giro (Bardeen,
/// Press y Teukolsky, 1972).
pub fn isco(a: f64) -> f64 {
    let z1 = 1.0 + (1.0 - a * a).cbrt() * ((1.0 + a).cbrt() + (1.0 - a).cbrt());
    let z2 = (3.0 * a * a + z1 * z1).sqrt();
    3.0 + z2 - ((3.0 - z1) * (3.0 + z1 + 2.0 * z2)).max(0.0).sqrt()
}

/// El radio de Kerr-Schild (el r de Boyer-Lindquist) de un punto.
pub fn radio_ks(p: Vec3, a: f64) -> f64 {
    metrica(p, a).r
}

/// La posición y el momento espacial (p_t = +1) de un rayo.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct EstadoRayo {
    pub x: Vec3,
    pub p: Vec3,
}

/// H = ½ g^{μν} p_μ p_ν: 0 en un rayo de luz (se conserva al integrar).
pub fn hamiltoniano(s: EstadoRayo, a: f64) -> f64 {
    let m = metrica(s.x, a);
    let lp = -1.0 + m.l.punto(s.p);
    // η^{μν} p_μ p_ν = −p_t² + |p|²
    0.5 * (-1.0 + s.p.punto(s.p) - m.f * lp * lp)
}

/// dx/dλ y dp/dλ.
pub fn derivada(s: EstadoRayo, a: f64) -> EstadoRayo {
    let m = metrica(s.x, a);
    let lp = -1.0 + m.l.punto(s.p);
    let g = gradiente_efe(s.x, s.p, a);
    EstadoRayo {
        // dx/dλ = ∂H/∂p = p − f (l·p) l
        x: s.p.mas(m.l, -m.f * lp),
        // dp/dλ = −∂H/∂x = ½ ∇F
        p: vec3(0.5 * g.x, 0.5 * g.y, 0.5 * g.z),
    }
}

fn mover(b: EstadoRayo, d: EstadoRayo, k: f64) -> EstadoRayo {
    EstadoRayo {
        x: b.x.mas(d.x, k),
        p: b.p.mas(d.p, k),
    }
}

/// k₁ + 2 k₂ + 2 k₃ + k₄.
fn suma_rk4(k1: Vec3, k2: Vec3, k3: Vec3, k4: Vec3) -> Vec3 {
    vec3(
        k1.x + 2.0 * k2.x + 2.0 * k3.x + k4.x,
        k1.y + 2.0 * k2.y + 2.0 * k3.y + k4.y,
        k1.z + 2.0 * k2.z + 2.0 * k3.z + k4.z,
    )
}

pub fn paso_rk4(s: &mut EstadoRayo, a: f64, h: f64) {
    let k1 = derivada(*s, a);
    let k2 = derivada(mover(*s, k1, h / 2.0), a);
    let k3 = derivada(mover(*s, k2, h / 2.0), a);
    let k4 = derivada(mover(*s, k3, h), a);
    s.x = s.x.mas(suma_rk4(k1.x, k2.x, k3.x, k4.x), h / 6.0);
    s.p = s.p.mas(suma_rk4(k1.p, k2.p, k3.p, k4.p), h / 6.0);
}

/// El paso para un rayo a radio `r`: proporcional a lo lejos que está del
/// horizonte, con tope. Lejos del agujero el espacio es casi plano y el rayo va
/// casi recto: el paso crece además con r (a partir de 8, proporcional).
pub fn paso_en(r: f64, a: f64, precision: f64) -> f64 {
    precision * (r - horizonte(a)).clamp(0.01, 50.0) * (r / 8.0).max(1.0)
}

/// El rayo que sale de la cámara en `direccion` (unitaria): el momento que hace
/// H = 0.
pub fn rayo_desde(camara: Vec3, direccion: Vec3, a: f64) -> EstadoRayo {
    let n = direccion.unitario();
    let m = metrica(camara, a);
    // p = s n con H = 0: s² (1 − f ln²) + 2 f ln s − (1 + f) = 0
    let ln = m.l.punto(n);
    let (ca, cb, cc) = (1.0 - m.f * ln * ln, 2.0 * m.f * ln, -(1.0 + m.f));
    let s = (-cb + (cb * cb - 4.0 * ca * cc).sqrt()) / (2.0 * ca);
    EstadoRayo {
        x: camara,
        p: vec3(n.x * s, n.y * s, n.z * s),
    }
}

/// El azimut de Kerr-Schild de un punto del plano z = 0 (el que gira con el
/// disco: a radio fijo, avanza como el φ de Boyer-Lindquist).
pub fn azimut(p: Vec3, a: f64) -> f64 {
    p.y.atan2(p.x) + a.atan2(radio_ks(p, a))
}

#[derive(Clone, Copy, Debug, Default)]
pub struct DescTrazado {
    /// El borde interior del disco.
    pub r_in: f64,
    /// El exterior.
    pub r_out: f64,
    /// A esta distancia del centro, el rayo ya escapó.
    pub lejos: f64,
    pub max_pasos: u32,
    /// El paso, en fracción de lo lejos que está del horizonte.
    pub precision: f64,
    /// El rayo que pasa más cerca del centro que esto ya no recoge disco ni ve
    /// el cielo (0: todos): quita el anillo de fotones pegado a la sombra (con
    /// giro, en el lado que gira hacia ti se ve como un reflejo dentro de la
    /// esfera negra).
    pub rozar: f64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Cruce {
    pub r: f64,
    pub fi: f64,
}

/// Lo que encuentra un rayo: hasta 3 cruces con el disco (el primero, el más
/// cercano a la cámara), y si cae en el agujero o escapa (con la dirección en
/// la que se va, para el cielo).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Trazado {
    pub cruces: usize,
    pub cruce: [Cruce; 3],
    pub cae: bool,
    pub cielo: Vec3,
    pub pasos: u32,
}

pub fn trazar(mut s: EstadoRayo, a: f64, d: &DescTrazado) -> Trazado {
    let mut t = Trazado::default();
    let r_h = horizonte(a);
    let mut r_min = 1e30_f64;
    while t.pasos < d.max_pasos {
        let r = radio_ks(s.x, a);
        r_min = r_min.min(r);
        // ya rozó: será sombra (sin más disco ni cielo), no hace falta seguirlo.
        // Cae: hacia atrás, en estas coordenadas (las de Kerr-Schild que entran)
        // el rayo que cae no cruza el horizonte: se le acerca sin fin (es el
        // horizonte del pasado) y su momento crece sin tope. A MARGEN_HORIZONTE
        // de él ya no vuelve a salir
        if r_min < d.rozar || r < r_h + MARGEN_HORIZONTE {
            t.cae = true;
            return t;
        }
        if s.x.punto(s.x) > d.lejos * d.lejos && s.x.punto(s.p) > 0.0 {
            break;
        }
        let antes = s.x;
        paso_rk4(&mut s, a, paso_en(r, a, d.precision));
        // cruza el plano del disco: el punto de cruce, interpolado
        if (antes.z > 0.0) != (s.x.z > 0.0) && t.cruces < 3 {
            let k = antes.z / (antes.z - s.x.z);
            let q = vec3(antes.x + (s.x.x - antes.x) * k, antes.y + (s.x.y - antes.y) * k, 0.0);
            let rq = radio_ks(q, a);
            if rq >= d.r_in && rq <= d.r_out && r_min >= d.rozar {
                t.cruce[t.cruces] = Cruce {
                    r: rq,
                    fi: azimut(q, a),
                };
                t.cruces += 1;
            }
        }
        t.pasos += 1;
    }
    // escapó (o se acabaron los pasos lejos del agujero): hacia donde va; el
    // que rozó (ver DescTrazado::rozar), tampoco ve el cielo
    t.cae = r_min < d.rozar;
    t.cielo = s.p.unitario();
    t
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cerca(a: f64, b: f64) -> bool {
        (a - b).abs() < 1e-9
    }

    const TRAZADO_DE_PRUEBA: DescTrazado = DescTrazado {
        r_in: 1e9,
        r_out: 1e9,
        lejos: 60.0,
        max_pasos: 20000,
        precision: 0.02,
        rozar: 0.0,
    };

    /// Un rayo que sale de x = −50 hacia +x en el plano z = 0 con parámetro de
    /// impacto b = L / E (L = x p_y − y p_x, E = 1). En Kerr-Schild el momento
    /// en la cámara no mide 1, así que la altura y se ajusta hasta que L sea b.
    fn con_impacto(b: f64, a: f64) -> EstadoRayo {
        let mut y = b;
        let mut s = EstadoRayo::default();
        for _ in 0..8 {
            s = rayo_desde(vec3(-50.0, y, 0.0), vec3(1.0, 0.0, 0.0), a);
            let l = s.x.x * s.p.y - s.x.y * s.p.x;
            y *= b / -l;
        }
        s
    }

    fn de_lado(b: f64, a: f64) -> Trazado {
        trazar(con_impacto(b, a), a, &TRAZADO_DE_PRUEBA)
    }

    #[test]
    fn horizonte_e_isco_conocidos() {
        assert!(cerca(horizonte(0.0), 2.0) && cerca(horizonte(1.0), 1.0));
        assert!(cerca(isco(0.0), 6.0) && (isco(1.0) - 1.0).abs() < 1e-6);
        assert!(isco(0.9) > horizonte(0.9) && isco(0.9) < 6.0 && (isco(0.9) - 2.3209).abs() < 1e-3);
        assert!(cerca(radio_ks(vec3(3.0, 4.0, 0.0), 0.0), 5.0));
        assert!(cerca(radio_ks(vec3(3.0, 4.0, 0.0), 0.6), (25.0_f64 - 0.36).sqrt()));
    }

    /// En puntos y momentos variados: fuera del plano, cerca del horizonte,
    /// lejos, con y sin giro.
    #[test]
    fn el_gradiente_exacto_es_el_numerico() {
        let puntos = [
            vec3(3.0, 4.0, 1.0),
            vec3(-2.0, 0.5, -1.5),
            vec3(30.0, -5.0, 8.0),
            vec3(1.6, 0.3, 0.4),
            vec3(0.5, 2.0, 0.0),
        ];
        let momentos = [vec3(0.3, -0.8, 0.5), vec3(-1.0, 0.2, 0.1), vec3(0.6, 0.6, -0.5)];
        let mut peor = 0.0_f64;
        for a in [0.0, 0.6, 0.9, 0.99] {
            for &x in &puntos {
                if radio_ks(x, a) < horizonte(a) + 0.05 {
                    continue;
                }
                for &p in &momentos {
                    let g = gradiente_efe(x, p, a);
                    let e = 1e-6;
                    let n = [
                        (efe(x.mas(vec3(1.0, 0.0, 0.0), e), p, a) - efe(x.mas(vec3(1.0, 0.0, 0.0), -e), p, a))
                            / (2.0 * e),
                        (efe(x.mas(vec3(0.0, 1.0, 0.0), e), p, a) - efe(x.mas(vec3(0.0, 1.0, 0.0), -e), p, a))
                            / (2.0 * e),
                        (efe(x.mas(vec3(0.0, 0.0, 1.0), e), p, a) - efe(x.mas(vec3(0.0, 0.0, 1.0), -e), p, a))
                            / (2.0 * e),
                    ];
                    for (gi, ni) in [g.x, g.y, g.z].into_iter().zip(n) {
                        peor = peor.max((gi - ni).abs() / (ni.abs() + 1e-6));
                    }
                }
            }
        }
        assert!(peor < 1e-5, "diferencia relativa {peor}");
    }

    #[test]
    fn la_luz_es_luz() {
        let s = rayo_desde(vec3(-30.0, 2.0, 1.0), vec3(1.0, -0.1, 0.05), 0.9);
        assert!(hamiltoniano(s, 0.9).abs() < 1e-12, "el rayo de la cámara cumple H = 0");
        // y lo sigue cumpliendo al integrarlo, cerca del agujero
        let mut t = rayo_desde(vec3(-30.0, 4.0, 0.5), vec3(1.0, 0.0, 0.0), 0.9);
        let mut peor = 0.0_f64;
        for _ in 0..3000 {
            let h = paso_en(radio_ks(t.x, 0.9), 0.9, 0.02);
            paso_rk4(&mut t, 0.9, h);
            peor = peor.max(hamiltoniano(t, 0.9).abs());
            if radio_ks(t.x, 0.9) > 60.0 {
                break;
            }
        }
        assert!(peor < 1e-6, "H llegó a {peor}");
    }

    /// Sin giro, la luz con parámetro de impacto menor que 3√3 ≈ 5,196 cae.
    #[test]
    fn schwarzschild_captura_con_3_raiz_de_3() {
        assert!(de_lado(5.0, 0.0).cae && de_lado(-5.0, 0.0).cae);
        assert!(!de_lado(5.4, 0.0).cae && !de_lado(-5.4, 0.0).cae);
        // y se desvía: sale hacia el lado del agujero
        let t = de_lado(8.0, 0.0);
        assert!(!t.cae && t.cielo.y < -0.1);
    }

    /// Con a = 0,9 la luz que va a favor del giro roza hasta b ≈ 2,84 y la que
    /// va en contra cae ya con b ≈ 6,83. El fotón de b > 0 viaja hacia −x por
    /// y = b: su L_z es +b, a favor del giro.
    #[test]
    fn kerr_arrastra_la_luz() {
        assert!(!de_lado(5.0, 0.9).cae, "a favor del giro, b = 5 escapa");
        assert!(de_lado(-5.0, 0.9).cae, "en contra del giro, b = −5 cae");
        assert!(de_lado(2.6, 0.9).cae && !de_lado(3.1, 0.9).cae, "a favor, el umbral");
        assert!(
            de_lado(-6.6, 0.9).cae && !de_lado(-7.1, 0.9).cae,
            "en contra, el umbral"
        );
    }

    /// La cámara un poco por encima del plano mira al agujero: el rayo que pasa
    /// por encima del agujero cruza el disco por detrás (y se dobla).
    #[test]
    fn el_disco_se_ve_por_encima_y_por_debajo() {
        let d = DescTrazado {
            r_in: isco(0.9),
            r_out: 20.0,
            lejos: 60.0,
            max_pasos: 20000,
            precision: 0.02,
            rozar: 0.0,
        };
        let camara = vec3(-30.0, 0.0, 3.0);
        let abajo = rayo_desde(camara, vec3(1.0, 0.0, -0.13), 0.9);
        let arriba = rayo_desde(camara, vec3(1.0, 0.0, 0.04), 0.9);
        let delante = trazar(abajo, 0.9, &d);
        assert!(delante.cruces >= 1 && delante.cruce[0].r > isco(0.9) && delante.cruce[0].r < 20.0);
        assert!(
            trazar(arriba, 0.9, &d).cruces >= 1,
            "la luz del disco de detrás llega doblada"
        );
        // sin el anillo pegado a la sombra (rozar): el disco de delante y el de
        // detrás doblado siguen ahí (esos rayos no bajan de la ISCO)
        let sin_anillo = DescTrazado {
            rozar: 0.9 * isco(0.9),
            ..d
        };
        assert!(trazar(abajo, 0.9, &sin_anillo).cruces >= 1 && trazar(arriba, 0.9, &sin_anillo).cruces >= 1);
        // y un rayo que roza el horizonte (justo fuera de la sombra) ya no
        // recoge disco
        let (mut con, mut sin) = (0, 0);
        for k in 0..100 {
            let s = rayo_desde(vec3(-30.0, 2.0 + f64::from(k) * 0.02, 1.5), vec3(1.0, 0.0, -0.04), 0.9);
            con += usize::from(trazar(s, 0.9, &d).cruces > 0);
            sin += usize::from(trazar(s, 0.9, &sin_anillo).cruces > 0);
        }
        assert!(sin < con, "rozando el agujero, algunos rayos pierden el disco");
    }
}
