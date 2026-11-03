//! Con qué choca el líquido y con qué fuerza: los nodos más cercanos, los bordes de la vista y lo que
//! entra en el círculo del vórtice, recordado de un fotograma al siguiente.

use super::super::liquido::FORMA as LIQUIDO;
use super::super::puente::{Escena, GotaGpu, Nodo};
use super::{FORMA, MARGEN};

/// Obstáculos que ve el líquido a la vez: los más cercanos.
pub const MAX_OBSTACULOS: usize = 8;

/// Empujones que recibe el líquido a la vez: los más fuertes.
pub const MAX_CONTACTOS: usize = 4;

/// Bordes de la vista que no existen: tan lejos que nada llega.
const SIN_BORDE: f32 = 1e3;

/// radios: lo más que barre un nodo en un fotograma. Si salta más, se recoloca, no empuja.
const BARRIDO_MAX: f32 = 1.5;

/// Lo que se recuerda de un fotograma al siguiente: cuánto se hundía cada cosa en el círculo del
/// nodo, para saber a qué velocidad choca.
#[derive(Clone, Debug, Default)]
pub struct Contactos {
    nodos: Vec<f32>,
    bordes: [f32; 4],
    /// Dónde estaba cada nodo (en mundo), para saber lo que barrió; vacío al empezar.
    antes: Vec<[f32; 2]>,
}

/// Un empujón al líquido: de dónde viene (x, y, unitario) y su fuerza.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
struct Empujon {
    x: f32,
    y: f32,
    fuerza: f32,
}

/// Se queda con los `N` que tengan la `clave` más alta, en orden.
fn quedarse<T: Copy, const N: usize>(lista: &mut [(f32, T); N], clave: f32, valor: T) {
    let Some(sitio) = lista.iter().position(|(c, _)| clave > *c) else {
        return;
    };
    lista.copy_within(sitio..N - 1, sitio + 1);
    lista[sitio] = (clave, valor);
}

impl Contactos {
    /// La velocidad a la que entra algo que ahora se hunde `hundido`; 0 si no entra.
    fn entrada(antes: &mut f32, hundido: f32, dt: f32) -> f32 {
        let velocidad = if dt > 0.0 && hundido > 0.0 {
            (hundido - antes.max(0.0)) / dt
        } else {
            0.0
        };
        *antes = hundido;
        velocidad.max(0.0)
    }

