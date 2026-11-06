//! Las órdenes que el ratón da a la física y cómo se aplican en cada paso.

use super::Topologia;
use super::layout::Layout;

/// Calor de la física al arrastrar o liberar un nodo.
pub const CALOR_ARRASTRE: f32 = 0.1;

/// Al arrastrar o liberar un nodo, cuánto responden los demás a las fuerzas:
/// se mueve ese nodo y el resto apenas reacciona. Medido arrastrando 300
/// unidades: los demás se mueven ≤ 4 durante el arrastre y ≤ 25 al enfriarse
/// (tests::al_arrastrar_un_nodo_los_demas_apenas_se_mueven).
pub const MOVILIDAD_RESTO: f32 = 0.02;

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Orden {
    /// Clava el nodo en esa posición, o lo suelta con None.
    Fijar(usize, Option<[f32; 2]>),
    /// Solo ese nodo responde del todo a las fuerzas hasta que la física se
    /// enfríe (los demás, con MOVILIDAD_RESTO).
    Enfocar(usize),
    /// El nodo que va con el cursor (los choques no lo mueven), o None al
    /// soltarlo.
    Arrastrar(Option<usize>),
    Recalentar(f32),
}

/// Llevar el nodo `i` a `p` con el ratón: queda clavado ahí y los demás
/// apenas reaccionan.
pub fn llevar(i: usize, p: [f32; 2]) -> [Orden; 4] {
    [
        Orden::Arrastrar(Some(i)),
        Orden::Enfocar(i),
        Orden::Fijar(i, Some(p)),
        Orden::Recalentar(CALOR_ARRASTRE),
    ]
}

/// Soltar el botón: ningún nodo va ya con el cursor.
pub fn soltar() -> Orden {
    Orden::Arrastrar(None)
}

/// Desclavar el nodo `i`: vuelve con sus vecinos y los demás apenas
/// reaccionan.
pub fn liberar(i: usize) -> [Orden; 3] {
    [
        Orden::Enfocar(i),
        Orden::Fijar(i, None),
        Orden::Recalentar(CALOR_ARRASTRE),
    ]
}

pub fn aplicar(l: &mut Layout, orden: Orden) {
    match orden {
        Orden::Fijar(i, p) => l.fijar(i, p),
        Orden::Enfocar(i) => l.enfocar(Some(i), MOVILIDAD_RESTO),
        Orden::Arrastrar(i) => l.arrastrado = i,
        Orden::Recalentar(a) => l.recalentar(a),
    }
}

/// Un paso de la física. Al enfriarse, todos vuelven a ser igual de móviles.
/// Devuelve si se acaba de enfriar.
pub fn paso(topo: &Topologia, l: &mut Layout) -> bool {
    topo.tick(l);
    let frio = !l.activo();
    if frio {
        l.enfocar(None, 1.0);
    }
    frio
}

#[cfg(test)]
mod tests {
    use super::{CALOR_ARRASTRE, aplicar, liberar, llevar, paso, soltar};
    use crate::nodos::fisica::Limites;
    use crate::nodos::fisica::colocacion::colocar;
    use crate::nodos::fisica::pruebas::{bordes, dentro, distancia, menu, sin_solapes};
    use std::collections::HashMap;

