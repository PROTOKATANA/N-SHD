//! El fluido, lo que se calcula en la CPU: la geometría de la rejilla, el diagnóstico y la huella.
//! El solver va en la GPU (`nucleo/nodos/fluido_stam`).
//!
//! La rejilla son N × N celdas con un círculo de radio N / 2 - 1 dentro (una celda de pared
//! alrededor). Cada celda se clasifica por q = (2x - (N - 1))² + (2y - (N - 1))², un entero: la CPU
//! y el shader comparan el mismo número con los mismos umbrales, sin diferencias de redondeo.

use super::puente::{DiagnosticoFluido, FormaFluido};
use super::vortice::FORMA;

/// La base de la que parte [`huella`].
pub const HUELLA_INICIAL: u64 = 14_695_981_039_346_656_037;

const PRIMO_FNV: u64 = 1_099_511_628_211;

/// Ver el puente.
pub fn forma_fluido_vortice() -> FormaFluido {
    FORMA.fluido
}

pub fn q_celda(x: i32, y: i32, celdas: u32) -> f32 {
    let lado = celdas as i32;
    let dx = 2 * x - (lado - 1);
    let dy = 2 * y - (lado - 1);
    (dx * dx + dy * dy) as f32
}

/// q en el borde de un círculo de `fraccion` del radio (1: el círculo entero).
pub fn q_radio(fraccion: f32, celdas: u32) -> f32 {
    // radio del círculo en celdas, con una celda de pared alrededor; en q (coordenadas dobladas)
    // los umbrales van al cuadrado y por 4
    let radio = (celdas as f32 - 2.0) * 0.5;
    (2.0 * fraccion * radio) * (2.0 * fraccion * radio)
}

/// Ver el puente.
pub fn q_circulo(celdas: u32) -> f32 {
    q_radio(1.0, celdas)
}

/// Cuántas celdas tienen `q_desde` < q ≤ `q_hasta`.
pub fn contar_celdas(celdas: u32, q_desde: f32, q_hasta: f32) -> u32 {
    let lado = celdas as i32;
    let mut n = 0;
    for y in 0..lado {
        for x in 0..lado {
            let q = q_celda(x, y, celdas);
            if q > q_desde && q <= q_hasta {
                n += 1;
            }
        }
    }
    n
}

/// `con_fuentes(q)`: las celdas donde el efecto mete o saca fluido (no cuentan en la divergencia).
/// La divergencia, con las mismas diferencias centradas que el shader.
pub fn diagnosticar(vel: &[f32], escalar: &[f32], celdas: u32, con_fuentes: impl Fn(f32) -> bool) -> DiagnosticoFluido {
    let q_fluido = q_radio(1.0, celdas);
    let lado = celdas as i32;
    let fluido = |x: i32, y: i32| x >= 0 && y >= 0 && x < lado && y < lado && q_celda(x, y, celdas) <= q_fluido;
    // la velocidad de una celda (la pared, quieta)
    let v = |x: i32, y: i32, eje: usize| {
        if fluido(x, y) {
            vel[(y * lado + x) as usize * 2 + eje]
        } else {
            0.0
        }
    };
    let mut d = DiagnosticoFluido {
        divergencia_max: 0.0,
        divergencia_media: 0.0,
        velocidad_max: 0.0,
        escalar_min: 1e30,
        escalar_max: -1e30,
        nan: 0,
    };
    let mut suma = 0.0_f64;
    let mut dentro = 0_u32;
    for y in 0..lado {
        for x in 0..lado {
            if !fluido(x, y) {
                continue;
            }
            let i = (y * lado + x) as usize;
            let (vx, vy, t) = (vel[i * 2], vel[i * 2 + 1], escalar[i]);
            if vx.is_nan() || vy.is_nan() || t.is_nan() {
                d.nan += 1;
                continue;
            }
            d.velocidad_max = d.velocidad_max.max(vx.hypot(vy));
            d.escalar_min = d.escalar_min.min(t);
            d.escalar_max = d.escalar_max.max(t);
            let pared = !fluido(x + 1, y) || !fluido(x - 1, y) || !fluido(x, y + 1) || !fluido(x, y - 1);
            if con_fuentes(q_celda(x, y, celdas)) || pared {
                continue;
            }
            let div = 0.5 * ((v(x + 1, y, 0) - v(x - 1, y, 0)) + (v(x, y + 1, 1) - v(x, y - 1, 1)));
            d.divergencia_max = d.divergencia_max.max(div.abs());
            suma += f64::from(div.abs());
            dentro += 1;
        }
    }
    if dentro > 0 {
        d.divergencia_media = (suma / f64::from(dentro)) as f32;
    }
    d
}

