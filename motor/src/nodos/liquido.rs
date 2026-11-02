//! Lo que el líquido calcula en la CPU antes de dibujar: el cbuffer con los nodos y los enlaces, y
//! la caja que cubre.
//!
//! Rejilla de culling: la caja se parte en celdas y cada píxel recorre solo los elementos (nodos y
//! enlaces) cuya influencia toca su celda, en el mismo orden que la lista completa.
//!
//! Un elemento más lejos que su radio más la fusión (k) no pesa en la smin... salvo a través de
//! cadenas de elementos que se funden entre sí: con un alcance de 1 k, el 0,35 % de los puntos
//! cambiaba (hasta 38/255 en el borde). Con [`ALCANCE_FUSION`] = 4 k, lo que se pinta es igual que
//! sin rejilla en las 12 configuraciones de las pruebas (0/255; con 3 k, 1/255 en una), y en la GPU
//! real la textura sale idéntica (0 píxeles distintos). Medido con 256 nodos en una textura de
//! 2560x1080: el fotograma de GPU pasa de 9,8 ms a 0,8-1,2 ms de mediana.

use super::color_rgba;
use super::puente::{CamaraGpu, Escena, LiquidoGpu, Nodo};

/// Lo que pase del tope no se dibuja en el líquido (cada píxel recorre todo: es para un menú).
pub const MAX_NODOS: usize = 256;

pub const MAX_ARISTAS: usize = 512;

/// Forma del líquido, en múltiplos del radio que se ve del nodo.
pub struct FormaLiquido {
    /// Radio de la gota del nodo.
    pub gota: f32,
    /// Radio del cuello en lo más fino.
    pub cuello: f32,
    /// k de la smin: cuánto se funden.
    pub fusion: f32,
    pub opacidad: f32,
    /// El nodo resalta sobre su gota.
    pub oscurecer: f32,
    /// La del vórtice, que ocupa su nodo entero: solo asoma alrededor.
    pub gota_vortice: f32,
}

pub const FORMA: FormaLiquido = FormaLiquido {
    gota: 1.6,
    cuello: 0.22,
    fusion: 1.6,
    opacidad: 0.55,
    oscurecer: 0.25,
    gota_vortice: 1.15,
};

pub const ALCANCE_FUSION: f32 = 4.0;

/// Celdas por lado.
pub const REJILLA_MAX_LADO: u32 = 32;

pub const REJILLA_MAX_CELDAS: usize = (REJILLA_MAX_LADO * REJILLA_MAX_LADO) as usize;

/// Entradas de todas las listas (16 bits): lo que queda del cbuffer hasta 64 KiB.
pub const REJILLA_MAX_INDICES: u32 = 24544;

fn menor(a: f32, b: f32) -> f32 {
    if b < a { b } else { a }
}

