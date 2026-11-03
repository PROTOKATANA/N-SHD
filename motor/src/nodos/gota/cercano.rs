//! La silueta del líquido de los enlaces cerca del vórtice: las paredes de los canales de la gota.

use super::super::liquido::FORMA as LIQUIDO;
use super::super::puente::{Escena, GotaGpu, Nodo};

/// Gotas del líquido de los enlaces que se copian cerca del vórtice.
pub const MAX_GOTAS: usize = 12;

/// Tubos del líquido de los enlaces que se copian cerca del vórtice.
pub const MAX_TUBOS: usize = 16;

/// radios: lo que se aleja una gota del borde del vórtice y aún cuenta.
const CERCA_LIQUIDO: f32 = 1.2;

/// La silueta del líquido de los enlaces cerca del vórtice, igual que la pinta `PasadaLiquido`: sus
/// gotas y sus tubos, en el mismo orden y con la misma unión suave. Es la pared de las tuberías de
/// la gota: así un puente que engorda, porque sus nodos se juntan, es una tubería más ancha. Todo
/// relativo al vórtice y en sus radios.
pub(super) fn liquido_cercano(
    escena: &Escena,
    nodos: &[Nodo],
    aristas: &[u32],
    i: usize,
    factor: f32,
    g: &mut GotaGpu,
) {
    let cuantos = (escena.cuenta_nodos as usize).min(nodos.len());
    let c = nodos[i];
    let unidad = c.radio * factor;
    let bien = |n: &Nodo| n.x.is_finite() && n.y.is_finite() && n.radio.is_finite();
    // el radio con que el líquido pinta cada nodo: el del vórtice, encogido
    let radio_liquido = |j: usize| {
        let encoge = if j == i {
            LIQUIDO.gota_vortice / LIQUIDO.gota
        } else {
            1.0
        };
        nodos[j].radio * factor * encoge
    };
    // los más cercanos al borde del vórtice, y luego en su orden, que es el de la unión suave
    let mut cerca: Vec<(f32, usize)> = (0..cuantos)
        .filter(|j| bien(&nodos[*j]))
        .map(|j| {
            let n = nodos[j];
            let borde = ((n.x - c.x).hypot(n.y - c.y) - LIQUIDO.gota * radio_liquido(j)) / unidad - 1.0;
            (if j == i { f32::MIN } else { borde }, j)
        })
        .filter(|(borde, _)| *borde < CERCA_LIQUIDO)
        .collect();
    cerca.sort_by(|a, b| a.0.total_cmp(&b.0));
    cerca.truncate(MAX_GOTAS);
    let mut dentro: Vec<usize> = cerca.into_iter().map(|(_, j)| j).collect();
    dentro.sort_unstable();
    g.gotas = [[0.0; 4]; MAX_GOTAS];
    for (k, j) in dentro.iter().enumerate() {
        let n = nodos[*j];
        let r = radio_liquido(*j);
        let hundir = if *j == i && escena.vortice_libre {
            (LIQUIDO.gota_vortice - LIQUIDO.gota_vortice_libre) * n.radio * factor
        } else {
            0.0
        };
        g.gotas[k] = [
            (n.x - c.x) / unidad,
            (n.y - c.y) / unidad,
            (LIQUIDO.gota * r - hundir) / unidad,
            LIQUIDO.fusion * r / unidad,
        ];
    }
    // los tubos que tocan alguna de esas gotas, y los que pasan cerca de la gota aunque sus
    // extremos queden lejos: los que cruzan por debajo también se funden con ella. Si no caben
    // todos, los más cercanos, y luego en su orden
    let local = |n: &Nodo| [(n.x - c.x) / unidad, (n.y - c.y) / unidad];
    let mut tubos: Vec<(f32, usize, usize, usize)> = aristas
        .chunks_exact(2)
        .enumerate()
        .filter_map(|(orden, par)| {
            let (a, b) = (par[0] as usize, par[1] as usize);
            if a == b || a >= cuantos || b >= cuantos || !bien(&nodos[a]) || !bien(&nodos[b]) {
                return None;
            }
            let lejos = if dentro.contains(&a) || dentro.contains(&b) {
                0.0
            } else {
                distancia_al_centro(local(&nodos[a]), local(&nodos[b]))
            };
            (lejos < CERCA_TUBO).then_some((lejos, orden, a, b))
        })
        .collect();
    tubos.sort_by(|x, y| x.0.total_cmp(&y.0));
    tubos.truncate(MAX_TUBOS);
    tubos.sort_by_key(|t| t.1);
    g.tubos = [[0.0; 4]; MAX_TUBOS];
    g.anchos = [[0.0; 4]; MAX_TUBOS / 4];
    g.funde = [[0.0; 4]; MAX_TUBOS / 4];
    for (t, (_, _, a, b)) in tubos.iter().enumerate() {
        let (pa, pb) = (local(&nodos[*a]), local(&nodos[*b]));
        g.tubos[t] = [pa[0], pa[1], pb[0], pb[1]];
        g.anchos[t / 4][t % 4] = LIQUIDO.cuello * radio_liquido(*a).min(radio_liquido(*b)) / unidad;
        // la gota se funde del todo con sus enlaces y con los que pasan por debajo de ella; con uno
        // ajeno que se acerca, poco a poco: de golpe tendería un puente hasta él
        g.funde[t / 4][t % 4] = if *a == i || *b == i {
            1.0
        } else {
            ((FUNDE_HASTA - distancia_al_centro(pa, pb)) / FUNDE_BANDA).clamp(0.0, 1.0)
        };
    }
    g.cuenta_liquido = [dentro.len() as f32, tubos.len() as f32, 0.0, 0.0];
}