/// La huella de un estado: con el reloj de pasos fijos, la misma en cada ejecución. Una
/// optimización que no cambia el resultado la deja igual, bit a bit.
pub fn huella(datos: &[u8], base: u64) -> u64 {
    datos
        .iter()
        .fold(base, |h, &b| (h ^ u64::from(b)).wrapping_mul(PRIMO_FNV))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::nodos::vortice::rejilla_de;

    fn bytes(v: &[f32]) -> Vec<u8> {
        v.iter().flat_map(|x| x.to_le_bytes()).collect()
    }

    #[test]
    fn la_huella_cambia_con_el_ultimo_bit() {
        let mut datos = vec![1.0_f32; 64];
        let h = huella(&bytes(&datos), HUELLA_INICIAL);
        assert_eq!(h, huella(&bytes(&datos), HUELLA_INICIAL));
        datos[17] = f32::from_bits(1.0_f32.to_bits() + 1);
        assert_ne!(h, huella(&bytes(&datos), HUELLA_INICIAL));
    }

    #[test]
    fn la_huella_es_la_fnv_de_siempre() {
        assert_eq!(huella(b"", HUELLA_INICIAL), HUELLA_INICIAL);
        assert_eq!(huella(b"a", HUELLA_INICIAL), 0xaf63_dc4c_8601_ec8c);
    }

    fn campo(n: u32, giro: f32, abre: f32) -> Vec<f32> {
        let medio = (n as f32 - 1.0) * 0.5;
        let mut vel = vec![0.0; (n * n * 2) as usize];
        for y in 0..n {
            for x in 0..n {
                let (dx, dy) = (x as f32 - medio, y as f32 - medio);
                let i = ((y * n + x) * 2) as usize;
                vel[i] = -giro * dy + abre * dx;
                vel[i + 1] = giro * dx + abre * dy;
            }
        }
        vel
    }

    /// Un giro rígido no tiene divergencia, uno que se abre sí, y un NaN se cuenta.
    #[test]
    fn el_diagnostico_ve_la_divergencia_y_los_nan() {
        let n = FORMA.fluido.celdas;
        let medio = (n as f32 - 1.0) * 0.5;
        let mut calor = vec![0.5_f32; (n * n) as usize];
        let r = rejilla_de(n);
        let fuentes = |q| r.con_fuentes(q);

        let d = diagnosticar(&campo(n, 0.5, 0.0), &calor, n, fuentes);
        assert!(d.divergencia_max < 1e-4 && d.nan == 0, "un giro rígido no diverge");
        assert!(d.velocidad_max > 0.5 * medio * 0.8 && (d.escalar_min - 0.5).abs() < 1e-4);

        let abierto = campo(n, 0.0, 0.25);
        let d = diagnosticar(&abierto, &calor, n, fuentes);
        assert!((d.divergencia_max - 0.5).abs() < 1e-3 && (d.divergencia_media - 0.5).abs() < 1e-3);

        calor[((n / 2) * n + n / 2) as usize] = f32::NAN;
        assert_eq!(diagnosticar(&abierto, &calor, n, fuentes).nan, 1);
    }
}
