//! La rejilla de culling del líquido: la caja se parte en celdas y cada píxel recorre solo los
//! elementos (nodos y enlaces) cuya influencia toca su celda, en el mismo orden que la lista completa.
//!
//! Un elemento más lejos que su radio más la fusión (k) no pesa en la smin... salvo a través de
//! cadenas de elementos que se funden entre sí: con un alcance de 1 k, el 0,35 % de los puntos
//! cambiaba (hasta 38/255 en el borde). Con [`ALCANCE_FUSION`] = 4 k, lo que se pinta es igual que
//! sin rejilla en las 12 configuraciones de las pruebas (0/255; con 3 k, 1/255 en una), y en la GPU
//! real la textura sale idéntica (0 píxeles distintos). Medido con 256 nodos en una textura de
//! 2560x1080: el fotograma de GPU pasa de 9,8 ms a 0,8-1,2 ms de mediana.
//!
//! Las celdas, las más pequeñas en las que caben las listas. Medido (banco del visor, 800 × 600, la
//! misma imagen byte a byte): con celdas del tamaño del nodo mayor, 0,191 ms con 9 nodos, 2,64 con
//! una malla de 12 × 9 y 4,83 con una de 16 × 16; con las más pequeñas, 0,096, 0,31 y 0,57. En la
//! CPU cuesta entre 26 y 63 µs más por fotograma (de 1 a 27 µs con 9 nodos, de 53 a 116 con 256).

use super::super::puente::LiquidoGpu;
use super::{ALCANCE_FUSION, FORMA, MAX_ARISTAS, MAX_NODOS, mayor, menor};

/// Celdas por lado.
pub const REJILLA_MAX_LADO: u32 = 32;

pub const REJILLA_MAX_CELDAS: usize = (REJILLA_MAX_LADO * REJILLA_MAX_LADO) as usize;

/// Entradas de todas las listas (16 bits): lo que queda del cbuffer hasta 64 KiB.
pub const REJILLA_MAX_INDICES: u32 = 24544;

/// De `lo` a `hi`; un NaN pasa. Lo usa la prueba del trozo de fila, que repite la prueba de antes.
#[cfg(test)]
fn acotar(v: f32, lo: f32, hi: f32) -> f32 {
    if v < lo {
        lo
    } else if hi < v {
        hi
    } else {
        v
    }
}

/// Lo que hace falta para saber qué celdas toca un elemento: nodos 0..nn-1, enlaces nn + e.
struct Reparto<'a> {
    nodos: &'a [[f32; 4]; MAX_NODOS],
    aristas: &'a [[u32; 4]; MAX_ARISTAS / 2],
    nn: u32,
    caja: &'a [f32; 4],
    aa: f32,
    influencia_nodo: f32,
    influencia_enlace: f32,
    lado: f32,
    cols: u32,
    filas: u32,
    /// Una celda toca la influencia de un enlace si su centro está a menos de ella más la influencia
    /// (conservador).
    media_diagonal: f32,
}

