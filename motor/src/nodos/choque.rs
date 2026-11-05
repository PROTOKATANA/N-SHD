//! Los nodos no se tocan: dos nodos normales nunca quedan más cerca que la suma de sus radios de
//! choque, su radio que se ve por [`SEPARACION`]. Corrige las posiciones directamente, como
//! `forceCollide` de d3 pero sin depender de ninguna velocidad: actúa también al arrastrar. El
//! vórtice no choca, porque es la gota: los nodos la atraviesan y la deforman.

use super::camara::vista;
use super::puente::{Escena, Nodo};

/// Radio de choque de un nodo, en múltiplos del radio que se ve: algo por dentro de su gota en el
/// líquido de los enlaces (1,6), para que dos nodos juntos se acerquen un poco y sus gotas apenas se
/// rocen.
pub const SEPARACION: f32 = 1.5;

/// [`SEPARACION`], para quien integra desde C++.
pub fn separacion_nodos() -> f32 {
    SEPARACION
}

/// Pasadas por llamada: apartar un par puede montar a uno sobre otro, o sacarlo de la vista, y la
/// pasada siguiente lo arregla.
pub const PASADAS: usize = 8;

/// Lo que se mueve un nodo al volver a la vista sin contar como devuelto por el borde (mundo): los
/// redondeos.
const HOLGURA_BORDE: f32 = 0.01;

/// Lo que dos nodos se pueden tocar sin contar como solape (mundo): apartarlos justo hasta tocarse
/// deja redondeos de unas cienmilésimas que, sin esto, no acaban nunca.
const HOLGURA_CHOQUE: f32 = 1e-3;

/// Aparta los nodos que se tocan. `arrastrado`: el índice + 1 del que lleva el cursor (0, ninguno).
/// Ese no se mueve, salvo contra uno que el borde acaba de devolver, porque ese no tiene adónde ir:
/// entonces cede él.
/// Con la vista de la escena, un nodo que estaba dentro no sale de ella por un choque. Los que ya
/// estaban fuera no se tocan. Devuelve si quedaron sin tocarse. Es O(n²) por pasada: pensado para
/// un menú (cientos de nodos, no miles).
pub fn separar_nodos(escena: &Escena, nodos: &mut [Nodo], arrastrado: u32) -> bool {
    let n = (escena.cuenta_nodos as usize).min(nodos.len());
    let nodos = &mut nodos[..n];
    let bien = |p: &Nodo| p.x.is_finite() && p.y.is_finite() && p.radio.is_finite() && p.radio > 0.0;
    // el radio que se ve de cada nodo; 0, los que no chocan: el vórtice y los que no son finitos
    let radios: Vec<f32> = nodos
        .iter()
        .enumerate()
        .map(|(k, p)| {
            if k + 1 == escena.vortice as usize || !bien(p) {
                0.0
            } else {
                p.radio * escena.factor_radio
            }
        })
        .collect();
    let v = vista(escena);
    let dentro: Vec<bool> = nodos
        .iter()
        .zip(&radios)
        .map(|(p, r)| v.is_some_and(|v| *r > 0.0 && cabe(p.x, v[0] + r, v[2] - r) && cabe(p.y, v[1] + r, v[3] - r)))
        .collect();
    let mut movidos = vec![false; n];
    let mut acotados = vec![false; n];
    for _ in 0..PASADAS {
        let solapes = apartar(nodos, &radios, arrastrado as usize, &acotados, &mut movidos);
        let fuera = match v {
            Some(v) => acotar(nodos, &radios, &dentro, &movidos, arrastrado as usize, v, &mut acotados),
            None => false,
        };
        if !solapes && !fuera {
            return true;
        }
    }
    false
}

