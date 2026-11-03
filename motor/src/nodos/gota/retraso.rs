//! Lo que el arrastre deja atrás: el líquido va en el marco de su nodo, y solo el borde se queda
//! atrás, sujeto por un muelle.

use super::FORMA;

/// Lo que el arrastre deja atrás, en radios: un punto con inercia, sujeto al nodo por un muelle. Sale
/// liso aunque el ratón llegue a saltos, tiene tope y se mece al parar, como el líquido.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub(super) struct Retraso {
    pub(super) donde: [f32; 2],
    pub(super) velocidad: [f32; 2],
}

impl Retraso {
    /// Tras moverse el nodo `movido` radios en `dt` s.
    pub(super) fn avanzar(&mut self, movido: [f32; 2], dt: f32) {
        for ((d, v), m) in self.donde.iter_mut().zip(&mut self.velocidad).zip(movido) {
            *v -= (FORMA.vuelta * *d + FORMA.calma * *v) * dt;
            *d += *v * dt - m;
        }
        let largo = self.donde[0].hypot(self.donde[1]);
        if largo > FORMA.retraso_max {
            self.donde = self.donde.map(|d| d * FORMA.retraso_max / largo);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::super::prueba::DT;
    use super::*;

    #[test]
    fn el_retraso_tiene_tope_por_rapido_que_vaya() {
        let mut r = Retraso::default();
        for _ in 0..60 {
            r.avanzar([3.0, 4.0], DT);
        }
        assert!((r.donde[0].hypot(r.donde[1]) - FORMA.retraso_max).abs() < 1e-5);
        assert!(r.donde[0] < 0.0 && r.donde[1] < 0.0);
    }

    #[test]
    fn un_arrastre_que_sigue_igual_deja_un_retraso_fijo() {
        let mut r = Retraso::default();
        // 2 radios/s
        for _ in 0..600 {
            r.avanzar([2.0 * DT, 0.0], DT);
        }
        let esperado = -2.0 * FORMA.calma / FORMA.vuelta;
        assert!((r.donde[0] - esperado).abs() < 1e-3, "{}", r.donde[0]);
    }

    #[test]
    fn al_parar_se_mece_y_se_recompone() {
        let mut r = Retraso {
            donde: [-FORMA.retraso_max, 0.0],
            velocidad: [0.0; 2],
        };
        let mut paso_al_otro_lado = false;
        for _ in 0..600 {
            r.avanzar([0.0; 2], DT);
            paso_al_otro_lado |= r.donde[0] > 0.01;
        }
        assert!(paso_al_otro_lado, "se mece");
        assert!(r.donde[0].abs() < 1e-3 && r.velocidad[0].abs() < 1e-3, "y se para");
    }

    #[test]
    fn el_retraso_no_diverge_con_el_paso_mas_largo() {
        let mut r = Retraso::default();
        for i in 0..2000 {
            let signo = if i % 7 < 3 { 1.0 } else { -1.0 };
            r.avanzar([signo, 0.0], FORMA.paso_max);
            assert!(r.donde[0].is_finite() && r.velocidad[0].abs() < 100.0);
        }
    }
}
