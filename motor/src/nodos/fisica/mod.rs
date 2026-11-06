//! La física del grafo de CORTEX, copiada tal cual (rama de las pestañas, `N-CNX/grafo/motor/src/fisica`): el
//! layout de fuerzas, la colocación inicial y las órdenes del ratón. Lo que es de la aplicación se queda en ella: el
//! hilo que la hace correr y la disposición guardada (aquí, `colocacion::Lugar`). Solo cambia lo que no cambia
//! el resultado: la repulsión se calcula en orden, sin rayon, y los comentarios no nombran a nadie.

pub mod colocacion;
pub mod grafo;
pub mod layout;
pub mod ordenes;

pub use ordenes::Orden;

/// Lo que se ve, en mundo (x0, y0, x1, y1): ningún nodo sale de ahí con su
/// círculo (2026-10-28). `factor_radio` es el de la cámara: cuenta
/// el radio que se ve.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Limites {
    pub caja: (f32, f32, f32, f32),
    pub factor_radio: f32,
}

/// Radio de choque de un nodo, en múltiplos de su radio: dos nodos nunca
/// quedan más cerca que la suma de los suyos. Era el de la gota del líquido
/// (1,6, `forma_liquido` en linux/liquido.h), para que ni los nodos ni sus
/// gotas se montaran; se dejó en 1,5 (2026-10-28), un poco por dentro
/// de la gota: dos nodos juntos se acercan algo más y sus gotas apenas se
/// rozan.
pub const SEPARACION: f32 = 1.5;

/// Fuerzas para un menú: nodos grandes y separados, cúmulos por grupo.
pub fn fuerzas_menu() -> layout::Fuerzas {
    layout::Fuerzas {
        repulsion: 3000.0,
        distancia_enlace: 150.0,
        fuerza_enlace: 0.4,
        fuerza_centro: 0.02,
        fuerza_grupo: 0.06,
    }
}

/// Nodos del menú del login, para el que están afinadas [`fuerzas_menu`].
const NODOS_MENU: usize = 7;

/// Las fuerzas para un grafo de `n` nodos: hasta el tamaño del menú, las
/// suyas. Con más nodos en la misma vista (las pestañas), la repulsión y los
/// enlaces encogen para que quepan sin aplastarse contra los bordes, y los
/// grupos se aprietan en cúmulos.
pub fn fuerzas_para(n: usize) -> layout::Fuerzas {
    let menu = fuerzas_menu();
    if n <= NODOS_MENU {
        return menu;
    }
    let k = NODOS_MENU as f32 / n as f32;
    layout::Fuerzas {
        repulsion: menu.repulsion * k.powf(0.75),
        distancia_enlace: menu.distancia_enlace * k.powf(0.25),
        fuerza_grupo: menu.fuerza_grupo * 2.5,
        ..menu
    }
}

/// Lo que la física necesita del grafo, que no cambia mientras corre.
pub struct Topologia {
    pub aristas: Vec<(u32, u32)>,
    pesos: Vec<f32>,
    /// Largo en reposo de cada enlace (ver `nueva`).
    largos: Vec<f32>,
    grupos: Vec<u32>,
    n_grupos: u32,
    /// Radio de cada nodo: el de su círculo, el que cuenta contra los bordes.
    pub radios: Vec<f32>,
    /// Radio de choque de cada nodo (ver SEPARACION).
    pub choque: Vec<f32>,
}

impl Topologia {
    pub fn nueva(aristas: Vec<(u32, u32)>, grupos: Vec<u32>, radios: Vec<f32>) -> Self {
        let choque: Vec<f32> = radios.iter().map(|r| r * SEPARACION).collect();
        let mut grados = vec![0u32; choque.len()];
        for &(a, b) in &aristas {
            grados[a as usize] += 1;
            grados[b as usize] += 1;
        }
        // peso por arista estilo d3 (1/min de los grados)
        let pesos = aristas
            .iter()
            .map(|&(a, b)| 1.0 / grados[a as usize].min(grados[b as usize]).max(1) as f32)
            .collect();
        // Un enlace mide lo mismo de borde a borde: si sus nodos son más
        // grandes que el más pequeño (el vórtice), se alarga lo que crecen
        let minimo = choque.iter().copied().fold(f32::INFINITY, f32::min);
        let crece = |i: u32| (choque[i as usize] - minimo) / SEPARACION;
        let distancia = fuerzas_para(choque.len()).distancia_enlace;
        let largos = aristas.iter().map(|&(a, b)| distancia + crece(a) + crece(b)).collect();
        let n_grupos = grupos.iter().copied().max().map_or(0, |g| g + 1);
        Self {
            aristas,
            pesos,
            largos,
            grupos,
            n_grupos,
            radios,
            choque,
        }
    }