impl Reparto<'_> {
    /// El radio que se ve, ya con el factor de la cámara.
    fn radio(&self, i: usize) -> f32 {
        self.nodos[i][2]
    }

    fn extremos(&self, e: u32) -> (usize, usize) {
        let k = (e - self.nn) as usize;
        let par = self.aristas[k / 2];
        (par[(k % 2) * 2] as usize, par[(k % 2) * 2 + 1] as usize)
    }

    /// La caja de influencia de un elemento, en mundo.
    fn influencia(&self, e: u32) -> [f32; 4] {
        if e < self.nn {
            let e = e as usize;
            let m = self.radio(e) * self.influencia_nodo + self.aa;
            let n = self.nodos[e];
            return [n[0] - m, n[1] - m, n[0] + m, n[1] + m];
        }
        let (a, b) = self.extremos(e);
        let (na, nb) = (self.nodos[a], self.nodos[b]);
        let m = menor(self.radio(a), self.radio(b)) * self.influencia_enlace + self.aa;
        [
            menor(na[0], nb[0]) - m,
            menor(na[1], nb[1]) - m,
            mayor(na[0], nb[0]) + m,
            mayor(na[1], nb[1]) + m,
        ]
    }

    fn celda(&self, v: f32, origen: f32, n: u32) -> u32 {
        let q = ((v - origen) / self.lado).floor();
        // negativa o NaN, la primera: un NaN pasaría cualquier tope
        if q > 0.0 { menor(q, (n - 1) as f32) as u32 } else { 0 }
    }

    /// Llama a `f(celda)` por cada celda que toca la influencia del elemento `e`: los nodos, su
    /// caja; los enlaces, solo las celdas cerca del segmento (un enlace largo en diagonal no llena
    /// media rejilla).
    fn por_celda(&self, e: u32, mut f: impl FnMut(usize)) {
        let c = self.influencia(e);
        let x0 = self.celda(c[0], self.caja[0], self.cols);
        let y0 = self.celda(c[1], self.caja[1], self.filas);
        let x1 = self.celda(c[2], self.caja[0], self.cols);
        let y1 = self.celda(c[3], self.caja[1], self.filas);
        let (mut ax, mut ay, mut abx, mut aby, mut alcance) = (0.0, 0.0, 0.0, 0.0, 0.0);
        let enlace = e >= self.nn;
        if enlace {
            let (a, b) = self.extremos(e);
            ax = self.nodos[a][0];
            ay = self.nodos[a][1];
            abx = self.nodos[b][0] - ax;
            aby = self.nodos[b][1] - ay;
            alcance = menor(self.radio(a), self.radio(b)) * self.influencia_enlace + self.aa + self.media_diagonal;
        }
        for y in y0..=y1 {
            let (mut desde, mut hasta) = (x0, x1);
            if enlace {
                // las celdas de la fila con el centro a menos de `alcance` del segmento, sin probarlas
                // una a una: probar cada celda costaba casi todo el reparto con celdas pequeñas
                let py = self.caja[1] + (y as f32 + 0.5) * self.lado;
                match tramo_de_fila(ax, ay, abx, aby, alcance, py) {
                    None => continue,
                    Some((lo, hi)) if lo.is_finite() && hi.is_finite() => {
                        // un poco más ancho: de sobra no cambia nada, y de menos se perdería una celda
                        let holgura = self.lado * 1e-3;
                        let a = ((lo - holgura - self.caja[0]) / self.lado - 0.5).ceil();
                        let b = ((hi + holgura - self.caja[0]) / self.lado - 0.5).floor();
                        if b < x0 as f32 || a > x1 as f32 || b < a {
                            continue;
                        }
                        desde = mayor(a, x0 as f32) as u32;
                        hasta = menor(b, x1 as f32) as u32;
                    }
                    // un NaN: la fila entera, como cualquier otra cosa que no se sabe dónde cae
                    Some(_) => {}
                }
            }
            for x in desde..=hasta {
                f((y * self.cols + x) as usize);
            }
        }
    }
}

/// El trozo de la fila `py` a menos de `r` del segmento de `a` a `a + ab`. La cápsula es convexa: es
/// un solo intervalo, el de los círculos de los extremos y la franja de en medio juntos.
fn tramo_de_fila(ax: f32, ay: f32, abx: f32, aby: f32, r: f32, py: f32) -> Option<(f32, f32)> {
    // con un NaN no se sabe dónde cae: que decida quien llama
    if ![ax, ay, abx, aby, r, py].iter().all(|v| v.is_finite()) {
        return Some((f32::NAN, f32::NAN));
    }
    let (mut lo, mut hi) = (f32::INFINITY, f32::NEG_INFINITY);
    for (cx, cy) in [(ax, ay), (ax + abx, ay + aby)] {
        let dy = py - cy;
        let h2 = r * r - dy * dy;
        if h2 >= 0.0 {
            let h = h2.sqrt();
            lo = menor(lo, cx - h);
            hi = mayor(hi, cx + h);
        }
    }
    let largo = (abx * abx + aby * aby).sqrt();
    if largo > 1e-6 {
        let (ux, uy, dy) = (abx / largo, aby / largo, py - ay);
        // a menos de r de la recta: |(x - ax)·uy - dy·ux| <= r
        let (mut a, mut b) = (f32::NEG_INFINITY, f32::INFINITY);
        if uy.abs() > 1e-9 {
            let (p, q) = ((dy * ux - r) / uy, (dy * ux + r) / uy);
            (a, b) = (mayor(a, menor(p, q)), menor(b, mayor(p, q)));
        } else if (dy * ux).abs() > r {
            return (lo <= hi).then_some((lo, hi));
        }
        // y con la proyección entre los extremos: 0 <= (x - ax)·ux + dy·uy <= largo
        if ux.abs() > 1e-9 {
            let (p, q) = (-dy * uy / ux, (largo - dy * uy) / ux);
            (a, b) = (mayor(a, menor(p, q)), menor(b, mayor(p, q)));
        } else if dy * uy < 0.0 || dy * uy > largo {
            return (lo <= hi).then_some((lo, hi));
        }
        if a <= b {
            lo = menor(lo, ax + a);
            hi = mayor(hi, ax + b);
        }
    }
    (lo <= hi).then_some((lo, hi))
}