    /// Rellena los obstáculos, los bordes y los empujones para el vórtice `vortice`, un índice de
    /// `nodos`, tras `dt` s.
    pub fn rellenar(&mut self, escena: &Escena, nodos: &[Nodo], vortice: usize, dt: f32, g: &mut GotaGpu) {
        let factor = if escena.factor_radio > 0.0 {
            escena.factor_radio
        } else {
            1.0
        };
        let c = nodos[vortice];
        let radio = c.radio * factor;
        // el líquido se estira: lo que esté a menos de esto puede tocarlo
        let cerca = 1.0;
        let mut obstaculos = [(f32::MIN, ([0.0_f32; 4], [0.0_f32; 2])); MAX_OBSTACULOS];
        let mut fuertes = [(0.0_f32, Empujon::default()); MAX_CONTACTOS];
        self.nodos.resize(nodos.len(), 0.0);
        let hay_antes = self.antes.len() == nodos.len();
        let bien = radio > 0.0 && radio.is_finite() && c.x.is_finite() && c.y.is_finite();
        if bien {
            for (i, n) in nodos.iter().enumerate() {
                if i == vortice || !n.x.is_finite() || !n.y.is_finite() || !n.radio.is_finite() {
                    continue;
                }
                let (dx, dy) = ((n.x - c.x) / radio, (n.y - c.y) / radio);
                let r = n.radio * factor / radio;
                let distancia = dx.hypot(dy);
                let hueco = distancia - r - 1.0;
                if hueco < cerca {
                    // lo que se movió respecto al vórtice: lo que barre por el líquido
                    let mut barre = [0.0, 0.0];
                    if hay_antes {
                        let (a, v) = (self.antes[i], self.antes[vortice]);
                        barre = [
                            ((n.x - a[0]) - (c.x - v[0])) / radio,
                            ((n.y - a[1]) - (c.y - v[1])) / radio,
                        ];
                        let largo = barre[0].hypot(barre[1]);
                        if largo.is_nan() || largo > BARRIDO_MAX {
                            barre = [0.0, 0.0];
                        }
                    }
                    // el radio con que choca, y el de su gota en el líquido de los enlaces
                    quedarse(&mut obstaculos, -hueco, ([dx, dy, r + MARGEN, r * LIQUIDO.gota], barre));
                }
                let velocidad = Self::entrada(&mut self.nodos[i], -hueco, dt);
                if velocidad > 0.0 && distancia > 0.0 {
                    let empujon = Empujon {
                        x: dx / distancia,
                        y: dy / distancia,
                        fuerza: (velocidad * FORMA.choque).min(FORMA.choque_max),
                    };
                    quedarse(&mut fuertes, empujon.fuerza, empujon);
                }
            }
        }
        // los bordes de la vista, en el orden x0, y0, x1, y1
        let mut bordes = [-SIN_BORDE, -SIN_BORDE, SIN_BORDE, SIN_BORDE];
        if bien && escena.ancho > 0 && escena.alto > 0 && escena.escala > 0.0 {
            let (medio_x, medio_y) = (
                escena.ancho as f32 * 0.5 / escena.escala,
                escena.alto as f32 * 0.5 / escena.escala,
            );
            bordes = [
                (escena.centro_x - medio_x - c.x) / radio,
                (escena.centro_y - medio_y - c.y) / radio,
                (escena.centro_x + medio_x - c.x) / radio,
                (escena.centro_y + medio_y - c.y) / radio,
            ];
            let direcciones = [(-1.0, 0.0), (0.0, -1.0), (1.0, 0.0), (0.0, 1.0)];
            for (k, (x, y)) in direcciones.into_iter().enumerate() {
                let velocidad = Self::entrada(&mut self.bordes[k], 1.0 - bordes[k].abs(), dt);
                if velocidad > 0.0 {
                    let fuerza = (velocidad * FORMA.choque).min(FORMA.choque_max);
                    quedarse(&mut fuertes, fuerza, Empujon { x, y, fuerza });
                }
            }
        }
        g.bordes = bordes;
        let cuantos = obstaculos.iter().filter(|(c, _)| *c > f32::MIN).count();
        g.inercia[3] = cuantos as f32;
        g.barrido = [[0.0; 4]; MAX_OBSTACULOS / 2];
        for (k, (_, (o, barre))) in obstaculos.into_iter().enumerate() {
            g.obstaculos[k] = o;
            g.barrido[k / 2][(k % 2) * 2] = barre[0];
            g.barrido[k / 2][(k % 2) * 2 + 1] = barre[1];
        }
        self.antes = nodos.iter().map(|n| [n.x, n.y]).collect();
        for (destino, (_, e)) in g.contactos.iter_mut().zip(fuertes) {
            *destino = [e.x, e.y, e.fuerza, 0.0];
        }
    }
}

#[cfg(test)]
mod tests {
    use super::super::prueba::{DT, escena, nodo};
    use super::*;

    fn rellenar(c: &mut Contactos, e: &Escena, nodos: &[Nodo]) -> GotaGpu {
        let mut g = GotaGpu::default();
        c.rellenar(e, nodos, 0, DT, &mut g);
        g
    }

    #[test]
    fn un_nodo_lejos_no_es_obstaculo() {
        let g = rellenar(
            &mut Contactos::default(),
            &escena(),
            &[nodo(0.0, 0.0, 10.0), nodo(100.0, 0.0, 10.0)],
        );
        assert_eq!(g.inercia[3], 0.0);
    }

    #[test]
    fn un_nodo_cerca_va_relativo_y_en_radios() {
        let nodos = [nodo(10.0, 20.0, 10.0), nodo(25.0, 20.0, 5.0)];
        let g = rellenar(&mut Contactos::default(), &escena(), &nodos);
        assert_eq!(g.inercia[3], 1.0);
        assert_eq!(g.obstaculos[0], [1.5, 0.0, 0.5 + MARGEN, 0.5 * LIQUIDO.gota]);
    }

    #[test]
    fn sin_vista_no_hay_bordes_y_con_vista_si() {
        let nodos = [nodo(100.0, 50.0, 10.0), nodo(500.0, 0.0, 10.0)];
        let g = rellenar(&mut Contactos::default(), &escena(), &nodos);
        assert_eq!(g.bordes[2], SIN_BORDE);
        let e = Escena {
            ancho: 400,
            alto: 200,
            ..escena()
        };
        let g = rellenar(&mut Contactos::default(), &e, &nodos);
        // la vista va de -200 a 200 y de -100 a 100, y el vórtice mide 10
        assert_eq!(g.bordes, [-30.0, -15.0, 10.0, 5.0]);
    }