    /// La física de este grafo, empezando en `pos`.
    pub fn layout(&self, pos: Vec<[f32; 2]>) -> layout::Layout {
        let mut l = layout::Layout::nuevo(pos.len(), fuerzas_para(pos.len()));
        l.pos = pos;
        l.radios.clone_from(&self.radios);
        l.choque.clone_from(&self.choque);
        l
    }

    pub fn tick(&self, l: &mut layout::Layout) -> bool {
        l.tick(&self.aristas, &self.pesos, &self.largos, &self.grupos, self.n_grupos)
    }
}

#[cfg(test)]
pub(crate) mod pruebas {
    use super::{Limites, Topologia};

    /// Caja que contiene todos los nodos, con su radio (de `camara.rs` de CORTEX, que se queda allí).
    fn caja(pos: &[[f32; 2]], radios: &[f32]) -> Option<(f32, f32, f32, f32)> {
        if pos.is_empty() {
            return None;
        }
        let mut c = (f32::MAX, f32::MAX, f32::MIN, f32::MIN);
        for (p, &r) in pos.iter().zip(radios) {
            c.0 = c.0.min(p[0] - r);
            c.1 = c.1.min(p[1] - r);
            c.2 = c.2.max(p[0] + r);
            c.3 = c.3.max(p[1] + r);
        }
        Some(c)
    }

    /// Radio del menú de ejemplo (el de nodo_ui.dart); el vórtice, 4 veces.
    pub const RADIO: f32 = 22.0;

    /// El menú de ejemplo (catalogo.dart): una estrella con enlaces cruzados
    /// y, en el centro, el vórtice (4 veces más grande).
    pub fn menu() -> (Vec<String>, Topologia) {
        let ids = [
            "inicio",
            "perfil",
            "ajustes",
            "tareas",
            "calendario",
            "colores",
            "notas",
        ];
        let aristas = vec![(0, 1), (0, 2), (0, 3), (0, 4), (0, 5), (0, 6), (1, 2), (3, 4), (4, 6)];
        let mut radios = vec![RADIO; 7];
        radios[0] *= 4.0;
        let topo = Topologia::nueva(aristas, vec![0, 1, 1, 2, 2, 3, 2], radios);
        (ids.iter().map(|s| s.to_string()).collect(), topo)
    }

    pub fn distancia(a: [f32; 2], b: [f32; 2]) -> f32 {
        ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2)).sqrt()
    }

    /// Ningún par de nodos más cerca que la suma de sus radios de choque.
    pub fn sin_solapes(pos: &[[f32; 2]], choque: &[f32]) -> Result<(), String> {
        for i in 0..pos.len() {
            for j in i + 1..pos.len() {
                let minimo = choque[i] + choque[j] - 0.01;
                let d = distancia(pos[i], pos[j]);
                if d < minimo {
                    return Err(format!("{i} y {j} a {d:.1} (mínimo {minimo:.1})"));
                }
            }
        }
        Ok(())
    }

    /// Todos los nodos, con su círculo, dentro de los bordes.
    pub fn dentro(pos: &[[f32; 2]], radios: &[f32], l: Limites) -> Result<(), String> {
        let (x0, y0, x1, y1) = l.caja;
        for (i, (p, r)) in pos.iter().zip(radios).enumerate() {
            let r = r * l.factor_radio - 0.01;
            if p[0] - r < x0 || p[0] + r > x1 || p[1] - r < y0 || p[1] + r > y1 {
                return Err(format!("{i} fuera: {p:?} con radio {r:.1} (bordes {:?})", l.caja));
            }
        }
        Ok(())
    }

    /// Bordes a `margen` de la caja de los nodos (con su círculo).
    pub fn bordes(pos: &[[f32; 2]], radios: &[f32], margen: f32) -> Limites {
        let c = caja(pos, radios).unwrap_or_default();
        Limites {
            caja: (c.0 - margen, c.1 - margen, c.2 + margen, c.3 + margen),
            factor_radio: 1.0,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{fuerzas_menu, fuerzas_para};

    #[test]
    fn el_menu_del_login_conserva_sus_fuerzas() {
        for n in 1..=7 {
            let f = fuerzas_para(n);
            let m = fuerzas_menu();
            assert_eq!(
                (f.repulsion, f.distancia_enlace, f.fuerza_grupo),
                (m.repulsion, m.distancia_enlace, m.fuerza_grupo)
            );
        }
    }

    #[test]
    fn con_mas_nodos_encogen_y_los_grupos_aprietan() {
        let (ocho, cuarenta) = (fuerzas_para(8), fuerzas_para(40));
        assert!(cuarenta.repulsion < ocho.repulsion && ocho.repulsion < fuerzas_menu().repulsion);
        assert!(cuarenta.distancia_enlace < ocho.distancia_enlace);
        assert!(cuarenta.fuerza_grupo > fuerzas_menu().fuerza_grupo);
        assert_eq!(cuarenta.fuerza_enlace, fuerzas_menu().fuerza_enlace);
    }
}