/// Una pasada: cada par más cerca que la suma de sus radios de choque se separa justo hasta
/// tocarse, a medias, salvo que uno sea el arrastrado o esté contra el borde. Marca en `movidos`
/// los que apartó y devuelve si había alguno.
fn apartar(nodos: &mut [Nodo], radios: &[f32], arrastrado: usize, acotados: &[bool], movidos: &mut [bool]) -> bool {
    let mut solapes = false;
    for i in 0..nodos.len() {
        for j in i + 1..nodos.len() {
            if radios[i] <= 0.0 || radios[j] <= 0.0 {
                continue;
            }
            let minimo = (radios[i] + radios[j]) * SEPARACION;
            let (dx, dy) = (nodos[j].x - nodos[i].x, nodos[j].y - nodos[i].y);
            let d2 = dx * dx + dy * dy;
            let tope = minimo - HOLGURA_CHOQUE;
            if d2 >= tope * tope {
                continue;
            }
            let d = d2.sqrt();
            // encima exacto: una dirección fija por par, para que se repita
            let (ux, uy) = if d > 1e-4 {
                (dx / d, dy / d)
            } else {
                let a = (i * 31 + j) as f32 * 2.399_963;
                (a.cos(), a.sin())
            };
            let solape = minimo - d;
            // contra uno que el borde acaba de devolver cede el otro entero, aunque sea el
            // arrastrado; si no, el arrastrado no cede
            let (wi, wj) = match (acotados[i], acotados[j]) {
                (false, true) => (1.0, 0.0),
                (true, false) => (0.0, 1.0),
                _ if arrastrado == i + 1 => (0.0, 1.0),
                _ if arrastrado == j + 1 => (1.0, 0.0),
                _ => (0.5, 0.5),
            };
            nodos[i].x -= ux * solape * wi;
            nodos[i].y -= uy * solape * wi;
            nodos[j].x += ux * solape * wj;
            nodos[j].y += uy * solape * wj;
            movidos[i] |= wi > 0.0;
            movidos[j] |= wj > 0.0;
            solapes = true;
        }
    }
    solapes
}

/// Devuelve a la vista, con su círculo dentro, los nodos que un choque apartó y que estaban dentro
/// (el arrastrado lo pone quien integra), y marca en `acotados` los que movió más que la holgura.
/// Devuelve si movió alguno.
fn acotar(
    nodos: &mut [Nodo],
    radios: &[f32],
    dentro: &[bool],
    movidos: &[bool],
    arrastrado: usize,
    v: [f32; 4],
    acotados: &mut [bool],
) -> bool {
    let mut movido = false;
    for (k, p) in nodos.iter_mut().enumerate() {
        if !dentro[k] || !movidos[k] || arrastrado == k + 1 {
            continue;
        }
        let r = radios[k];
        let (x, y) = (acotar_eje(p.x, v[0] + r, v[2] - r), acotar_eje(p.y, v[1] + r, v[3] - r));
        if (x - p.x).abs() > HOLGURA_BORDE || (y - p.y).abs() > HOLGURA_BORDE {
            acotados[k] = true;
            movido = true;
        }
        (p.x, p.y) = (x, y);
    }
    movido
}

fn cabe(x: f32, desde: f32, hasta: f32) -> bool {
    desde <= x && x <= hasta
}

