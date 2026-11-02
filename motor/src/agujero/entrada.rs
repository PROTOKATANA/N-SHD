//! Lo último que llegó de la interfaz: la vista, el cursor y los dobles
//! clics. Lo escribe quien integra y lo lee el hilo que pinta el
//! agujero en cada fotograma, bajo un candado corto: con él solo se cambian o
//! se copian valores ya hechos. El último valor gana: lo que llega entre dos
//! fotogramas no se acumula.

use std::sync::{Mutex, MutexGuard, PoisonError};

use crate::agujero::puente::{Cursor, Vista};

/// La vista más grande que se pinta, por lado.
const LADO_MAXIMO: u32 = 4096;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Entrada {
    pub vista: Vista,
    pub cursor: Cursor,
    /// Los dobles clics que ha habido.
    pub dobles_clics: u32,
}

/// El almacén de la entrada; hay uno por proceso, [`ENTRADA`].
pub struct AlmacenEntrada(Mutex<Entrada>);

impl AlmacenEntrada {
    pub const fn nuevo() -> Self {
        AlmacenEntrada(Mutex::new(Entrada {
            vista: Vista { ancho: 0, alto: 0 },
            cursor: Cursor {
                x: 0.0,
                y: 0.0,
                pulsado: false,
                dentro: false,
                rodar: false,
            },
            dobles_clics: 0,
        }))
    }

    /// Nada de lo que se hace con el candado deja los datos a medias: si otro
    /// hilo lo envenenase, siguen siendo válidos.
    fn entrada(&self) -> MutexGuard<'_, Entrada> {
        self.0.lock().unwrap_or_else(PoisonError::into_inner)
    }

    /// El tamaño de la vista en píxeles físicos. Fuera de medida, sin vista.
    pub fn vista(&self, ancho: u32, alto: u32) {
        let cabe = |lado| (1..=LADO_MAXIMO).contains(&lado);
        self.entrada().vista = Vista {
            ancho: if cabe(ancho) { ancho } else { 0 },
            alto: if cabe(alto) { alto } else { 0 },
        };
    }

    /// Un cursor que no es un número se descarta: la órbita lo acumularía
    /// para siempre.
    pub fn cursor(&self, c: Cursor) {
        if c.x.is_finite() && c.y.is_finite() {
            self.entrada().cursor = c;
        }
    }

    pub fn doble_clic(&self) {
        let mut e = self.entrada();
        e.dobles_clics = e.dobles_clics.wrapping_add(1);
    }

    pub fn leer(&self) -> Entrada {
        *self.entrada()
    }
}

/// La entrada de la app: la que escribe quien integra y lee el hilo que pinta.
static ENTRADA: AlmacenEntrada = AlmacenEntrada::nuevo();

/// Ver [`AlmacenEntrada::vista`].
pub fn vista(ancho: u32, alto: u32) {
    ENTRADA.vista(ancho, alto);
}

/// Ver [`AlmacenEntrada::cursor`].
pub fn cursor(c: Cursor) {
    ENTRADA.cursor(c);
}

/// Ver [`AlmacenEntrada::doble_clic`].
pub fn doble_clic() {
    ENTRADA.doble_clic();
}

/// La entrada de ahora.
pub fn leer() -> Entrada {
    ENTRADA.leer()
}

/// La vista que pidió la interfaz: lo que lee la GPU por el puente.
pub fn vista_pedida() -> Vista {
    ENTRADA.leer().vista
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cursor_en(x: f32) -> Cursor {
        Cursor {
            x,
            y: 2.0,
            pulsado: true,
            dentro: true,
            rodar: false,
        }
    }

    #[test]
    fn el_ultimo_valor_gana() {
        let a = AlmacenEntrada::nuevo();
        a.vista(800, 600);
        a.vista(1024, 768);
        a.cursor(cursor_en(1.0));
        a.cursor(cursor_en(3.0));
        let e = a.leer();
        assert_eq!(e.vista, Vista { ancho: 1024, alto: 768 });
        assert_eq!(e.cursor, cursor_en(3.0));
    }

    #[test]
    fn la_vista_fuera_de_medida_es_ninguna() {
        let a = AlmacenEntrada::nuevo();
        a.vista(0, 600);
        assert_eq!(a.leer().vista, Vista { ancho: 0, alto: 600 });
        a.vista(4096, 4097);
        assert_eq!(a.leer().vista, Vista { ancho: 4096, alto: 0 });
    }

    #[test]
    fn un_cursor_que_no_es_un_numero_se_descarta() {
        let a = AlmacenEntrada::nuevo();
        a.cursor(cursor_en(1.0));
        a.cursor(cursor_en(f32::NAN));
        a.cursor(cursor_en(f32::INFINITY));
        assert_eq!(a.leer().cursor, cursor_en(1.0));
    }

    #[test]
    fn cada_doble_clic_cuenta() {
        let a = AlmacenEntrada::nuevo();
        a.doble_clic();
        a.doble_clic();
        assert_eq!(a.leer().dobles_clics, 2);
    }
}
