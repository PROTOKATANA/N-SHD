//! Los ensayos sin ratón, para los bancos y los volcados: un arrastre de
//! mentira, dobles clics de mentira y los segundos en que volcar la textura.

use crate::agujero::puente::Cursor;

/// De dónde sale un guion: cada uno, el texto de su variable de entorno.
#[derive(Clone, Copy, Debug, Default)]
pub struct DescGuion<'a> {
    /// `AGUJERO_ARRASTRE=<vx>,<vy>[,1]`: de los 3 a los 4 s, un cursor arrastra
    /// la cámara desde (400, 300) a esa velocidad (px/s) y luego la suelta;
    /// con `,1`, con el botón derecho (rodar).
    pub arrastre: Option<&'a str>,
    /// `AGUJERO_DOBLE_CLIC=3,3.4`: un doble clic en cada uno de esos segundos.
    pub dobles_clics: Option<&'a str>,
    /// `AGUJERO_VOLCAR_TIEMPOS=1,4.5,9`: los segundos en que volcar.
    pub volcar_en: Option<&'a str>,
}

/// Un número de un texto del entorno, si lo es de verdad.
fn numero(texto: &str) -> Option<f64> {
    texto.parse().ok().filter(|v: &f64| v.is_finite())
}

/// Unos segundos separados por comas, en orden: los que no crecen se
/// descartan, y se para en el primero que no es un número.
pub fn segundos_de(texto: Option<&str>) -> Vec<f64> {
    let mut segundos: Vec<f64> = Vec::new();
    for t in texto.into_iter().flat_map(|t| t.split(',')).map_while(numero) {
        if segundos.last().is_none_or(|&ultimo| t > ultimo) {
            segundos.push(t);
        }
    }
    segundos
}

#[derive(Clone, Debug, Default)]
pub struct Guion {
    vx: f64,
    vy: f64,
    rodar: bool,
    clics: Vec<f64>,
    volcar_en: Vec<f64>,
    volcados: usize,
}

impl Guion {
    pub fn nuevo(d: DescGuion<'_>) -> Self {
        let mut guion = Guion {
            clics: segundos_de(d.dobles_clics),
            volcar_en: segundos_de(d.volcar_en),
            ..Guion::default()
        };
        // el arrastre son dos números, no segundos: no tienen por qué crecer
        let mut partes = d.arrastre.into_iter().flat_map(|a| a.split(','));
        if let Some(vx) = partes.next().and_then(numero) {
            guion.vx = vx;
            if let Some(vy) = partes.next().and_then(numero) {
                guion.vy = vy;
                guion.rodar = partes.next().and_then(numero).is_some_and(|boton| boton > 0.5);
            }
        }
        guion
    }

    /// El guion que piden las variables de entorno.
    pub fn del_entorno() -> Self {
        let arrastre = std::env::var("AGUJERO_ARRASTRE").ok();
        let dobles_clics = std::env::var("AGUJERO_DOBLE_CLIC").ok();
        let volcar_en = std::env::var("AGUJERO_VOLCAR_TIEMPOS").ok();
        Guion::nuevo(DescGuion {
            arrastre: arrastre.as_deref(),
            dobles_clics: dobles_clics.as_deref(),
            volcar_en: volcar_en.as_deref(),
        })
    }

    /// El cursor de mentira en el segundo `t`, si lo hay.
    pub fn cursor(&self, t: f64) -> Option<Cursor> {
        if self.vx == 0.0 && self.vy == 0.0 {
            return None;
        }
        let desde = t - 3.0;
        if desde < 0.0 {
            return None;
        }
        let hasta = desde.min(1.0);
        let pulsado = desde <= 1.0;
        Some(Cursor {
            x: (400.0 + self.vx * hasta) as f32,
            y: (300.0 + self.vy * hasta) as f32,
            pulsado: pulsado && !self.rodar,
            dentro: true,
            rodar: pulsado && self.rodar,
        })
    }