/// radios: lo que se aleja del centro del vórtice un tubo que aún cuenta, aunque sus extremos estén
/// lejos: lo que llega el líquido por los canales y la unión suave, con holgura.
const CERCA_TUBO: f32 = 2.5;

/// radios desde el centro: un enlace ajeno que pasa a esta distancia empieza a fundirse con la gota,
/// y del todo a `FUNDE_BANDA` menos.
const FUNDE_HASTA: f32 = 1.3;

const FUNDE_BANDA: f32 = 0.35;

/// Lo cerca que pasa del centro (0, 0) el segmento de `a` a `b`.
fn distancia_al_centro(a: [f32; 2], b: [f32; 2]) -> f32 {
    let ab = [b[0] - a[0], b[1] - a[1]];
    let largo2 = ab[0] * ab[0] + ab[1] * ab[1];
    let u = if largo2 > 1e-12 {
        (-(a[0] * ab[0] + a[1] * ab[1]) / largo2).clamp(0.0, 1.0)
    } else {
        0.0
    };
    (a[0] + ab[0] * u).hypot(a[1] + ab[1] * u)
}

#[cfg(test)]
mod tests {
    use super::super::prueba::{escena, nodo};
    use super::*;

    #[test]
    fn los_canales_copian_la_silueta_del_liquido_de_los_enlaces_cerca_del_vortice() {
        let mut g = GotaGpu::default();
        let e = Escena {
            cuenta_nodos: 3,
            ..escena()
        };
        // uno cerca, unido al vórtice, y otro lejos, unido al de cerca
        let nodos = [nodo(0.0, 0.0, 10.0), nodo(25.0, 0.0, 2.0), nodo(200.0, 0.0, 2.0)];
        liquido_cercano(&e, &nodos, &[0, 1, 1, 2], 0, 1.0, &mut g);
        assert_eq!(g.cuenta_liquido[0], 2.0, "el vórtice y el de cerca");
        assert_eq!(g.cuenta_liquido[1], 2.0, "los dos enlaces que tocan a alguno de ellos");
        assert_eq!(g.tubos[0], [0.0, 0.0, 2.5, 0.0], "en radios del vórtice");
        let cuello = LIQUIDO.cuello * 0.2;
        assert!((g.anchos[0][0] - cuello).abs() < 1e-6, "el cuello sale del radio menor");
        assert_eq!(g.funde[0][1], 0.0, "el que solo pasa cerca no se funde con la gota");
        assert_eq!(g.funde[0][0], 1.0, "el suyo, del todo");
        assert!(
            (g.gotas[1][2] - LIQUIDO.gota * 0.2).abs() < 1e-6,
            "la gota del de cerca"
        );
    }

    #[test]
    fn un_enlace_que_cruza_por_debajo_tambien_cuenta() {
        let mut g = GotaGpu::default();
        let e = Escena {
            cuenta_nodos: 3,
            ..escena()
        };
        // dos nodos lejos, a cada lado, unidos por un enlace que pasa por debajo del vórtice
        let nodos = [nodo(0.0, 0.0, 10.0), nodo(-100.0, 5.0, 2.0), nodo(100.0, 5.0, 2.0)];
        liquido_cercano(&e, &nodos, &[1, 2], 0, 1.0, &mut g);
        assert_eq!(g.cuenta_liquido[0], 1.0, "solo el vórtice está cerca");
        assert_eq!(g.cuenta_liquido[1], 1.0, "pero el enlace que lo cruza cuenta");
        assert_eq!(g.tubos[0], [-10.0, 0.5, 10.0, 0.5]);
        assert_eq!(g.funde[0][0], 1.0, "y se funde con la gota");
        // y uno lejos no
        let lejos = [nodo(0.0, 0.0, 10.0), nodo(-100.0, 80.0, 2.0), nodo(100.0, 80.0, 2.0)];
        liquido_cercano(&e, &lejos, &[1, 2], 0, 1.0, &mut g);
        assert_eq!(g.cuenta_liquido[1], 0.0);
        assert!((distancia_al_centro([-1.0, 2.0], [1.0, 2.0]) - 2.0).abs() < 1e-6);
        assert!(
            (distancia_al_centro([3.0, 4.0], [6.0, 8.0]) - 5.0).abs() < 1e-6,
            "el extremo más cercano"
        );
    }
}
