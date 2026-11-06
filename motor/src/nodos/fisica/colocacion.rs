//! Dónde empieza cada nodo: el grafo asentado de un tirón, o la disposición
//! guardada (con los nodos nuevos junto a sus vecinos).

use super::layout::{self, Layout};
use super::{Topologia, fuerzas_para};
use std::collections::HashMap;

/// Dónde se dejó un nodo, y si quedó clavado: lo que guarda quien integra (en CORTEX, `disposicion`).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Lugar {
    pub x: f32,
    pub y: f32,
    pub fijo: bool,
}

/// Tope de pasos al asentar la física de un tirón (se enfría en ~300).
const PASOS_MAX: usize = 2000;

/// Tope de pasadas para separar de un tirón una disposición guardada (en la
/// física basta con layout::PASADAS_CHOQUE por paso: se repite en cada uno).
const PASADAS_AL_CARGAR: usize = 500;

/// Calor con que arranca la física si hay nodos nuevos que colocar entre los
/// guardados.
pub const CALOR_NUEVOS: f32 = 0.5;

/// Cómo arranca la física.
pub struct Inicio {
    pub pos: Vec<[f32; 2]>,
    /// Qué nodos arrancan clavados.
    pub fijos: Vec<bool>,
    /// Calor inicial: 0 si todo está ya en su sitio.
    pub calor: f32,
}

/// Mezclador determinista (splitmix64): pseudoaleatoriedad reproducible por
/// semilla, sin dependencias. El de HILO-CP.
fn mezclar(semilla: u64, v: u64) -> u64 {
    let mut z = semilla
        .wrapping_add(0x9E37_79B9_7F4A_7C15)
        .wrapping_add(v.wrapping_mul(0xBF58_476D_1CE4_E5B9));
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    z ^ (z >> 31)
}

/// Número en [0, 1) a partir del mezclador.
fn azar(semilla: u64, v: u64) -> f32 {
    (mezclar(semilla, v) >> 40) as f32 / (1u64 << 24) as f32
}

/// Corre la física hasta que se enfría.
fn asentar(topo: &Topologia, pos: Vec<[f32; 2]>) -> Vec<[f32; 2]> {
    let mut l = topo.layout(pos);
    for _ in 0..PASOS_MAX {
        if !topo.tick(&mut l) {
            break;
        }
    }
    l.pos
}

/// Sin nada guardado, el grafo asentado de un tirón (primer fotograma
/// limpio, sin animación). Con algo guardado, cada nodo donde se dejó
/// (separando los que se solapen); los nuevos, junto a sus vecinos ya
/// colocados, y la física templada para que encuentren sitio.
pub fn colocar(ids: &[String], topo: &Topologia, guardado: &HashMap<String, Lugar>) -> Inicio {
    let n = ids.len();
    let lugares: Vec<Option<Lugar>> = ids.iter().map(|id| guardado.get(id).copied()).collect();
    if lugares.iter().all(Option::is_none) {
        return Inicio {
            pos: asentar(topo, Layout::nuevo(n, fuerzas_para(n)).pos),
            fijos: vec![false; n],
            calor: 0.0,
        };
    }
    let espiral = Layout::nuevo(n, fuerzas_para(n)).pos;
    let mut pos = Vec::with_capacity(n);
    for (i, lugar) in lugares.iter().enumerate() {
        if let Some(l) = lugar {
            pos.push([l.x, l.y]);
            continue;
        }
        let vecinos: Vec<[f32; 2]> = topo
            .aristas
            .iter()
            .filter_map(|&(a, b)| match (a as usize, b as usize) {
                (a, b) if a == i => lugares[b].map(|l| [l.x, l.y]),
                (a, b) if b == i => lugares[a].map(|l| [l.x, l.y]),
                _ => None,
            })
            .collect();
        if vecinos.is_empty() {
            pos.push(espiral[i]);
        } else {
            let k = vecinos.len() as f32;
            let c = vecinos
                .iter()
                .fold([0.0, 0.0], |s, p| [s[0] + p[0] / k, s[1] + p[1] / k]);
            // a una distancia de enlace del centro de sus vecinos, en una
            // dirección propia (dos nuevos con los mismos vecinos no caen
            // uno encima del otro)
            let a = azar(i as u64, 7) * std::f32::consts::TAU;
            let d = fuerzas_para(n).distancia_enlace;
            pos.push([c[0] + d * a.cos(), c[1] + d * a.sin()]);
        }
    }
    let mut fijos: Vec<Option<[f32; 2]>> = lugares
        .iter()
        .zip(&pos)
        .map(|(l, p)| l.is_some_and(|l| l.fijo).then_some(*p))
        .collect();
    layout::separar(&mut pos, &topo.choque, &mut fijos, None, PASADAS_AL_CARGAR);
    Inicio {
        calor: if lugares.iter().any(Option::is_none) {
            CALOR_NUEVOS
        } else {
            0.0
        },
        fijos: fijos.iter().map(Option::is_some).collect(),
        pos,
    }
}