fn mayor(a: f32, b: f32) -> f32 {
    if a < b { b } else { a }
}

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
fn repartir(l: &mut LiquidoGpu, nn: u32, na: u32, caja: &[f32; 4], aa: f32, con_rejilla: bool) {
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

/// Rellena `liquido` y deja en `caja` la del líquido en mundo. Ver el puente.
pub fn rellenar_liquido(
    escena: &Escena,
    nodos: &[Nodo],
    aristas: &[u32],
    liquido: &mut LiquidoGpu,
    caja: &mut [f32; 4],
    con_rejilla: bool,
) -> bool {
    let nn = (escena.cuenta_nodos as usize).min(MAX_NODOS).min(nodos.len());
    if nn == 0 {
        return false;
    }
    let n = &nodos[..nn];
    // el radio que se ve: el del mundo por el factor (los topes de zoom)
    let factor = if escena.factor_radio > 0.0 {
        escena.factor_radio
    } else {
        1.0
    };
    // todo finito, o sin líquido este fotograma: un NaN o un infinito (o una escala 0) darían
    // índices de celda basura y escrituras fuera de los arrays
    let escala_buena = escena.escala.is_finite() && escena.escala > 0.0;
    if !escala_buena || !factor.is_finite() {
        return false;
    }
    if n.iter()
        .any(|p| !p.x.is_finite() || !p.y.is_finite() || !p.radio.is_finite())
    {
        return false;
    }
    *caja = [3.4e38, 3.4e38, -3.4e38, -3.4e38];
    // el vórtice ocupa su nodo entero (el remolino mide su radio): su gota es más pequeña, para que
    // solo asome alrededor
    let encoge_vortice = FORMA.gota_vortice / FORMA.gota;
    for (i, p) in n.iter().enumerate() {
        let es_vortice = i + 1 == escena.vortice as usize;
        let radio = p.radio * factor * if es_vortice { encoge_vortice } else { 1.0 };
        liquido.nodos[i] = [p.x, p.y, radio, 0.0];
        liquido.colores[i] = color_rgba(p.rgba);
        let margen = radio * (FORMA.gota + FORMA.fusion) + 2.0 / escena.escala;
        caja[0] = menor(caja[0], p.x - margen);
        caja[1] = menor(caja[1], p.y - margen);
        caja[2] = mayor(caja[2], p.x + margen);
        caja[3] = mayor(caja[3], p.y + margen);
    }
    let mut na = 0;
    for par in aristas.chunks_exact(2) {
        if na >= MAX_ARISTAS {
            break;
        }
        let (a, b) = (par[0], par[1]);
        if a as usize >= nn || b as usize >= nn {
            continue;
        }
        liquido.aristas[na / 2][(na % 2) * 2] = a;
        liquido.aristas[na / 2][(na % 2) * 2 + 1] = b;
        na += 1;
    }
    liquido.forma = [FORMA.gota, FORMA.cuello, FORMA.fusion, FORMA.opacidad];
    liquido.cuenta[0] = FORMA.oscurecer;
    liquido.cuenta[1] = nn as f32;
    liquido.cuenta[2] = na as f32;
    // valores finitos pero extremos pueden desbordar la caja a infinito
    if !(caja[2] - caja[0]).is_finite() || !(caja[3] - caja[1]).is_finite() {
        return false;
    }
    repartir(liquido, nn as u32, na as u32, caja, 2.0 / escena.escala, con_rejilla);
    true
}

/// Ver el puente.
pub fn rellenar_liquido_en_camara(
    escena: &Escena,
    nodos: &[Nodo],
    aristas: &[u32],
    liquido: &mut LiquidoGpu,
    camara: &mut CamaraGpu,
    con_rejilla: bool,
) -> bool {
    rellenar_liquido(escena, nodos, aristas, liquido, &mut camara.caja, con_rejilla)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cerca(a: f32, b: f32) -> bool {
        (a - b).abs() < 1e-4
    }

    fn nodo(x: f32, y: f32, radio: f32, rgba: u32) -> Nodo {
        Nodo { x, y, radio, rgba }
    }

    fn escena(nodos: u32) -> Escena {
        Escena {
            centro_x: 10.0,
            centro_y: -5.0,
            escala: 2.0,
            cuenta_nodos: nodos,
            resaltado: 3,
            factor_radio: 1.0,
            ..Escena::default()
        }
    }

    #[test]
    fn cubre_los_nodos_y_salta_los_enlaces_rotos() {
        let nodos = [nodo(0.0, 0.0, 10.0, 0xff00_00ff), nodo(100.0, 0.0, 10.0, 0x00ff_00ff)];
        // el segundo par apunta a un nodo que no existe
        let pares = [0, 1, 0, 7];
        let mut l = LiquidoGpu::vacio();
        let mut caja = [0.0; 4];
        assert!(rellenar_liquido(&escena(2), &nodos, &pares, &mut l, &mut caja, true));
        assert!(cerca(l.cuenta[1], 2.0) && cerca(l.cuenta[2], 1.0), "el roto, fuera");
        assert!(l.aristas[0][0] == 0 && l.aristas[0][1] == 1, "el enlace bueno");
        assert!(cerca(l.colores[0][0], 1.0) && cerca(l.colores[0][1], 0.0) && cerca(l.colores[1][1], 1.0));
        // margen = radio que se ve (10, con factor 1) × (gota + fusión) + 2 / escala
        let margen = 10.0 * (FORMA.gota + FORMA.fusion) + 1.0;
        assert!(cerca(caja[0], -margen) && cerca(caja[2], 100.0 + margen));
        assert!(cerca(caja[1], -margen) && cerca(caja[3], margen));
    }

    #[test]
    fn sin_nodos_no_hay_liquido_y_el_tope_se_respeta() {
        let mut l = LiquidoGpu::vacio();
        let mut caja = [0.0; 4];
        assert!(!rellenar_liquido(&escena(0), &[], &[], &mut l, &mut caja, true));
        let muchos = vec![nodo(1.0, 2.0, 3.0, 0xffff_ffff); 300];
        assert!(rellenar_liquido(&escena(300), &muchos, &[], &mut l, &mut caja, true));
        assert!(cerca(l.cuenta[1], MAX_NODOS as f32));
        // la cabecera dice 5 nodos pero solo llegaron 2: no se lee de más
        let pocos = vec![nodo(1.0, 2.0, 3.0, 0xffff_ffff); 2];
        assert!(rellenar_liquido(&escena(5), &pocos, &[], &mut l, &mut caja, true));
        assert!(cerca(l.cuenta[1], 2.0));
    }

    /// Un NaN, un infinito, una escala 0 o una caja que se desborda: sin líquido.
    #[test]
    fn lo_que_no_es_finito_no_llega_a_la_rejilla() {
        let mut l = LiquidoGpu::vacio();
        let mut caja = [0.0; 4];
        let mut con = |primero: Nodo, escala: f32| {
            let e = Escena { escala, ..escena(2) };
            let nodos = [primero, nodo(100.0, 0.0, 10.0, 0xffff_ffff)];
            rellenar_liquido(&e, &nodos, &[], &mut l, &mut caja, true)
        };
        let blanco = 0xffff_ffff;
        assert!(!con(nodo(f32::NAN, 0.0, 10.0, blanco), 2.0), "x NaN");
        assert!(!con(nodo(0.0, f32::INFINITY, 10.0, blanco), 2.0), "y infinita");
        assert!(!con(nodo(0.0, 0.0, f32::NAN, blanco), 2.0), "radio NaN");
        assert!(!con(nodo(0.0, 0.0, 10.0, blanco), 0.0), "escala 0");
        assert!(!con(nodo(0.0, 0.0, 10.0, blanco), f32::NAN), "escala NaN");
        // finitos, pero la caja (con el margen de la gota) pasa de 3,4·10³⁸
        assert!(!con(nodo(-3e38, 0.0, 1e38, blanco), 2.0), "caja desbordada");
        assert!(con(nodo(0.0, 0.0, 10.0, blanco), 2.0), "finito, sí");
    }

    /// El factor de la cámara (los topes de zoom) agranda o encoge las gotas.
    #[test]
    fn el_factor_de_radio_escala_las_gotas() {
        let nodos = [nodo(0.0, 0.0, 10.0, 0xff00_00ff), nodo(100.0, 0.0, 40.0, 0x00ff_00ff)];
        let mut e = Escena {
            factor_radio: 0.5,
            ..escena(2)
        };
        let mut l = LiquidoGpu::vacio();
        let mut caja = [0.0; 4];
        rellenar_liquido(&e, &nodos, &[], &mut l, &mut caja, true);
        assert!(cerca(l.nodos[0][2], 5.0) && cerca(l.nodos[1][2], 20.0));
        // el nodo grande marca el borde derecho: 20 × (gota + fusión) + 2 / escala
        assert!(cerca(caja[2], 100.0 + 20.0 * (FORMA.gota + FORMA.fusion) + 1.0));
        // el vórtice (el nodo 1) ocupa su nodo: su gota encoge
        e.vortice = 2;
        rellenar_liquido(&e, &nodos, &[], &mut l, &mut caja, true);
        assert!(cerca(l.nodos[1][2] * FORMA.gota, 20.0 * FORMA.gota_vortice) && cerca(l.nodos[0][2], 5.0));
    }

    // ---- la cuenta del shader (pasada_liquido), copiada aquí ----

    #[derive(Clone, Copy)]
    struct Muestra {
        d: f32,
        col: [f32; 3],
    }

    fn smin_ref(a: f32, b: f32, k: f32) -> (f32, f32) {
        let h = (0.5 + 0.5 * (b - a) / k).clamp(0.0, 1.0);
        let m = (k - (a - b).abs()).max(0.0) / k;
        (a.min(b) - m * m * k * 0.25, h)
    }

    /// Lo que hay en la celda de (px, py).
    fn celda_de(l: &LiquidoGpu, px: f32, py: f32) -> usize {
        let cols = l.rejilla[3] as i32;
        let filas = l.cuenta[3] as i32;
        let cx = (((px - l.rejilla[0]) / l.rejilla[2]).floor() as i32).clamp(0, cols - 1);
        let cy = (((py - l.rejilla[1]) / l.rejilla[2]).floor() as i32).clamp(0, filas - 1);
        (cy * cols + cx) as usize
    }

    /// El campo en (px, py): con la lista de la celda (como el shader) o con la lista completa.
    fn evaluar(l: &LiquidoGpu, px: f32, py: f32, con_rejilla: bool) -> Muestra {
        let mut m = Muestra { d: 1e9, col: [0.0; 3] };
        let nn = l.cuenta[1] as u32;
        let na = l.cuenta[2] as u32;
        let plegar = |m: &mut Muestra, e: u32| {
            if e < nn {
                let n = l.nodos[e as usize];
                let r = n[2];
                let (d, h) = smin_ref((px - n[0]).hypot(py - n[1]) - l.forma[0] * r, m.d, l.forma[2] * r);
                m.d = d;
                for j in 0..3 {
                    m.col[j] += h * (l.colores[e as usize][j] - m.col[j]);
                }
                return;
            }
            let q = (e - nn) as usize;
            let (a, b) = (
                l.aristas[q / 2][(q % 2) * 2] as usize,
                l.aristas[q / 2][(q % 2) * 2 + 1] as usize,
            );
            let (na, nb) = (l.nodos[a], l.nodos[b]);
            let (abx, aby) = (nb[0] - na[0], nb[1] - na[1]);
            let largo2 = (abx * abx + aby * aby).max(1e-6);
            let t = (((px - na[0]) * abx + (py - na[1]) * aby) / largo2).clamp(0.0, 1.0);
            let r = na[2].min(nb[2]);
            let capsula = (px - na[0] - abx * t).hypot(py - na[1] - aby * t) - l.forma[1] * r;
            let (d, h) = smin_ref(capsula, m.d, l.forma[2] * r);
            m.d = d;
            for j in 0..3 {
                let c = l.colores[a][j] + t * (l.colores[b][j] - l.colores[a][j]);
                m.col[j] += h * (c - m.col[j]);
            }
        };
        if !con_rejilla {
            for e in 0..nn + na {
                plegar(&mut m, e);
            }
            return m;
        }
        let celda = celda_de(l, px, py);
        let w = l.celdas[celda / 4][celda % 4];
        for k in (w & 0xFFFF)..(w & 0xFFFF) + (w >> 16) {
            let k = k as usize;
            let doble = l.indices[k / 8][(k / 2) % 4];
            plegar(&mut m, if k % 2 == 1 { doble >> 16 } else { doble & 0xFFFF });
        }
        m
    }

    struct Config {
        semilla: u32,
        ancho: f32,
        alto: f32,
        escala: f32,
    }

    /// 256 nodos (el tope) al azar, en estrella con anillo.
    fn grafo(cfg: &Config) -> (Vec<Nodo>, Vec<u32>) {
        let mut semilla = cfg.semilla;
        let mut azar = || {
            semilla = semilla.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            (semilla >> 8) as f32 / 16_777_216.0
        };
        let mut nodos = Vec::new();
        for i in 0..256_u32 {
            let (x, y) = (azar() * cfg.ancho, azar() * cfg.alto);
            nodos.push(nodo(
                x,
                y,
                22.0,
                0x89b4_faff_u32.wrapping_add(i.wrapping_mul(0x0100_0000)),
            ));
        }
        let mut pares = Vec::new();
        for i in 1..256 {
            pares.extend([0, i]);
        }
        for i in 1..255 {
            pares.extend([i, i + 1]);
        }
        (nodos, pares)
    }

    /// Peor diferencia (en 1/255) entre el RGBA8 con y sin rejilla, elementos por píxel y píxeles
    /// con líquido.
    fn comparar(cfg: &Config) -> (i32, f64, i32) {
        let (nodos, pares) = grafo(cfg);
        let e = Escena {
            escala: cfg.escala,
            ..escena(256)
        };
        let mut l = LiquidoGpu::vacio();
        let mut caja = [0.0; 4];
        assert!(rellenar_liquido(&e, &nodos, &pares, &mut l, &mut caja, true));
        let aa = 2.0 / e.escala;
        // lo que sale de verdad del shader, en RGBA8: cobertura con un píxel de suavizado y color
        // oscurecido, premultiplicados como en la textura
        let rgba8 = |m: &Muestra, canal: usize| {
            let alfa = l.forma[3] * (0.5 - m.d * e.escala).clamp(0.0, 1.0);
            let v = if canal == 3 {
                alfa
            } else {
                m.col[canal] * (1.0 - l.cuenta[0]) * alfa
            };
            (v.clamp(0.0, 1.0) * 255.0).round() as i32
        };
        let (mut peor, mut visibles, mut recorridos) = (0, 0, 0.0);
        for j in 0..160 {
            for i in 0..160 {
                let px = caja[0] + (caja[2] - caja[0]) * (i as f32 + 0.5) / 160.0;
                let py = caja[1] + (caja[3] - caja[1]) * (j as f32 + 0.5) / 160.0;
                let completa = evaluar(&l, px, py, false);
                let celda = evaluar(&l, px, py, true);
                for c in 0..4 {
                    peor = peor.max((rgba8(&completa, c) - rgba8(&celda, c)).abs());
                }
                if completa.d <= aa {
                    visibles += 1;
                }
                let c = celda_de(&l, px, py);
                recorridos += f64::from(l.celdas[c / 4][c % 4] >> 16);
            }
        }
        (peor, recorridos / (160.0 * 160.0), visibles)
    }

    #[test]
    fn la_rejilla_no_cambia_lo_que_se_ve() {
        let mut peor_total = 0;
        let mut por_pixel_max = 0.0_f64;
        for semilla in [12345, 777, 31337] {
            for (ancho, alto) in [(3000.0, 1500.0), (1200.0, 600.0)] {
                for escala in [0.8, 2.0] {
                    let (peor, por_pixel, visibles) = comparar(&Config {
                        semilla,
                        ancho,
                        alto,
                        escala,
                    });
                    assert!(visibles > 500, "hay líquido que comparar");
                    peor_total = peor_total.max(peor);
                    por_pixel_max = por_pixel_max.max(por_pixel);
                }
            }
        }
        assert!(peor_total <= 1, "igual con y sin rejilla (±1/255): {peor_total}");
        assert!(
            por_pixel_max < 300.0,
            "bastante menos que la lista completa: {por_pixel_max}"
        );
    }

    /// Si las listas no caben, una sola celda con todos los elementos, en orden.
    #[test]
    fn si_no_cabe_vuelve_a_la_lista_completa() {
        // 256 nodos en dos esquinas opuestas, unidos en cruz: cada enlace cruza la caja entera
        let nodos: Vec<Nodo> = (0..256)
            .map(|i| {
                let esquina = if i % 2 == 1 { 5000.0 } else { 0.0 };
                nodo(esquina, esquina, 22.0, 0xffff_ffff)
            })
            .collect();
        // cada nodo con el siguiente y con el de tres más allá: 510 diagonales
        let mut pares = Vec::new();
        for i in 0..255 {
            pares.extend([i, i + 1, i, (i + 3) % 256]);
        }
        let mut l = LiquidoGpu::vacio();
        let mut caja = [0.0; 4];
        assert!(rellenar_liquido(&escena(256), &nodos, &pares, &mut l, &mut caja, true));
        assert!(l.rejilla[3] == 1.0 && l.cuenta[3] == 1.0, "una sola celda");
        let total = 256 + l.cuenta[2] as u32;
        assert_eq!(l.celdas[0][0], total << 16, "la celda lleva todos");
        for k in 0..total as usize {
            let doble = l.indices[k / 8][(k / 2) % 4];
            let e = if k % 2 == 1 { doble >> 16 } else { doble & 0xFFFF };
            assert_eq!(e as usize, k, "en el orden de siempre");
        }
    }
}