/// `x` dentro de [desde, hasta]; si no cabe, en medio.
fn acotar_eje(x: f32, desde: f32, hasta: f32) -> f32 {
    if desde <= hasta {
        x.clamp(desde, hasta)
    } else {
        (desde + hasta) * 0.5
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn nodo(x: f32, y: f32, radio: f32) -> Nodo {
        Nodo {
            x,
            y,
            radio,
            rgba: 0xffff_ffff,
        }
    }

    /// Sin vórtice ni vista: solo el choque.
    fn escena(cuantos: u32) -> Escena {
        Escena {
            escala: 1.0,
            factor_radio: 1.0,
            cuenta_nodos: cuantos,
            ..Escena::default()
        }
    }

    fn distancia(a: &Nodo, b: &Nodo) -> f32 {
        (a.x - b.x).hypot(a.y - b.y)
    }

    fn sin_tocarse(nodos: &[Nodo], vortice: usize) -> bool {
        (0..nodos.len()).all(|i| {
            (i + 1..nodos.len()).all(|j| {
                i + 1 == vortice
                    || j + 1 == vortice
                    || distancia(&nodos[i], &nodos[j]) >= (nodos[i].radio + nodos[j].radio) * SEPARACION - 1e-3
            })
        })
    }

    #[test]
    fn dos_que_se_tocan_se_apartan_a_medias_hasta_su_separacion() {
        let mut nodos = [nodo(0.0, 0.0, 10.0), nodo(20.0, 0.0, 10.0)];
        assert!(separar_nodos(&escena(2), &mut nodos, 0));
        assert!((distancia(&nodos[0], &nodos[1]) - 30.0).abs() < 1e-4);
        assert!(
            (nodos[0].x + 5.0).abs() < 1e-4 && (nodos[1].x - 25.0).abs() < 1e-4,
            "a medias"
        );
    }

    #[test]
    fn los_que_no_se_tocan_no_cambian() {
        let antes = [nodo(0.0, 0.0, 10.0), nodo(30.0, 0.0, 10.0), nodo(0.0, 31.0, 10.0)];
        let mut nodos = antes;
        assert!(separar_nodos(&escena(3), &mut nodos, 0));
        assert_eq!(nodos, antes);
    }

    #[test]
    fn el_arrastrado_no_se_mueve() {
        let mut nodos = [nodo(0.0, 0.0, 10.0), nodo(5.0, 0.0, 10.0)];
        separar_nodos(&escena(2), &mut nodos, 1);
        assert_eq!((nodos[0].x, nodos[0].y), (0.0, 0.0));
        assert!((nodos[1].x - 30.0).abs() < 1e-4);
    }

    #[test]
    fn el_vortice_no_choca_los_nodos_lo_atraviesan() {
        let antes = [nodo(0.0, 0.0, 60.0), nodo(10.0, 0.0, 16.0)];
        let mut nodos = antes;
        let e = Escena {
            vortice: 1,
            ..escena(2)
        };
        assert!(separar_nodos(&e, &mut nodos, 0));
        assert_eq!(nodos, antes);
    }

    #[test]
    fn encima_exacto_se_apartan_igual_siempre() {
        let mut a = [nodo(3.0, 4.0, 10.0), nodo(3.0, 4.0, 10.0)];
        let mut b = a;
        separar_nodos(&escena(2), &mut a, 0);
        separar_nodos(&escena(2), &mut b, 0);
        assert_eq!(a, b);
        assert!((distancia(&a[0], &a[1]) - 30.0).abs() < 1e-3);
    }

    #[test]
    fn un_monton_queda_sin_tocarse_y_fuera_el_vortice() {
        // el vórtice y doce nodos casi en el mismo sitio, encima de él
        let mut nodos: Vec<Nodo> = (0..13)
            .map(|k| {
                if k == 0 {
                    nodo(0.0, 0.0, 60.0)
                } else {
                    nodo(k as f32 * 0.5, -(k as f32) * 0.25, 16.0)
                }
            })
            .collect();
        let e = Escena {
            vortice: 1,
            ..escena(13)
        };
        // como en fotogramas seguidos: un montón se abre en varios
        for _ in 0..60 {
            if separar_nodos(&e, &mut nodos, 0) {
                break;
            }
        }
        assert!(sin_tocarse(&nodos, 1));
        assert_eq!((nodos[0].x, nodos[0].y), (0.0, 0.0), "el vórtice no se mueve");
    }

    #[test]
    fn un_choque_no_saca_de_la_vista_y_el_arrastrado_cede_contra_el_borde() {
        // vista de -100 a 100; el de la derecha, contra el borde, y el arrastrado lo empuja hacia él
        let e = Escena {
            ancho: 200,
            alto: 200,
            ..escena(2)
        };
        let mut nodos = [nodo(70.0, 0.0, 10.0), nodo(90.0, 0.0, 10.0)];
        separar_nodos(&e, &mut nodos, 1);
        assert!(nodos[1].x <= 90.0 + 1e-4, "no sale de la vista");
        assert!(sin_tocarse(&nodos, 0), "el arrastrado cede");
        assert!(nodos[0].x < 70.0);
    }

    #[test]
    fn los_que_estaban_fuera_de_la_vista_no_se_meten() {
        // vista de -100 a 100; los dos, fuera
        let e = Escena {
            ancho: 200,
            alto: 200,
            ..escena(2)
        };
        let mut nodos = [nodo(300.0, 0.0, 10.0), nodo(310.0, 0.0, 10.0)];
        separar_nodos(&e, &mut nodos, 0);
        assert!(nodos[0].x > 100.0 && nodos[1].x > 100.0);
        assert!(sin_tocarse(&nodos, 0));
    }

    #[test]
    fn solo_los_de_la_escena_y_los_finitos() {
        let mut nodos = [nodo(0.0, 0.0, 10.0), nodo(f32::NAN, 0.0, 10.0), nodo(1.0, 0.0, 10.0)];
        // la escena solo tiene los dos primeros: el tercero no se toca
        separar_nodos(&escena(2), &mut nodos, 0);
        assert_eq!((nodos[0].x, nodos[2].x), (0.0, 1.0));
    }
}
