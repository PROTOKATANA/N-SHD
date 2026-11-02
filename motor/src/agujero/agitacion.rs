//! Lo agitado que está el disco: cada doble clic suma uno (con tope), y se
//! calma solo, como e^(−t / calma). El fluido lo usa como la fuerza de unos
//! chorros: se revuelve y luego la rotación y la caída lo vuelven a ordenar
//! hacia el agujero.

/// Por debajo, ya está en calma.
const CALMA_DEL_TODO: f64 = 1e-3;

#[derive(Clone, Copy, Debug)]
pub struct Agitacion {
    calma: f64,
    tope: f64,
    nivel: f64,
}

impl Agitacion {
    pub fn nueva(calma: f64, tope: f64) -> Self {
        Agitacion {
            calma,
            tope,
            nivel: 0.0,
        }
    }

    /// Un doble clic más.
    pub fn sumar(&mut self) {
        self.nivel = if self.nivel + 1.0 > self.tope {
            self.tope
        } else {
            self.nivel + 1.0
        };
    }

    /// Pasan `paso` s.
    pub fn avanzar(&mut self, paso: f64) {
        let calma = if self.calma > 0.0 { self.calma } else { 1e-3 };
        self.nivel *= (-paso / calma).exp();
        if self.nivel < CALMA_DEL_TODO {
            self.nivel = 0.0;
        }
    }

    pub fn nivel(&self) -> f64 {
        self.nivel
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cerca(a: f64, b: f64) -> bool {
        (a - b).abs() < 1e-9
    }

    #[test]
    fn cada_doble_clic_agita_mas() {
        let mut a = Agitacion::nueva(2.0, 5.0);
        assert!(cerca(a.nivel(), 0.0), "en calma al empezar");
        a.sumar();
        assert!(cerca(a.nivel(), 1.0), "un doble clic, uno");
        a.sumar();
        a.sumar();
        assert!(cerca(a.nivel(), 3.0), "cada doble clic suma");
        a.avanzar(2.0);
        assert!(cerca(a.nivel(), 3.0 * (-1.0_f64).exp()), "se calma: a 1/e en `calma` s");
        for _ in 0..10 {
            a.sumar();
        }
        assert!(cerca(a.nivel(), 5.0), "con tope");
        for _ in 0..60 {
            a.avanzar(0.5);
        }
        assert!(cerca(a.nivel(), 0.0), "y a los 30 s, en calma del todo");
    }
}