/// Reparte los elementos en la rejilla. Cada uno va a las celdas que toca su influencia: su radio en
/// la smin más la fusión y el suavizado. Una pasada por la geometría, que apunta cada par celda y
/// elemento en un búfer de este hilo que se reutiliza: las listas salen en el orden de los
/// elementos, el mismo de la lista completa.
pub(super) fn repartir(l: &mut LiquidoGpu, nn: u32, na: u32, caja: &[f32; 4], aa: f32, con_rejilla: bool) {
    let influencia_nodo = FORMA.gota + FORMA.fusion * ALCANCE_FUSION;
    let influencia_enlace = FORMA.cuello + FORMA.fusion * ALCANCE_FUSION;
    let ancho = caja[2] - caja[0];
    let alto = caja[3] - caja[1];
    let reparto_de = |lado: f32| {
        let en_celdas = |medida: f32| ((medida / lado).ceil() as u32).clamp(1, REJILLA_MAX_LADO);
        Reparto {
            nodos: &l.nodos,
            aristas: &l.aristas,
            nn,
            caja,
            aa,
            influencia_nodo,
            influencia_enlace,
            lado,
            cols: en_celdas(ancho),
            filas: en_celdas(alto),
            // redondeado por arriba, para no quedarse corto
            #[allow(clippy::approx_constant)]
            media_diagonal: lado * 0.70711,
        }
    };
    // Las celdas más pequeñas en las que caben las listas: cada píxel recorre solo los elementos
    // de la suya, y cuanto más pequeña, menos. Medido (banco del visor, 9 nodos): con 32 × 32 el
    // líquido tarda la mitad que con celdas del tamaño del nodo mayor, 0,097 frente a 0,191 ms.
    // Si no caben, el doble de grandes, hasta una sola celda
    let lados = REJILLA_MAX_LADO as f32;
    let mut lado = mayor(mayor(ancho / lados, alto / lados), 1e-6);
    let elementos = nn + na;
    let mut cuantos = [0_u32; REJILLA_MAX_CELDAS];
    let (rejilla, cabe) = PARES.with_borrow_mut(|pares| {
        let (reparto, total) = loop {
            let reparto = reparto_de(lado);
            cuantos.fill(0);
            pares.clear();
            let mut total = 0_u32;
            for e in 0..elementos {
                reparto.por_celda(e, |c| {
                    cuantos[c] += 1;
                    total += 1;
                    pares.push(((c as u32) << 16) | e);
                });
                // ya no cabe: no hace falta seguir contando
                if total > REJILLA_MAX_INDICES && con_rejilla && reparto.cols * reparto.filas > 1 {
                    break;
                }
            }
            if total <= REJILLA_MAX_INDICES || !con_rejilla || reparto.cols * reparto.filas == 1 {
                break (reparto, total);
            }
            lado *= 2.0;
        };
        let cabe = con_rejilla && total <= REJILLA_MAX_INDICES;
        ((reparto.cols, reparto.filas, reparto.lado), cabe)
    });
    PARES.with_borrow(|pares| rellenar_listas(l, &mut cuantos, pares, elementos, caja, cabe, rejilla));
}

