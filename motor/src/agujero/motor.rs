//! El estado de un agujero, avanzado una vez por fotograma desde el hilo que pinta:
//! la órbita, lo agitado y el guion de ensayo, con la entrada que dejó la
//! interfaz. Cada [`Motor`] es de C++, que lo crea con [`motor_nuevo`].

use crate::agujero::agitacion::Agitacion;
use crate::agujero::entrada::{self, Entrada};
use crate::agujero::guion::{self, Guion};
use crate::agujero::orbita::Orbita;
use crate::agujero::puente::{DescMotor, Fotograma};

pub struct Motor {
    orbita: Orbita,
    agitacion: Agitacion,
    guion: Guion,
    dobles_clics: u32,
}

impl Motor {
    pub fn nuevo(d: &DescMotor, guion: Guion) -> Self {
        Motor {
            orbita: Orbita::nueva(d.orbita),
            agitacion: Agitacion::nueva(d.calma, d.tope),
            guion,
            dobles_clics: 0,
        }
    }

    /// Ver el puente.
    pub fn fotograma(&self) -> Fotograma {
        Fotograma {
            base_trazado: *self.orbita.base_trazado(),
            azimut: self.orbita.azimut(),
            moviendose: self.orbita.moviendose(),
            agitado: self.agitacion.nivel(),
            dobles_clics: self.dobles_clics,
        }
    }

    /// Ver el puente.
    pub fn avanzar(&mut self, antes: f64, paso: f64) -> Fotograma {
        self.avanzar_con(entrada::leer(), antes, paso)
    }

    /// Pasan `paso` s con esta entrada, con lo que el guion ponga encima.
    pub fn avanzar_con(&mut self, e: Entrada, antes: f64, paso: f64) -> Fotograma {
        let clics = e
            .dobles_clics
            .wrapping_add(u32::try_from(self.guion.dobles_clics(antes)).unwrap_or(u32::MAX));
        while self.dobles_clics != clics {
            self.agitacion.sumar();
            self.dobles_clics = self.dobles_clics.wrapping_add(1);
        }
        let cursor = self.guion.cursor(antes).unwrap_or(e.cursor);
        self.orbita.avanzar(cursor, paso);
        self.agitacion.avanzar(paso);
        self.fotograma()
    }

    /// Ver el puente.
    pub fn volcar_siempre(&self) -> bool {
        self.guion.volcar_siempre()
    }

    /// Ver el puente.
    pub fn volcados_pendientes(&self) -> bool {
        self.guion.volcados_pendientes()
    }

    /// Ver el puente.
    pub fn volcados_hasta(&mut self, t: f64) -> Vec<String> {
        let segundos = self.guion.volcados_hasta(t);
        segundos.into_iter().map(guion::archivo_de).collect()
    }
}

/// Un motor con el guion que pidan las variables de entorno.
pub fn motor_nuevo(d: &DescMotor) -> Box<Motor> {
    Box::new(Motor::nuevo(d, Guion::del_entorno()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::agujero::guion::DescGuion;
    use crate::agujero::puente::{Cursor, DescOrbita};

    const DT: f64 = 1.0 / 60.0;

    fn descripcion() -> DescMotor {
        DescMotor {
            orbita: DescOrbita {
                sensibilidad: 0.01,
                frenado: 2.0,
                ..DescOrbita::default()
            },
            calma: 2.0,
            tope: 5.0,
        }
    }

    fn entrada_con_clics(dobles_clics: u32) -> Entrada {
        Entrada {
            dobles_clics,
            ..Entrada::default()
        }
    }

    #[test]
    fn cada_doble_clic_nuevo_agita_una_vez() {
        let mut m = Motor::nuevo(&descripcion(), Guion::default());
        assert_eq!(m.avanzar_con(entrada_con_clics(2), 0.0, 0.0).agitado, 2.0);
        let f = m.avanzar_con(entrada_con_clics(2), 0.0, 0.0);
        assert_eq!(
            (f.agitado, f.dobles_clics),
            (2.0, 2),
            "los ya vistos no cuentan otra vez"
        );
    }

    #[test]
    fn los_dobles_clics_del_guion_se_suman_a_los_de_verdad() {
        let guion = Guion::nuevo(DescGuion {
            dobles_clics: Some("3,3.3"),
            ..DescGuion::default()
        });
        let mut m = Motor::nuevo(&descripcion(), guion);
        assert_eq!(m.avanzar_con(entrada_con_clics(1), 2.9, 0.0).dobles_clics, 1);
        assert_eq!(m.avanzar_con(entrada_con_clics(1), 3.3, 0.0).dobles_clics, 3);
    }

    #[test]
    fn el_cursor_del_guion_manda_sobre_el_de_verdad() {
        let guion = Guion::nuevo(DescGuion {
            arrastre: Some("100,0"),
            ..DescGuion::default()
        });
        let quieto = Entrada {
            cursor: Cursor {
                x: 5.0,
                y: 5.0,
                pulsado: false,
                dentro: true,
                rodar: false,
            },
            ..Entrada::default()
        };
        let mut m = Motor::nuevo(&descripcion(), guion);
        // antes de los 3 s no hay arrastre: el cursor quieto no mueve nada
        let antes = m.avanzar_con(quieto, 2.9, DT).base_trazado;
        m.avanzar_con(quieto, 3.0, DT);
        let f = m.avanzar_con(quieto, 3.5, DT);
        assert_ne!(f.base_trazado, antes, "el arrastre de mentira gira la cámara");
    }

    #[test]
    fn el_fotograma_no_avanza_nada() {
        let mut m = Motor::nuevo(&descripcion(), Guion::default());
        m.avanzar_con(entrada_con_clics(1), 0.0, DT);
        assert_eq!(m.fotograma(), m.fotograma());
    }

    #[test]
    fn los_volcados_salen_con_su_archivo() {
        let guion = Guion::nuevo(DescGuion {
            volcar_en: Some("1,4.5"),
            ..DescGuion::default()
        });
        let mut m = Motor::nuevo(&descripcion(), guion);
        assert!(!m.volcar_siempre());
        assert_eq!(m.volcados_hasta(2.0), vec!["t01000.ppm"]);
        assert_eq!(m.volcados_hasta(5.0), vec!["t04500.ppm"]);
        assert!(m.volcados_hasta(9.0).is_empty());
    }
}