    #[test]
    fn se_quedan_los_obstaculos_mas_cercanos() {
        let mut nodos = vec![nodo(0.0, 0.0, 10.0)];
        for i in 0..12 {
            nodos.push(nodo(11.0 + i as f32 * 0.1, 0.0, 1.0));
        }
        let g = rellenar(&mut Contactos::default(), &escena(), &nodos);
        assert_eq!(g.inercia[3], MAX_OBSTACULOS as f32);
        assert!(g.obstaculos[0][0] < g.obstaculos[7][0], "el primero, el más cerca");
        assert_eq!(g.obstaculos[0][0], 1.1);
    }

    #[test]
    fn solo_empuja_lo_que_entra() {
        let mut c = Contactos::default();
        let e = escena();
        let nada = rellenar(&mut c, &e, &[nodo(0.0, 0.0, 10.0), nodo(21.0, 0.0, 10.0)]);
        assert_eq!(nada.contactos[0][2], 0.0, "aún no toca");

        let entra = rellenar(&mut c, &e, &[nodo(0.0, 0.0, 10.0), nodo(19.0, 0.0, 10.0)]);
        assert_eq!((entra.contactos[0][0], entra.contactos[0][1]), (1.0, 0.0));
        assert!(entra.contactos[0][2] > 0.0);

        let quieto = rellenar(&mut c, &e, &[nodo(0.0, 0.0, 10.0), nodo(19.0, 0.0, 10.0)]);
        assert_eq!(quieto.contactos[0][2], 0.0, "quieto, aunque siga dentro");

        let sale = rellenar(&mut c, &e, &[nodo(0.0, 0.0, 10.0), nodo(19.5, 0.0, 10.0)]);
        assert_eq!(sale.contactos[0][2], 0.0, "saliendo");
    }

    #[test]
    fn el_choque_tiene_tope() {
        let g = rellenar(
            &mut Contactos::default(),
            &escena(),
            &[nodo(0.0, 0.0, 10.0), nodo(5.0, 0.0, 10.0)],
        );
        assert_eq!(g.contactos[0][2], FORMA.choque_max);
    }

    #[test]
    fn un_borde_tambien_choca() {
        let mut c = Contactos::default();
        let e = Escena {
            ancho: 100,
            alto: 100,
            ..escena()
        };
        rellenar(&mut c, &e, &[nodo(0.0, 0.0, 10.0), nodo(0.0, -30.0, 1.0)]);
        let a = rellenar(&mut c, &e, &[nodo(45.0, 0.0, 10.0), nodo(0.0, -30.0, 1.0)]);
        assert_eq!((a.contactos[0][0], a.contactos[0][1]), (1.0, 0.0), "el de la derecha");
        assert!(a.contactos[0][2] > 0.0);
    }

    #[test]
    fn lo_que_no_es_finito_no_llega() {
        let nodos = [
            nodo(0.0, 0.0, 10.0),
            nodo(f32::NAN, 0.0, 10.0),
            nodo(0.0, f32::INFINITY, 10.0),
        ];
        let g = rellenar(&mut Contactos::default(), &escena(), &nodos);
        assert_eq!(g.inercia[3], 0.0);
        assert_eq!(g.contactos[0][2], 0.0);
    }

    #[test]
    fn lo_que_barre_un_nodo_va_respecto_al_vortice_y_un_salto_no_barre() {
        let mut c = Contactos::default();
        let e = escena();
        rellenar(&mut c, &e, &[nodo(0.0, 0.0, 10.0), nodo(15.0, 0.0, 5.0)]);
        // el vórtice va 2 a la izquierda y el nodo 3 a la derecha: barre 5, medio radio
        let g = rellenar(&mut c, &e, &[nodo(-2.0, 0.0, 10.0), nodo(18.0, 0.0, 5.0)]);
        assert_eq!((g.barrido[0][0], g.barrido[0][1]), (0.5, 0.0));
        let g = rellenar(&mut c, &e, &[nodo(-2.0, 0.0, 10.0), nodo(-2.0, 5.0, 5.0)]);
        assert_eq!(
            (g.barrido[0][0], g.barrido[0][1]),
            (0.0, 0.0),
            "saltó dos radios: se recoloca"
        );
    }
}
