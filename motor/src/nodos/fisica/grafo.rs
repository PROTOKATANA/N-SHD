//! La física de un grafo con su estado, para quien la usa desde C++ (el visor): lo que en CORTEX hace su hilo
//! (`fisica/hilo.rs`), un paso cada vez que se pide, en el hilo que pinta. Las mismas órdenes y en el mismo orden: las
//! del ratón se aplican al llegar, y el paso solo se da con la física caliente.

use std::collections::HashMap;

use super::colocacion::colocar;
use super::layout::Layout;
use super::ordenes::{CALOR_ARRASTRE, aplicar, liberar, llevar, paso, soltar};
use super::{Limites, Topologia};
use crate::nodos::puente::Nodo;

/// Ver el módulo.
pub struct FisicaGrafo {
    topo: Topologia,
    l: Layout,
}

/// La física de un grafo sin nada guardado: los nodos salen asentados de un tirón, y fríos. `aristas`, pares de
/// índices; `grupos` y `radios` (mundo), uno por nodo.
pub fn fisica_grafo(aristas: &[u32], grupos: &[u32], radios: &[f32]) -> Box<FisicaGrafo> {
    let n = radios.len().min(grupos.len());
    let pares = aristas
        .chunks_exact(2)
        .filter(|p| (p[0] as usize) < n && (p[1] as usize) < n)
        .map(|p| (p[0], p[1]))
        .collect();
    let topo = Topologia::nueva(pares, grupos[..n].to_vec(), radios[..n].to_vec());
    let ids = vec![String::new(); n];
    let inicio = colocar(&ids, &topo, &HashMap::new());
    let mut l = topo.layout(inicio.pos);
    l.alpha = inicio.calor;
    Box::new(FisicaGrafo { topo, l })
}

impl FisicaGrafo {
    /// Los bordes de la vista (mundo): ningún nodo sale de ellos con su círculo. Si metió alguno dentro, la física se
    /// templa para buscarles sitio.
    pub fn limitar(&mut self, x0: f32, y0: f32, x1: f32, y1: f32, factor_radio: f32) {
        let limites = Limites {
            caja: (x0, y0, x1, y1),
            factor_radio,
        };
        if self.l.limitar(limites) {
            self.l.recalentar(CALOR_ARRASTRE);
        }
    }

    /// Lleva el nodo `i` a (x, y) con el ratón: queda clavado ahí y los demás apenas reaccionan.
    pub fn llevar(&mut self, i: u32, x: f32, y: f32) {
        if (i as usize) < self.l.pos.len() {
            for orden in llevar(i as usize, [x, y]) {
                aplicar(&mut self.l, orden);
            }
        }
    }

    /// Se soltó el botón: el nodo que se llevaba se queda clavado donde quedó.
    pub fn soltar(&mut self) {
        aplicar(&mut self.l, soltar());
    }

    /// Desclava el nodo `i` (el clic derecho): vuelve con sus vecinos.
    pub fn liberar(&mut self, i: u32) {
        if (i as usize) < self.l.pos.len() {
            for orden in liberar(i as usize) {
                aplicar(&mut self.l, orden);
            }
        }
    }

    /// Si el nodo `i` está clavado.
    pub fn clavado(&self, i: u32) -> bool {
        self.l.fijos.get(i as usize).is_some_and(Option::is_some)
    }

    /// Un paso, si la física está caliente. Devuelve si movió los nodos.
    pub fn avanzar(&mut self) -> bool {
        if !self.l.activo() {
            return false;
        }
        paso(&self.topo, &mut self.l);
        true
    }

    /// Escribe las posiciones en `nodos` (x, y), en el orden del grafo.
    pub fn posiciones(&self, nodos: &mut [Nodo]) {
        for (n, p) in nodos.iter_mut().zip(&self.l.pos) {
            (n.x, n.y) = (p[0], p[1]);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn nodos(n: usize) -> Vec<Nodo> {
        vec![
            Nodo {
                x: 0.0,
                y: 0.0,
                radio: 22.0,
                rgba: 0
            };
            n
        ]
    }

    /// Lo que hace el hilo de CORTEX con un arrastre: el nodo va con el cursor, se queda clavado al soltar, la física
    /// se enfría, y al liberarlo vuelve a moverse.
    #[test]
    fn como_el_hilo_de_cortex() {
        let aristas = [0, 1, 1, 2, 0, 2, 3, 4];
        let mut f = fisica_grafo(&aristas, &[0, 0, 0, 1, 2], &[22.0; 5]);
        assert!(!f.avanzar(), "sale asentado y frío");
        let mut v = nodos(5);
        f.posiciones(&mut v);
        let destino = (v[3].x + 300.0, v[3].y);
        f.llevar(3, destino.0, destino.1);
        assert!(f.avanzar(), "llevarlo la calienta");
        f.soltar();
        while f.avanzar() {}
        f.posiciones(&mut v);
        assert_eq!((v[3].x, v[3].y), destino, "clavado donde se soltó");
        assert!(f.clavado(3));
        f.liberar(3);
        assert!(!f.clavado(3) && f.avanzar(), "liberado, vuelve a la física");
        // fuera de rango, nada
        f.llevar(9, 0.0, 0.0);
        f.liberar(9);
        assert!(!f.clavado(9));
    }

    #[test]
    fn al_limitar_los_de_fuera_entran() {
        let mut f = fisica_grafo(&[0, 1], &[0, 1], &[22.0; 2]);
        f.limitar(-30.0, -30.0, 30.0, 30.0, 1.0);
        let mut v = nodos(2);
        while f.avanzar() {}
        f.posiciones(&mut v);
        for p in &v {
            assert!(
                p.x.abs() <= 30.0 - 22.0 + 1e-3 && p.y.abs() <= 30.0 - 22.0 + 1e-3,
                "{p:?}"
            );
        }
    }
}