thread_local! {
    /// Los pares celda y elemento del reparto (celda << 16 | elemento): se reutiliza en cada fotograma.
    static PARES: std::cell::RefCell<Vec<u32>> = const { std::cell::RefCell::new(Vec::new()) };
}

/// Las listas de cada celda en el cbuffer, de los pares ya apuntados. Si no caben, una sola celda con
/// todo.
fn rellenar_listas(
    l: &mut LiquidoGpu,
    cuantos: &mut [u32; REJILLA_MAX_CELDAS],
    pares: &[u32],
    elementos: u32,
    caja: &[f32; 4],
    cabe: bool,
    (cols, filas, lado): (u32, u32, f32),
) {
    let (ancho, alto) = (caja[2] - caja[0], caja[3] - caja[1]);
    // si las listas no caben, una sola celda con todo: la lista completa de siempre (más lento,
    // igual de correcto)
    let (cols, filas) = if cabe { (cols, filas) } else { (1, 1) };
    if !cabe {
        cuantos.fill(0);
        cuantos[0] = elementos;
    }
    let mut cursor = [0_u32; REJILLA_MAX_CELDAS];
    let mut inicio = 0_u32;
    for c in 0..(cols * filas) as usize {
        cursor[c] = inicio;
        l.celdas[c / 4][c % 4] = inicio | (cuantos[c] << 16);
        inicio += cuantos[c];
    }
    let indices = &mut l.indices;
    let mut apuntar = |c: usize, e: u32| {
        let k = cursor[c] as usize;
        cursor[c] += 1;
        let par = &mut indices[k / 8][(k / 2) % 4];
        *par = if k % 2 == 1 {
            (*par & 0xFFFF) | (e << 16)
        } else {
            (*par & 0xFFFF_0000) | e
        };
    };
    if cabe {
        for &p in pares {
            apuntar((p >> 16) as usize, p & 0xFFFF);
        }
    } else {
        for e in 0..elementos {
            apuntar(0, e);
        }
    }
    l.rejilla = [
        caja[0],
        caja[1],
        if cabe {
            lado
        } else {
            mayor(mayor(ancho, alto), 1e-6) * 2.0
        },
        cols as f32,
    ];
    l.cuenta[3] = filas as f32;
}

#[cfg(test)]
mod tests {
    use super::*;

    /// El trozo de cada fila coge todas las celdas que cogía probarlas una a una (centro a menos del
    /// alcance del segmento) y, de más, solo alguna justo en el borde.
    #[test]
    fn el_trozo_de_fila_es_el_de_probar_celda_a_celda() {
        let mut azar = 12345_u32;
        let mut siguiente = || {
            azar ^= azar << 13;
            azar ^= azar >> 17;
            azar ^= azar << 5;
            azar as f32 / u32::MAX as f32
        };
        let lado = 7.0;
        for _ in 0..3000 {
            let (ax, ay) = (siguiente() * 200.0, siguiente() * 200.0);
            // también horizontales, verticales y de un punto
            let (mut abx, mut aby) = (siguiente() * 160.0 - 80.0, siguiente() * 160.0 - 80.0);
            match (siguiente() * 8.0) as u32 {
                0 => aby = 0.0,
                1 => abx = 0.0,
                2 => (abx, aby) = (0.0, 0.0),
                _ => {}
            }
            let r = 1.0 + siguiente() * 30.0;
            for y in 0..40 {
                let py = (y as f32 + 0.5) * lado;
                let tramo = tramo_de_fila(ax, ay, abx, aby, r, py);
                for x in 0..40 {
                    let px = (x as f32 + 0.5) * lado;
                    let largo2 = mayor(abx * abx + aby * aby, 1e-6);
                    let t = acotar(((px - ax) * abx + (py - ay) * aby) / largo2, 0.0, 1.0);
                    let d = (px - ax - abx * t).hypot(py - ay - aby * t);
                    let dentro = tramo.is_some_and(|(lo, hi)| px >= lo - 1e-3 && px <= hi + 1e-3);
                    if d <= r {
                        assert!(dentro, "falta la celda {x}, {y}: a {d} de {r}");
                    } else if dentro {
                        assert!(d <= r + 0.01, "sobra la celda {x}, {y}: a {d} de {r}");
                    }
                }
            }
        }
    }
}