    /// Los dobles clics de mentira hasta el segundo `t` (incluido).
    pub fn dobles_clics(&self, t: f64) -> usize {
        self.clics.partition_point(|&clic| clic <= t)
    }

    /// Sin segundos de volcado: cada fotograma (sobre el mismo archivo).
    pub fn volcar_siempre(&self) -> bool {
        self.volcar_en.is_empty()
    }

    /// Queda algún volcado por hacer.
    pub fn volcados_pendientes(&self) -> bool {
        self.volcados < self.volcar_en.len()
    }

    /// Los segundos de volcado que han llegado hasta `t` y aún no se volcaron
    /// (cada uno, una vez).
    pub fn volcados_hasta(&mut self, t: f64) -> Vec<f64> {
        let hasta = self.volcar_en.partition_point(|&segundo| segundo <= t);
        let desde = self.volcados.min(hasta);
        self.volcados = hasta.max(self.volcados);
        self.volcar_en[desde..hasta].to_vec()
    }
}

/// El archivo de un volcado (`t03500.ppm`).
pub fn archivo_de(segundo: f64) -> String {
    format!("t{:05}.ppm", (segundo * 1000.0 + 0.5) as i64)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cerca(a: f32, b: f32) -> bool {
        (a - b).abs() < 1e-3
    }

    #[test]
    fn los_segundos_de_un_texto() {
        assert_eq!(segundos_de(Some("3,3.3,3.6")).len(), 3, "tres segundos");
        assert_eq!(segundos_de(Some("2,1,4")), vec![2.0, 4.0], "los que no crecen, fuera");
        assert!(segundos_de(None).is_empty(), "sin texto, nada");
        assert!(segundos_de(Some("x")).is_empty(), "sin números, nada");
        assert_eq!(
            segundos_de(Some("1,x,3")),
            vec![1.0],
            "se para en el primero que no es un número"
        );
        assert_eq!(segundos_de(Some("1,inf,3")), vec![1.0], "un infinito no es un segundo");
    }

    #[test]
    fn el_guion_de_prueba() {
        let mut g = Guion::nuevo(DescGuion {
            arrastre: Some("150,200"),
            dobles_clics: Some("3,3.3,3.6"),
            volcar_en: Some("1,4.5"),
        });
        assert_eq!(g.dobles_clics(2.9), 0);
        assert_eq!(g.dobles_clics(3.3), 2);
        assert_eq!(g.dobles_clics(9.0), 3);
        assert_eq!(g.cursor(2.9), None, "antes de los 3 s, sin cursor");
        assert!(
            g.cursor(3.5)
                .is_some_and(|c| cerca(c.x, 475.0) && cerca(c.y, 400.0) && c.pulsado),
            "a los 3,5 s, arrastrando"
        );
        assert!(
            g.cursor(4.5)
                .is_some_and(|c| cerca(c.x, 550.0) && cerca(c.y, 500.0) && !c.pulsado),
            "a los 4,5 s, suelto"
        );
        assert!(g.volcados_hasta(0.5).is_empty());
        assert_eq!(g.volcados_hasta(5.0).len(), 2, "los dos juntos");
        assert!(g.volcados_hasta(9.0).is_empty(), "cada volcado, una vez");
        assert_eq!(archivo_de(3.5), "t03500.ppm");
    }

    #[test]
    fn sin_arrastre_no_hay_cursor() {
        assert_eq!(Guion::default().cursor(3.5), None);
    }

    #[test]
    fn con_el_boton_derecho_rueda() {
        let g = Guion::nuevo(DescGuion {
            arrastre: Some("150,200,1"),
            ..DescGuion::default()
        });
        assert!(g.cursor(3.5).is_some_and(|c| c.rodar && !c.pulsado));
    }

    #[test]
    fn sin_segundos_vuelca_siempre() {
        assert!(Guion::default().volcar_siempre());
        assert!(
            !Guion::nuevo(DescGuion {
                volcar_en: Some("1"),
                ..DescGuion::default()
            })
            .volcar_siempre()
        );
    }
}