#[cfg(test)]
mod tests {
    use super::{CALOR_NUEVOS, Lugar, colocar};
    use crate::nodos::fisica::pruebas::{distancia, menu, sin_solapes};
    use std::collections::HashMap;

    /// Sin nada guardado, el grafo sale asentado: ningún par de nodos se
    /// solapa ni queda absurdamente lejos, y la física arranca fría.
    #[test]
    fn sin_guardar_el_menu_sale_asentado() {
        let (ids, topo) = menu();
        let inicio = colocar(&ids, &topo, &HashMap::new());
        let mut minima = f32::MAX;
        let mut maxima = 0.0f32;
        for i in 0..inicio.pos.len() {
            for j in i + 1..inicio.pos.len() {
                let d = distancia(inicio.pos[i], inicio.pos[j]);
                minima = minima.min(d);
                maxima = maxima.max(d);
            }
        }
        println!("distancias entre nodos: mínima {minima:.1}, máxima {maxima:.1}");
        // radio de nodo 22: sin solapes y con aire para el título
        assert!(minima > 90.0, "nodos demasiado juntos: {minima}");
        assert!(maxima < 1200.0, "grafo demasiado disperso: {maxima}");
        assert_eq!(inicio.fijos, vec![false; 7]);
        assert_eq!(inicio.calor, 0.0);
    }

    #[test]
    fn con_todo_guardado_cada_nodo_vuelve_a_su_sitio() {
        // separados 250: más que el choque de «inicio» (el vórtice) con su
        // vecino, que es (88 + 22) × 1,5
        let (ids, topo) = menu();
        let guardado: HashMap<String, Lugar> = ids
            .iter()
            .enumerate()
            .map(|(i, id)| {
                (
                    id.clone(),
                    Lugar {
                        x: i as f32 * 250.0,
                        y: -(i as f32),
                        fijo: i == 3,
                    },
                )
            })
            .collect();
        let inicio = colocar(&ids, &topo, &guardado);
        for (i, p) in inicio.pos.iter().enumerate() {
            assert_eq!(*p, [i as f32 * 250.0, -(i as f32)]);
        }
        assert_eq!(inicio.fijos, vec![false, false, false, true, false, false, false]);
        assert_eq!(inicio.calor, 0.0);
    }

    #[test]
    fn un_nodo_nuevo_aparece_junto_a_sus_vecinos() {
        let (ids, topo) = menu();
        // «notas» (6) es nuevo; sus vecinos son inicio (0) y calendario (4)
        let mut guardado = HashMap::new();
        for (i, id) in ids.iter().enumerate().take(6) {
            guardado.insert(
                id.clone(),
                Lugar {
                    x: 1000.0 + i as f32 * 200.0,
                    y: 0.0,
                    fijo: false,
                },
            );
        }
        let inicio = colocar(&ids, &topo, &guardado);
        assert_eq!(inicio.pos[0], [1000.0, 0.0]);
        // centro de sus vecinos: x = (1000 + 1800) / 2 = 1400; a 150 de él
        let d = ((inicio.pos[6][0] - 1400.0).powi(2) + inicio.pos[6][1].powi(2)).sqrt();
        assert!((d - 150.0).abs() < 0.01, "lejos de sus vecinos: {:?}", inicio.pos[6]);
        assert_eq!(inicio.calor, CALOR_NUEVOS);
    }

    #[test]
    fn una_disposicion_guardada_solapada_se_separa_al_cargar() {
        let (ids, topo) = menu();
        // todos en fila a 10 unidades (muy solapados); «tareas» clavado
        let guardado: HashMap<String, Lugar> = ids
            .iter()
            .enumerate()
            .map(|(i, id)| {
                (
                    id.clone(),
                    Lugar {
                        x: i as f32 * 10.0,
                        y: 0.0,
                        fijo: i == 3,
                    },
                )
            })
            .collect();
        let inicio = colocar(&ids, &topo, &guardado);
        sin_solapes(&inicio.pos, &topo.choque).unwrap_or_else(|e| panic!("{e}"));
        assert!(inicio.fijos[3], "el clavado sigue clavado (en su sitio nuevo)");
    }
}
