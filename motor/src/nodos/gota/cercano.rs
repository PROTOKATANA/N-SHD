//! La silueta del líquido de los enlaces cerca del vórtice: las paredes de los canales de la gota.

use super::super::liquido::FORMA as LIQUIDO;
use super::super::puente::{Escena, GotaGpu, Nodo};

/// Tubos del líquido de los enlaces que se copian cerca del vórtice: sus enlaces.
pub const MAX_TUBOS: usize = 16;

/// La silueta de los canales de la gota, igual que la pinta `PasadaLiquido`: el círculo del vórtice
/// y sus enlaces, en el mismo orden y con la misma unión suave. La gota va envuelta en el anillo que
/// le pinta el líquido de los enlaces y es una esfera: sus canales son solo sus enlaces. Lo ajeno
/// que pasa cerca o por debajo no cuenta, o su canal pintaba sobre el borde de la gota y la cortaba
/// en recto; la deforman los nodos. Todo relativo al vórtice y en sus radios.
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
    // El círculo del vórtice, siempre hundido bajo la gota : los canales salen de él. El líquido
    // de los enlaces le pinta un anillo; si el canal copiara su círculo, su líquido lo llenaría y
    // lo taparía
    let hay = i < cuantos && bien(&c);
    g.circulo = [0.0; 4];
    if hay {
        let r = radio_liquido(i);
        let hundir = (LIQUIDO.gota_vortice - LIQUIDO.gota_vortice_hundida) * c.radio * factor;
        g.circulo = [
            0.0,
            0.0,
            (LIQUIDO.gota * r - hundir) / unidad,
            LIQUIDO.fusion * r / unidad,
        ];
    }
    // sus enlaces, en su orden; si no caben todos, los primeros
    let local = |n: &Nodo| [(n.x - c.x) / unidad, (n.y - c.y) / unidad];
    let tubos: Vec<(usize, usize)> = aristas
        .chunks_exact(2)
        .map(|par| (par[0] as usize, par[1] as usize))
        .filter(|&(a, b)| {
            hay && a != b && a < cuantos && b < cuantos && (a == i || b == i) && bien(&nodos[a]) && bien(&nodos[b])
        })
        .take(MAX_TUBOS)
        .collect();
    g.tubos = [[0.0; 4]; MAX_TUBOS];
    g.anchos = [[0.0; 4]; MAX_TUBOS / 4];
    for (t, &(a, b)) in tubos.iter().enumerate() {
        let (pa, pb) = (local(&nodos[a]), local(&nodos[b]));
        g.tubos[t] = [pa[0], pa[1], pb[0], pb[1]];
        g.anchos[t / 4][t % 4] = LIQUIDO.cuello * radio_liquido(a).min(radio_liquido(b)) / unidad;
    }
    g.cuenta_liquido = [if hay { 1.0 } else { 0.0 }, tubos.len() as f32, 0.0, 0.0];
}

#[cfg(test)]
mod tests {
    use super::super::prueba::{escena, nodo};
    use super::*;

    #[test]
    fn sus_canales_son_solo_sus_enlaces() {
        let mut g = GotaGpu::default();
        let e = Escena {
            cuenta_nodos: 5,
            ..escena()
        };
        // uno suyo; uno ajeno que pasa por debajo, entre dos nodos cerca; y otro ajeno, que sale del
        // suyo
        let nodos = [
            nodo(0.0, 0.0, 10.0),
            nodo(25.0, 0.0, 2.0),
            nodo(-15.0, 5.0, 2.0),
            nodo(15.0, 5.0, 2.0),
            nodo(200.0, 0.0, 2.0),
        ];
        liquido_cercano(&e, &nodos, &[0, 1, 2, 3, 1, 4], 0, 1.0, &mut g);
        assert_eq!(g.cuenta_liquido, [1.0, 1.0, 0.0, 0.0], "el vórtice y su enlace");
        assert_eq!(g.tubos[0], [0.0, 0.0, 2.5, 0.0], "en radios del vórtice");
        let cuello = LIQUIDO.cuello * 0.2;
        assert!((g.anchos[0][0] - cuello).abs() < 1e-6, "el cuello sale del radio menor");
    }

    #[test]
    fn el_canal_nunca_copia_el_circulo_del_vortice() {
        let mut g = GotaGpu::default();
        let e = Escena {
            cuenta_nodos: 2,
            ..escena()
        };
        let nodos = [nodo(0.0, 0.0, 10.0), nodo(25.0, 0.0, 2.0)];
        liquido_cercano(&e, &nodos, &[0, 1], 0, 1.0, &mut g);
        assert!(
            (g.circulo[2] - LIQUIDO.gota_vortice_hundida).abs() < 1e-5,
            "hundido bajo la gota"
        );
    }

    #[test]
    fn sin_vortice_bien_no_hay_canales() {
        let mut g = GotaGpu::default();
        let e = Escena {
            cuenta_nodos: 2,
            ..escena()
        };
        let nodos = [nodo(f32::NAN, 0.0, 10.0), nodo(25.0, 0.0, 2.0)];
        liquido_cercano(&e, &nodos, &[0, 1], 0, 1.0, &mut g);
        assert_eq!(g.cuenta_liquido, [0.0; 4]);
    }
}