    /// Lo que se pidió: ningún nodo sale de la vista. Con los bordes a
    /// 40 unidades del grafo, se arrastra «tareas» 600 unidades a la
    /// derecha: se para en el borde y, en ningún paso, sale un nodo ni se
    /// solapan dos.
    #[test]
    fn el_arrastrado_se_para_en_el_borde_y_nadie_sale() {
        let (ids, topo) = menu();
        let base = colocar(&ids, &topo, &HashMap::new()).pos;
        let limites = bordes(&base, &topo.radios, 40.0);
        let mut l = topo.layout(base.clone());
        l.alpha = 0.0;
        assert!(!l.limitar(limites), "todos cabían: no se movió ninguno");
        let nodo = 3;
        let mut p = base[nodo];
        for k in 1..=60 {
            p[0] += 10.0;
            for orden in llevar(nodo, p) {
                aplicar(&mut l, orden);
            }
            paso(&topo, &mut l);
            dentro(&l.pos, &topo.radios, limites).unwrap_or_else(|e| panic!("paso {k}: {e}"));
            sin_solapes(&l.pos, &topo.choque).unwrap_or_else(|e| panic!("paso {k}: {e}"));
        }
        let borde = limites.caja.2 - topo.radios[nodo];
        assert!(
            (l.pos[nodo][0] - borde).abs() < 1e-3,
            "no se paró en el borde ({borde}): {:?}",
            l.pos[nodo]
        );
        aplicar(&mut l, soltar());
        while !paso(&topo, &mut l) {}
        dentro(&l.pos, &topo.radios, limites).unwrap_or_else(|e| panic!("al enfriar: {e}"));
        sin_solapes(&l.pos, &topo.choque).unwrap_or_else(|e| panic!("al enfriar: {e}"));
    }

    /// Al encoger la vista, los nodos que se salen entran ya, el clavado
    /// sigue clavado (en su sitio nuevo) y, templada la física, ninguno se
    /// solapa ni vuelve a salir.
    #[test]
    fn al_encoger_la_vista_los_nodos_entran() {
        let (ids, topo) = menu();
        let base = colocar(&ids, &topo, &HashMap::new()).pos;
        let mut l = topo.layout(base.clone());
        l.alpha = 0.0;
        l.fijar(3, Some(base[3]));
        // un 10 % menos por cada lado
        let todo = bordes(&base, &topo.radios, 0.0).caja;
        let (mx, my) = ((todo.2 - todo.0) * 0.1, (todo.3 - todo.1) * 0.1);
        let limites = Limites {
            caja: (todo.0 + mx, todo.1 + my, todo.2 - mx, todo.3 - my),
            factor_radio: 1.0,
        };
        assert!(l.limitar(limites), "había nodos fuera");
        dentro(&l.pos, &topo.radios, limites).unwrap_or_else(|e| panic!("al limitar: {e}"));
        assert_eq!(l.fijos[3], Some(l.pos[3]), "el clavado sigue clavado");
        l.recalentar(CALOR_ARRASTRE);
        while !paso(&topo, &mut l) {}
        dentro(&l.pos, &topo.radios, limites).unwrap_or_else(|e| panic!("al enfriar: {e}"));
        sin_solapes(&l.pos, &topo.choque).unwrap_or_else(|e| panic!("al enfriar: {e}"));
    }

    /// Marca los nodos que tocan a `nodo` (a la suma de sus radios de
    /// choque): esos se apartan por el choque, no por las fuerzas.
    fn marcar_tocados(pos: &[[f32; 2]], choque: &[f32], nodo: usize, tocados: &mut [bool]) {
        for (i, t) in tocados.iter_mut().enumerate() {
            if i != nodo && distancia(pos[i], pos[nodo]) <= choque[i] + choque[nodo] + 0.5 {
                *t = true;
            }
        }
    }

