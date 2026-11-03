//! La rejilla de culling del líquido: la caja se parte en celdas y cada píxel recorre solo los
//! elementos (nodos y enlaces) cuya influencia toca su celda, en el mismo orden que la lista completa.
//!
//! Un elemento más lejos que su radio más la fusión (k) no pesa en la smin... salvo a través de
//! cadenas de elementos que se funden entre sí: con un alcance de 1 k, el 0,35 % de los puntos
//! cambiaba (hasta 38/255 en el borde). Con [`ALCANCE_FUSION`] = 4 k, lo que se pinta es igual que
//! sin rejilla en las 12 configuraciones de las pruebas (0/255; con 3 k, 1/255 en una), y en la GPU
//! real la textura sale idéntica (0 píxeles distintos). Medido con 256 nodos en una textura de
//! 2560x1080: el fotograma de GPU pasa de 9,8 ms a 0,8-1,2 ms de mediana.

use super::super::puente::LiquidoGpu;
use super::{ALCANCE_FUSION, FORMA, MAX_ARISTAS, MAX_NODOS, mayor, menor};

/// Celdas por lado.
pub const REJILLA_MAX_LADO: u32 = 32;

pub const REJILLA_MAX_CELDAS: usize = (REJILLA_MAX_LADO * REJILLA_MAX_LADO) as usize;

/// Entradas de todas las listas (16 bits): lo que queda del cbuffer hasta 64 KiB.
pub const REJILLA_MAX_INDICES: u32 = 24544;

/// De `lo` a `hi`; un NaN pasa.
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
            for x in x0..=x1 {
                if enlace {
                    let px = self.caja[0] + (x as f32 + 0.5) * self.lado;
                    let py = self.caja[1] + (y as f32 + 0.5) * self.lado;
                    let largo2 = mayor(abx * abx + aby * aby, 1e-6);
                    let t = acotar(((px - ax) * abx + (py - ay) * aby) / largo2, 0.0, 1.0);
                    if (px - ax - abx * t).hypot(py - ay - aby * t) > alcance {
                        continue;
                    }
                }
                f((y * self.cols + x) as usize);
            }
        }
    }
}

/// Reparte los elementos en la rejilla. Cada uno va a las celdas que toca su influencia: su radio en
/// la smin más la fusión y el suavizado. Dos pasadas (contar y rellenar), sin reservar: las listas
/// salen en el orden de los elementos, el mismo de la lista completa.
pub(super) fn repartir(l: &mut LiquidoGpu, nn: u32, na: u32, caja: &[f32; 4], aa: f32, con_rejilla: bool) {
    let influencia_nodo = FORMA.gota + FORMA.fusion * ALCANCE_FUSION;
    let influencia_enlace = FORMA.cuello + FORMA.fusion * ALCANCE_FUSION;
    let ancho = caja[2] - caja[0];
    let alto = caja[3] - caja[1];
    // celdas del tamaño de la influencia de un nodo, sin pasar de 32 por lado
    let mut lado = 0.0;
    for i in 0..nn as usize {
        lado = mayor(lado, l.nodos[i][2] * influencia_nodo);
    }
    let lados = REJILLA_MAX_LADO as f32;
    lado = mayor(mayor(mayor(lado, ancho / lados), alto / lados), 1e-6);
    let en_celdas = |medida: f32| ((medida / lado).ceil() as u32).clamp(1, REJILLA_MAX_LADO);
    let reparto = Reparto {
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
    };
    let elementos = nn + na;
    let mut cuantos = [0_u32; REJILLA_MAX_CELDAS];
    let mut total = 0_u32;
    for e in 0..elementos {
        reparto.por_celda(e, |c| {
            cuantos[c] += 1;
            total += 1;
        });
    }
    // si las listas no caben, una sola celda con todo: la lista completa de siempre (más lento,
    // igual de correcto)
    let cabe = con_rejilla && total <= REJILLA_MAX_INDICES;
    let (cols, filas) = if cabe { (reparto.cols, reparto.filas) } else { (1, 1) };
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
    for e in 0..elementos {
        if cabe {
            reparto.por_celda(e, |c| apuntar(c, e));
        } else {
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
