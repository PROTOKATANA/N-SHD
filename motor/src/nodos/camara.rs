//! El cbuffer «Camara» que comparten las pasadas. El radio que se ve de un nodo es su radio por el
//! factor: los topes de zoom los pone quien integra.

use super::puente::{CamaraGpu, Escena};

/// Ver el puente.
pub fn camara_de(escena: &Escena, ancho: u32, alto: u32) -> CamaraGpu {
    let sx = 2.0 * escena.escala / ancho as f32;
    let sy = 2.0 * escena.escala / alto as f32;
    CamaraGpu {
        transform: [sx, sy, -escena.centro_x * sx, -escena.centro_y * sy],
        caja: [0.0; 4],
        // los nodos no se vuelven a subir por el zoom: el factor va aquí
        extra: [escena.resaltado as f32, escena.factor_radio, escena.vortice as f32, 0.0],
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cerca(a: f32, b: f32) -> bool {
        (a - b).abs() < 1e-4
    }

    fn escena() -> Escena {
        Escena {
            centro_x: 10.0,
            centro_y: -5.0,
            escala: 2.0,
            resaltado: 3,
            factor_radio: 1.0,
            ..Escena::default()
        }
    }

    #[test]
    fn lleva_el_centro_al_centro_de_la_textura() {
        let c = camara_de(&escena(), 1000, 500);
        assert!(cerca(10.0 * c.transform[0] + c.transform[2], 0.0));
        assert!(cerca(-5.0 * c.transform[1] + c.transform[3], 0.0));
        assert!(cerca(c.transform[0], 2.0 * 2.0 / 1000.0));
        assert!(cerca(c.transform[1], 2.0 * 2.0 / 500.0));
        assert!(
            cerca(c.extra[0], 3.0) && cerca(c.extra[2], 0.0),
            "resaltado y sin vórtice"
        );
    }

    #[test]
    fn lleva_el_factor_de_radio_y_el_vortice() {
        let e = Escena {
            factor_radio: 0.5,
            vortice: 4,
            ..escena()
        };
        let z = camara_de(&e, 1000, 500);
        assert!(cerca(z.extra[1], 0.5) && cerca(z.extra[2], 4.0));
    }
}