    /// Lo que se pidió: al mover un nodo se mueve ese nodo y los demás
    /// apenas. Se arrastra 300 unidades (30 avisos del ratón, uno por paso
    /// de la física) con las mismas órdenes que manda el hub. Los que el
    /// arrastrado toca por el camino se apartan por el choque (eso lo prueba
    /// arrastrar_un_nodo_encima_de_otro_lo_aparta): aquí no cuentan.
    #[test]
    fn al_arrastrar_un_nodo_los_demas_apenas_se_mueven() {
        let (ids, topo) = menu();
        let base = colocar(&ids, &topo, &HashMap::new()).pos;
        // «tareas» (2 enlaces) e «inicio» (el centro, 6 enlaces)
        for nodo in [3, 0] {
            let mut l = topo.layout(base.clone());
            l.alpha = 0.0;
            let mut tocados = [false; 7];
            let mut p = base[nodo];
            let mut durante = 0.0f32;
            for _ in 0..30 {
                p[0] += 10.0;
                for orden in llevar(nodo, p) {
                    aplicar(&mut l, orden);
                }
                paso(&topo, &mut l);
                assert_eq!(l.pos[nodo], p, "el nodo arrastrado va con el cursor");
                marcar_tocados(&l.pos, &topo.choque, nodo, &mut tocados);
                durante = (0..7)
                    .filter(|&i| i != nodo && !tocados[i])
                    .map(|i| distancia(l.pos[i], base[i]))
                    .fold(durante, f32::max);
            }
            aplicar(&mut l, soltar());
            while !paso(&topo, &mut l) {
                marcar_tocados(&l.pos, &topo.choque, nodo, &mut tocados);
            }
            let otros = || (0..7).filter(move |&i| i != nodo && !tocados[i]);
            let tras = otros().map(|i| distancia(l.pos[i], base[i])).fold(0.0, f32::max);
            println!(
                "nodo {nodo}: los demás, máx {durante:.1} durante y {tras:.1} al enfriar (arrastre 300; tocados {:?})",
                (0..7).filter(|&i| tocados[i]).collect::<Vec<_>>()
            );
            assert!(
                durante <= 15.0,
                "nodo {nodo}: los demás se movieron {durante} durante el arrastre"
            );
            assert!(tras <= 30.0, "nodo {nodo}: los demás se movieron {tras} al enfriar");

            // liberado (clic derecho): él vuelve con sus vecinos; los demás, quietos
            let antes = l.pos.clone();
            for orden in liberar(nodo) {
                aplicar(&mut l, orden);
            }
            while !paso(&topo, &mut l) {}
            let vuelve = distancia(l.pos[nodo], antes[nodo]);
            let resto = otros().map(|i| distancia(l.pos[i], antes[i])).fold(0.0, f32::max);
            println!("nodo {nodo}: liberado vuelve {vuelve:.1}; los demás {resto:.1}");
            assert!(vuelve > 100.0, "nodo {nodo}: liberado no volvió ({vuelve})");
            assert!(resto <= 15.0, "nodo {nodo}: al liberar, los demás se movieron {resto}");
        }
    }

    /// Lo que se pidió: los nodos no se pueden superponer. Se arrastra
    /// «tareas» hasta encima de «inicio»: en ningún paso quedan solapados,
    /// el arrastrado va siempre con el cursor y es el otro el que se aparta.
    #[test]
    fn arrastrar_un_nodo_encima_de_otro_lo_aparta() {
        let (ids, topo) = menu();
        let base = colocar(&ids, &topo, &HashMap::new()).pos;
        let mut l = topo.layout(base.clone());
        l.alpha = 0.0;
        let (nodo, destino) = (3, base[0]);
        for k in 1..=40 {
            let t = k as f32 / 40.0;
            let p = [
                base[nodo][0] + (destino[0] - base[nodo][0]) * t,
                base[nodo][1] + (destino[1] - base[nodo][1]) * t,
            ];
            for orden in llevar(nodo, p) {
                aplicar(&mut l, orden);
            }
            paso(&topo, &mut l);
            assert_eq!(l.pos[nodo], p, "el nodo arrastrado va con el cursor");
            sin_solapes(&l.pos, &topo.choque).unwrap_or_else(|e| panic!("paso {k}: {e}"));
        }
        assert!(distancia(l.pos[0], base[0]) > 50.0, "«inicio» no se apartó");
        // se quedan justo a la suma de sus radios de choque (1,5 radios)
        let tocar = topo.choque[nodo] + topo.choque[0];
        let d = distancia(l.pos[nodo], l.pos[0]);
        assert!((d - tocar).abs() < 0.5, "a {d:.1}, no a {tocar}");
        aplicar(&mut l, soltar());
        while !paso(&topo, &mut l) {}
        sin_solapes(&l.pos, &topo.choque).unwrap_or_else(|e| panic!("al enfriar: {e}"));
    }
}
