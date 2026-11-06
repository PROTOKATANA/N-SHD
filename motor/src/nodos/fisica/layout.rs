//! Layout dirigido por fuerzas estilo d3-force: repulsión many-body
//! aproximada con Barnes-Hut (quadtree, θ), muelles por arista, gravedad al
//! centro y enfriamiento por alpha. Pensado para decenas de miles de nodos.

use super::Limites;

#[derive(Clone, Copy)]
pub struct Fuerzas {
    pub repulsion: f32,
    pub distancia_enlace: f32,
    pub fuerza_enlace: f32,
    pub fuerza_centro: f32,
    /// Atracción de cada nodo hacia el centroide de su tema: forma los
    /// cúmulos temáticos. 0.0 = agrupación desactivada.
    pub fuerza_grupo: f32,
}

impl Default for Fuerzas {
    fn default() -> Self {
        Self {
            repulsion: 120.0,
            distancia_enlace: 40.0,
            fuerza_enlace: 0.4,
            fuerza_centro: 0.02,
            fuerza_grupo: 0.06,
        }
    }
}

pub struct Layout {
    pub pos: Vec<[f32; 2]>,
    vel: Vec<[f32; 2]>,
    /// Nodos clavados (como fx/fy en d3): ninguna fuerza los mueve, pero ellos
    /// sí empujan y tiran de los demás.
    pub fijos: Vec<Option<[f32; 2]>>,
    /// Cuánto responde cada nodo a las fuerzas, en [0, 1]: al arrastrar uno,
    /// los demás apenas se mueven (ver `enfocar`).
    movilidad: Vec<f32>,
    /// Radio de choque de cada nodo: dos nodos nunca quedan más cerca que la
    /// suma de los suyos (ver `separar`). 0 = sin choque.
    pub choque: Vec<f32>,
    /// Radio del círculo de cada nodo: el que cuenta contra los bordes.
    pub radios: Vec<f32>,
    /// Los bordes de la vista (ver `limitar`); None = sin bordes.
    limites: Option<Limites>,
    /// Nodo que se arrastra con el ratón: los choques no lo mueven (va con el
    /// cursor); se apartan los demás.
    pub arrastrado: Option<usize>,
    pub alpha: f32,
    pub fuerzas: Fuerzas,
    /// Contador de ticks: sal del jitter del quadtree.
    ticks: u64,
}

const ALPHA_MIN: f32 = 0.003;
const ALPHA_DECAY: f32 = 0.0228;
const VEL_DECAY: f32 = 0.6;
// θ²=0.49 (θ=0.7, el de d3): con θ alto, los saltos de la aproximación en las
// fronteras de los cuadrantes dejan vacíos en cruz («flor de 4 puntas»).
const THETA2: f32 = 0.49;

impl Layout {
    /// Posiciones iniciales en espiral de girasol: determinista y sin
    /// solapamientos masivos en el arranque.
    pub fn nuevo(n: usize, fuerzas: Fuerzas) -> Self {
        let mut pos = Vec::with_capacity(n);
        let paso = fuerzas.distancia_enlace * 0.9;
        for i in 0..n {
            let r = paso * (i as f32).sqrt();
            let a = (i as f32) * 2.399_963; // ángulo áureo
            pos.push([r * a.cos(), r * a.sin()]);
        }
        Self {
            pos,
            vel: vec![[0.0, 0.0]; n],
            fijos: vec![None; n],
            movilidad: vec![1.0; n],
            choque: vec![0.0; n],
            radios: vec![0.0; n],
            limites: None,
            arrastrado: None,
            alpha: 1.0,
            fuerzas,
            ticks: 0,
        }
    }

    pub fn recalentar(&mut self, alpha: f32) {
        self.alpha = self.alpha.max(alpha);
    }

    /// Clava (o suelta, con None) un nodo; clavado, se coloca ya en su sitio,
    /// dentro de los bordes.
    pub fn fijar(&mut self, i: usize, p: Option<[f32; 2]>) {
        let p = p.map(|p| self.dentro(i, p));
        if let Some(f) = self.fijos.get_mut(i) {
            *f = p;
            if let Some(p) = p {
                self.pos[i] = p;
                self.vel[i] = [0.0, 0.0];
            }
        }
    }

    /// Con `Some(i)`, solo el nodo i responde del todo a las fuerzas y los
    /// demás con la fracción `resto`; con None, todos por igual.
    pub fn enfocar(&mut self, foco: Option<usize>, resto: f32) {
        for (i, m) in self.movilidad.iter_mut().enumerate() {
            *m = if foco.is_none_or(|f| f == i) { 1.0 } else { resto };
        }
    }

    pub fn activo(&self) -> bool {
        self.alpha > ALPHA_MIN
    }

    /// Pone los bordes de la vista y mete dentro los nodos que se salen.
    /// Devuelve si movió alguno. Unos bordes que no son finitos no se ponen.
    pub fn limitar(&mut self, limites: Limites) -> bool {
        let (x0, y0, x1, y1) = limites.caja;
        if ![x0, y0, x1, y1, limites.factor_radio].iter().all(|v| v.is_finite()) {
            return false;
        }
        self.limites = Some(limites);
        let mut acotados = vec![false; self.pos.len()];
        self.acotar(&mut acotados)
    }

    /// `p` para el nodo `i`, con su círculo dentro de los bordes; en un eje en
    /// que no cabe, en medio.
    fn dentro(&self, i: usize, p: [f32; 2]) -> [f32; 2] {
        let Some(l) = self.limites else { return p };
        let r = self.radios.get(i).copied().unwrap_or(0.0) * l.factor_radio;
        let (x0, y0, x1, y1) = l.caja;
        [acotar_eje(p[0], x0 + r, x1 - r), acotar_eje(p[1], y0 + r, y1 - r)]
    }

    /// Mete dentro de los bordes los nodos que se salen, clavados y arrastrado
    /// incluidos (el clavado queda clavado en el borde), y marca en
    /// `acotados` los que movió. Devuelve si movió alguno más que la holgura.
    fn acotar(&mut self, acotados: &mut [bool]) -> bool {
        if self.limites.is_none() {
            return false;
        }
        let mut movido = false;
        for i in 0..self.pos.len() {
            let p = self.dentro(i, self.pos[i]);
            if p == self.pos[i] {
                continue;
            }
            for ((nuevo, viejo), v) in p.iter().zip(self.pos[i]).zip(&mut self.vel[i]) {
                if *nuevo != viejo {
                    // contra el borde no se sigue empujando
                    *v = 0.0;
                    if (nuevo - viejo).abs() > HOLGURA_BORDE {
                        movido = true;
                        if let Some(a) = acotados.get_mut(i) {
                            *a = true;
                        }
                    }
                }
            }
            self.pos[i] = p;
            if let Some(f) = self.fijos.get_mut(i).and_then(Option::as_mut) {
                *f = p;
            }
        }
        movido
    }

    /// Un paso de simulación. Devuelve false si ya está frío.
    /// `pesos` es el peso por arista (1/min(grados), estilo d3): sin él los
    /// hubs con miles de enlaces reciben fuerzas que desbordan los float.
    /// `grupos` asigna cada nodo a su tema (índices densos < n_grupos) para
    /// la fuerza de agrupación; con fuerza_grupo = 0 no se usa.
    /// Un paso. `largos`: largo en reposo de cada enlace (si falta, el de
    /// las fuerzas).
    pub fn tick(
        &mut self,
        aristas: &[(u32, u32)],
        pesos: &[f32],
        largos: &[f32],
        grupos: &[u32],
        n_grupos: u32,
    ) -> bool {
        if !self.activo() || self.pos.is_empty() {
            return false;
        }
        let alpha = self.alpha;
        self.alpha -= self.alpha * ALPHA_DECAY;
        let n = self.pos.len();
        let f = self.fuerzas;

        // ---- repulsión Barnes-Hut, por nodo (en CORTEX, en paralelo: el mismo resultado) ----
        self.ticks = self.ticks.wrapping_add(1);
        let arbol = Quadtree::construir(&self.pos, self.ticks);
        let pos = &self.pos;
        let mut pila = Vec::with_capacity(512);
        let deltas: Vec<(f32, f32)> = pos
            .iter()
            .map(|&[x, y]| arbol.repulsion_con_pila(x, y, f.repulsion, &mut pila))
            .collect();
        for (v, d) in self.vel.iter_mut().zip(&deltas) {
            v[0] += d.0 * alpha;
            v[1] += d.1 * alpha;
        }

        // ---- muelles por arista, ponderados estilo d3 ----
        for (k, &(a, b)) in aristas.iter().enumerate() {
            let (a, b) = (a as usize, b as usize);
            let dx = self.pos[b][0] - self.pos[a][0];
            let dy = self.pos[b][1] - self.pos[a][1];
            let d = (dx * dx + dy * dy).sqrt().max(1e-3);
            let peso = pesos.get(k).copied().unwrap_or(1.0);
            let largo = largos.get(k).copied().unwrap_or(f.distancia_enlace);
            let empuje = (d - largo) / d * f.fuerza_enlace * peso * alpha;
            let (ex, ey) = (dx * empuje * 0.5, dy * empuje * 0.5);
            self.vel[a][0] += ex;
            self.vel[a][1] += ey;
            self.vel[b][0] -= ex;
            self.vel[b][1] -= ey;
        }

        // ---- agrupación por tema: atracción al centroide del grupo ----
        if f.fuerza_grupo > 0.0 && grupos.len() == n && n_grupos > 0 {
            let ng = n_grupos as usize;
            let mut cx = vec![0.0f32; ng];
            let mut cy = vec![0.0f32; ng];
            let mut cuenta = vec![0.0f32; ng];
            for (p, &g) in self.pos.iter().zip(grupos) {
                let gr = (g as usize).min(ng - 1);
                cx[gr] += p[0];
                cy[gr] += p[1];
                cuenta[gr] += 1.0;
            }
            for gr in 0..ng {
                if cuenta[gr] > 0.0 {
                    cx[gr] /= cuenta[gr];
                    cy[gr] /= cuenta[gr];
                }
            }
            // Dos componentes: cohesión (nodo → centroide de su grupo) y
            // compactación (centroide → centro global). La segunda traslada
            // cada cúmulo en bloque hacia los demás sin comprimirlo por
            // dentro: evita los océanos vacíos entre grupos cuando la fuerza
            // de enlace es baja.
            const COMPACTAR_GRUPOS: f32 = 0.04;
            for (i, &g) in grupos.iter().enumerate() {
                let gr = (g as usize).min(ng - 1);
                self.vel[i][0] +=
                    (cx[gr] - self.pos[i][0]) * f.fuerza_grupo * alpha - cx[gr] * COMPACTAR_GRUPOS * alpha;
                self.vel[i][1] +=
                    (cy[gr] - self.pos[i][1]) * f.fuerza_grupo * alpha - cy[gr] * COMPACTAR_GRUPOS * alpha;
            }
        }

        // ---- gravedad al centro + integración con límite de velocidad ----
        // El tope de desplazamiento por tick hace la simulación incondicional-
        // mente estable: sin él, una configuración extrema puede desbordar los
        // float (NaN) y el grafo entero desaparece. El suelo de 60 evita que
        // una distancia de enlace pequeña vuelva glacial la convergencia desde
        // lejos (cúmulos que no llegan a juntarse antes de enfriarse).
        let vmax = (f.distancia_enlace * 2.0).max(60.0);
        // Gravedad mínima implícita: con fuerza central en 0 y sin agrupar,
        // la repulsión expandiría el grafo sin límite; este suelo impercepti-
        // ble lo mantiene acotado.
        let centro = f.fuerza_centro.max(0.004);
        for i in 0..n {
            self.vel[i][0] -= self.pos[i][0] * centro * alpha;
            self.vel[i][1] -= self.pos[i][1] * centro * alpha;
            self.vel[i][0] *= VEL_DECAY;
            self.vel[i][1] *= VEL_DECAY;
            let v2 = self.vel[i][0] * self.vel[i][0] + self.vel[i][1] * self.vel[i][1];
            if !v2.is_finite() {
                self.vel[i] = [0.0, 0.0];
            } else if v2 > vmax * vmax {
                let k = vmax / v2.sqrt();
                self.vel[i][0] *= k;
                self.vel[i][1] *= k;
            }
            let m = self.movilidad[i];
            self.vel[i][0] *= m;
            self.vel[i][1] *= m;
            self.pos[i][0] += self.vel[i][0];
            self.pos[i][1] += self.vel[i][1];
            // guardia anti-NaN: una posición no finita se re-coloca en vez de
            // contaminar el quadtree y la caja de encuadre
            if !self.pos[i][0].is_finite() || !self.pos[i][1].is_finite() {
                let a = (i as f32) * 2.399_963;
                let r = f.distancia_enlace * (i as f32).sqrt() * 0.3;
                self.pos[i] = [r * a.cos(), r * a.sin()];
                self.vel[i] = [0.0, 0.0];
            }
        }

        // ---- cota dura de dispersión: nunca océanos de vacío ----
        // Radio cuadrático medio del grafo frente al radio ideal por densidad
        // (el área que necesitan n nodos a la distancia configurada). Si se
        // excede con margen, TODO el grafo se contrae un 2% por tick hacia su
        // centroide. La contracción no escala con alpha y la repulsión sí:
        // gane quien gane durante la simulación, el estado final siempre cabe
        // dentro del límite, con cualquier configuración de sliders.
        // Solo cuentan los nodos libres: uno clavado lejos (arrastrado a mano)
        // no debe contraer a los demás hacia él.
        let libres = || {
            self.pos
                .iter()
                .zip(&self.fijos)
                .filter(|(_, f)| f.is_none())
                .map(|(p, _)| p)
        };
        let n_libres = libres().count().max(1);
        let (mut sx, mut sy) = (0.0f64, 0.0f64);
        for p in libres() {
            sx += p[0] as f64;
            sy += p[1] as f64;
        }
        let (cx, cy) = ((sx / n_libres as f64) as f32, (sy / n_libres as f64) as f32);
        let mut sr2 = 0.0f64;
        for p in libres() {
            let (dx, dy) = (p[0] - cx, p[1] - cy);
            sr2 += (dx * dx + dy * dy) as f64;
        }
        let rms = ((sr2 / n_libres as f64) as f32).sqrt();
        let r_ideal = f.distancia_enlace.max(10.0) * (n as f32).sqrt() * 0.5;
        let limite = r_ideal * 1.4;
        if rms.is_finite() && rms > limite {
            // Contracción PROPORCIONAL (nada de todo-o-nada): justo lo que
            // hace falta para volver al límite, acotada al 2% por tick. Si se
            // contrajera siempre un 2% fijo, la repulsión re-expandiría al
            // tick siguiente y el ciclo se vería como una vibración.
            let s = (limite / rms).max(0.98);
            for (p, &m) in self.pos.iter_mut().zip(&self.movilidad) {
                let s = 1.0 - (1.0 - s) * m;
                p[0] = cx + (p[0] - cx) * s;
                p[1] = cy + (p[1] - cy) * s;
            }
        }

        // ---- nodos clavados: donde se dejaron, pase lo que pase ----
        for (i, f) in self.fijos.iter().enumerate() {
            if let Some(p) = *f {
                self.pos[i] = p;
                self.vel[i] = [0.0, 0.0];
            }
        }

        // ---- choques y bordes: nunca dos nodos superpuestos ni fuera ----
        // Un choque puede empujar un nodo fuera de la vista; el borde lo
        // devuelve y la pasada siguiente lo aparta deslizándolo por él.
        let mut acotados = vec![false; n];
        self.acotar(&mut acotados);
        for _ in 0..PASADAS_CHOQUE {
            let solapes = apartar(&mut self.pos, &self.choque, &mut self.fijos, self.arrastrado, &acotados);
            let fuera = self.acotar(&mut acotados);
            if !solapes && !fuera {
                break;
            }
        }
        true
    }

    /// Caja envolvente (min_x, min_y, max_x, max_y). El bucle de señales usa
    /// su propia copia sobre la instantánea compartida; esta queda para tests
    /// y usos internos del hilo de simulación.
    #[allow(dead_code)]
    pub fn caja(&self) -> (f32, f32, f32, f32) {
        let mut caja = (f32::MAX, f32::MAX, f32::MIN, f32::MIN);
        for p in &self.pos {
            caja.0 = caja.0.min(p[0]);
            caja.1 = caja.1.min(p[1]);
            caja.2 = caja.2.max(p[0]);
            caja.3 = caja.3.max(p[1]);
        }
        if self.pos.is_empty() {
            (0.0, 0.0, 0.0, 0.0)
        } else {
            caja
        }
    }
}

// ---------------- quadtree ----------------

struct Celda {
    hijos: [i32; 4], // -1 = vacío
    masa: f32,
    mx: f32, // centro de masa acumulado (sumas ponderadas hasta normalizar)
    my: f32,
    punto: i32, // índice de punto si es hoja ocupada, -1 si interna/vacía
}

struct Quadtree {
    celdas: Vec<Celda>,
    cx: f32,
    cy: f32,
    mitad: f32,
    puntos: Vec<[f32; 2]>,
}

impl Quadtree {
    fn construir(puntos: &[[f32; 2]], sal: u64) -> Self {
        let mut min = [f32::MAX; 2];
        let mut max = [f32::MIN; 2];
        for p in puntos {
            for k in 0..2 {
                min[k] = min[k].min(p[k]);
                max[k] = max[k].max(p[k]);
            }
        }
        let mut cx = (min[0] + max[0]) * 0.5;
        let mut cy = (min[1] + max[1]) * 0.5;
        let mut mitad = ((max[0] - min[0]).max(max[1] - min[1]) * 0.5 + 1.0).max(1.0);
        // Jitter por tick del origen de la rejilla: las fronteras entre
        // cuadrantes cambian de sitio en cada paso y sus artefactos de
        // aproximación se promedian en lugar de grabarse en el layout.
        let mut z = sal.wrapping_mul(0x9E37_79B9_7F4A_7C15);
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        let dx = (((z & 0xffff) as f32 / 65535.0) - 0.5) * 0.14 * mitad;
        let dy = ((((z >> 16) & 0xffff) as f32 / 65535.0) - 0.5) * 0.14 * mitad;
        cx += dx;
        cy += dy;
        mitad += dx.abs().max(dy.abs()) + 1.0;
        let mut arbol = Self {
            celdas: vec![Celda::vacia()],
            cx,
            cy,
            mitad,
            puntos: puntos.to_vec(),
        };
        for (i, p) in puntos.iter().enumerate() {
            if p[0].is_finite() && p[1].is_finite() {
                arbol.insertar(i as i32, *p);
            }
        }
        arbol.normalizar();
        arbol
    }

    fn insertar(&mut self, indice: i32, p: [f32; 2]) {
        let (mut celda, mut cx, mut cy, mut mitad) = (0usize, self.cx, self.cy, self.mitad);
        for _ in 0..48 {
            self.celdas[celda].masa += 1.0;
            self.celdas[celda].mx += p[0];
            self.celdas[celda].my += p[1];
            let ocupante = self.celdas[celda].punto;
            if ocupante >= 0 {
                // hoja ocupada: baja el ocupante un nivel y reintenta
                self.celdas[celda].punto = -1;
                let q = self.puntos[ocupante as usize];
                let cuad = Self::cuadrante(q, cx, cy);
                let hijo = self.hijo(celda, cuad);
                self.celdas[hijo].punto = ocupante;
                // la masa del ocupante ya está contada en esta celda; el hijo
                // nuevo la recibe al normalizar vía su propio recorrido:
                self.celdas[hijo].masa += 1.0;
                self.celdas[hijo].mx += q[0];
                self.celdas[hijo].my += q[1];
            }
            if self.celdas[celda].hijos == [-1; 4] && self.celdas[celda].masa <= 1.5 {
                self.celdas[celda].punto = indice;
                return;
            }
            let cuad = Self::cuadrante(p, cx, cy);
            let hijo = self.hijo(celda, cuad);
            mitad *= 0.5;
            cx += mitad * if cuad & 1 == 1 { 1.0 } else { -1.0 };
            cy += mitad * if cuad & 2 == 2 { 1.0 } else { -1.0 };
            celda = hijo;
        }
        // profundidad máxima (puntos casi coincidentes): acumula aquí
        self.celdas[celda].masa += 1.0;
        self.celdas[celda].mx += p[0];
        self.celdas[celda].my += p[1];
    }

    fn hijo(&mut self, celda: usize, cuad: usize) -> usize {
        if self.celdas[celda].hijos[cuad] < 0 {
            self.celdas.push(Celda::vacia());
            let nuevo = (self.celdas.len() - 1) as i32;
            self.celdas[celda].hijos[cuad] = nuevo;
        }
        self.celdas[celda].hijos[cuad] as usize
    }

    fn cuadrante(p: [f32; 2], cx: f32, cy: f32) -> usize {
        (if p[0] >= cx { 1 } else { 0 }) | (if p[1] >= cy { 2 } else { 0 })
    }

    fn normalizar(&mut self) {
        for c in &mut self.celdas {
            if c.masa > 0.0 {
                c.mx /= c.masa;
                c.my /= c.masa;
            }
        }
    }

    /// Fuerza de repulsión sobre (x,y) con aproximación θ. La pila la aporta
    /// el llamante para reutilizarla entre nodos.
    fn repulsion_con_pila(&self, x: f32, y: f32, intensidad: f32, pila: &mut Vec<(usize, f32)>) -> (f32, f32) {
        let (mut fx, mut fy) = (0.0f32, 0.0f32);
        pila.clear();
        pila.push((0, self.mitad * 2.0));
        while let Some((celda, ancho)) = pila.pop() {
            let c = &self.celdas[celda];
            if c.masa <= 0.0 {
                continue;
            }
            let dx = x - c.mx;
            let dy = y - c.my;
            let d2 = dx * dx + dy * dy;
            if ancho * ancho < THETA2 * d2 || c.hijos == [-1; 4] {
                if d2 > 1e-4 {
                    let f = intensidad * c.masa / d2;
                    fx += dx * f;
                    fy += dy * f;
                } else {
                    // coincidentes: empujón determinista pequeño
                    fx += intensidad * 0.01 * ((celda % 7) as f32 - 3.0);
                    fy += intensidad * 0.01 * ((celda % 5) as f32 - 2.0);
                }
            } else {
                for &h in &c.hijos {
                    if h >= 0 {
                        pila.push((h as usize, ancho * 0.5));
                    }
                }
            }
        }
        (fx, fy)
    }
}

impl Celda {
    fn vacia() -> Self {
        Self {
            hijos: [-1; 4],
            masa: 0.0,
            mx: 0.0,
            my: 0.0,
            punto: -1,
        }
    }
}

/// Pasadas de la relajación de choques por paso de la física: un empujón
/// puede crear otro solape más allá (una cadena de nodos), que se resuelve en
/// la pasada siguiente o en el próximo paso.
pub const PASADAS_CHOQUE: usize = 8;

/// Aparta los nodos que se solapan (como forceCollide de d3, pero como
/// corrección directa de posiciones: no depende del calor ni de la movilidad,
/// así que también actúa con la física casi parada o al arrastrar). Cada par
/// más cerca que la suma de sus radios de choque se separa justo hasta
/// tocarse: a medias, salvo que uno sea el arrastrado, que no se mueve. Un
/// nodo clavado que se aparta queda clavado en su sitio nuevo.
///
/// Hace hasta `pasadas` pasadas y devuelve si quedó sin solapes. Es O(n²) por
/// pasada: pensado para un menú (cientos de nodos, no miles).
pub fn separar(
    pos: &mut [[f32; 2]],
    choque: &[f32],
    fijos: &mut [Option<[f32; 2]>],
    arrastrado: Option<usize>,
    pasadas: usize,
) -> bool {
    (0..pasadas).any(|_| !apartar(pos, choque, fijos, arrastrado, &[]))
}

/// Una pasada de [`separar`]; devuelve si había solapes. `acotados`: los
/// nodos que un borde de la vista acaba de devolver; contra uno de ellos el
/// arrastrado también cede, porque el otro no tiene adónde ir.
fn apartar(
    pos: &mut [[f32; 2]],
    choque: &[f32],
    fijos: &mut [Option<[f32; 2]>],
    arrastrado: Option<usize>,
    acotados: &[bool],
) -> bool {
    let n = pos.len().min(choque.len());
    let acotado = |k: usize| acotados.get(k).copied().unwrap_or(false);
    let mut solapes = false;
    for i in 0..n {
        for j in i + 1..n {
            let minimo = choque[i] + choque[j];
            if minimo <= 0.0 {
                continue;
            }
            let (dx, dy) = (pos[j][0] - pos[i][0], pos[j][1] - pos[i][1]);
            let d2 = dx * dx + dy * dy;
            if d2 >= minimo * minimo {
                continue;
            }
            let d = d2.sqrt();
            // encima exacto: una dirección fija por par (reproducible)
            let (ux, uy) = if d > 1e-4 {
                (dx / d, dy / d)
            } else {
                let a = (i * 31 + j) as f32 * 2.399_963;
                (a.cos(), a.sin())
            };
            let solape = minimo - d;
            let (wi, wj) = match arrastrado {
                Some(a) if a == i && !acotado(j) => (0.0, 1.0),
                Some(a) if a == j && !acotado(i) => (1.0, 0.0),
                _ => (0.5, 0.5),
            };
            pos[i][0] -= ux * solape * wi;
            pos[i][1] -= uy * solape * wi;
            pos[j][0] += ux * solape * wj;
            pos[j][1] += uy * solape * wj;
            for k in [i, j] {
                if let Some(f) = fijos.get_mut(k).and_then(Option::as_mut) {
                    *f = pos[k];
                }
            }
            solapes = true;
        }
    }
    solapes
}

/// Lo que puede moverse un nodo al meterlo dentro de los bordes sin contar
/// como movido (errores de redondeo de la cámara), en unidades de mundo.
const HOLGURA_BORDE: f32 = 0.01;

/// `x` dentro de [desde, hasta]; si no cabe, en medio.
fn acotar_eje(x: f32, desde: f32, hasta: f32) -> f32 {
    if desde <= hasta {
        x.clamp(desde, hasta)
    } else {
        (desde + hasta) * 0.5
    }
}

#[cfg(test)]
mod tests {
    use super::{Fuerzas, Layout, PASADAS_CHOQUE, separar};

    fn distancia(a: [f32; 2], b: [f32; 2]) -> f32 {
        ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2)).sqrt()
    }

    #[test]
    fn separar_aparta_los_nodos_superpuestos() {
        // tres nodos casi en el mismo sitio y uno lejos: radio de choque 10
        let mut pos = vec![[0.0, 0.0], [3.0, 0.0], [0.0, 0.0], [500.0, 0.0]];
        let mut fijos = vec![None; 4];
        // tres casi en el mismo punto no se resuelven en un solo paso de la
        // física (en ella se repite cada paso): aquí, con margen de pasadas
        assert!(separar(&mut pos, &[10.0; 4], &mut fijos, None, 100));
        for i in 0..4 {
            for j in i + 1..4 {
                assert!(
                    distancia(pos[i], pos[j]) >= 20.0 - 0.01,
                    "{i} y {j} se solapan: {pos:?}"
                );
            }
        }
        assert_eq!(pos[3], [500.0, 0.0], "el que no se solapa no se toca");
    }

    #[test]
    fn el_arrastrado_no_cede_y_el_clavado_se_aparta_clavado() {
        let mut pos = vec![[0.0, 0.0], [5.0, 0.0]];
        let mut fijos = vec![Some([0.0, 0.0]), Some([5.0, 0.0])];
        assert!(separar(&mut pos, &[10.0, 10.0], &mut fijos, Some(0), PASADAS_CHOQUE));
        assert_eq!(pos[0], [0.0, 0.0], "el arrastrado va con el cursor");
        assert!((pos[1][0] - 20.0).abs() < 1e-4, "el otro se aparta justo: {:?}", pos[1]);
        assert_eq!(fijos[1], Some(pos[1]), "y sigue clavado, en su sitio nuevo");
    }

    #[test]
    fn un_nodo_clavado_no_se_mueve_y_tira_de_su_vecino() {
        let f = Fuerzas {
            distancia_enlace: 100.0,
            ..Fuerzas::default()
        };
        let mut l = Layout::nuevo(3, f);
        let aristas = [(0, 1), (1, 2)];
        let pesos = [1.0, 1.0];
        for _ in 0..400 {
            l.tick(&aristas, &pesos, &[], &[], 0);
        }
        // se clava el nodo 2 lejos y se recalienta: el 1, su vecino, le sigue
        let lejos = [900.0, -600.0];
        let d_antes = ((l.pos[1][0] - lejos[0]).powi(2) + (l.pos[1][1] - lejos[1]).powi(2)).sqrt();
        l.fijar(2, Some(lejos));
        l.recalentar(0.3);
        for _ in 0..400 {
            l.tick(&aristas, &pesos, &[], &[], 0);
        }
        assert_eq!(l.pos[2], lejos);
        let d_despues = ((l.pos[1][0] - lejos[0]).powi(2) + (l.pos[1][1] - lejos[1]).powi(2)).sqrt();
        assert!(
            d_despues < d_antes * 0.5,
            "el vecino no le siguió: {d_antes} → {d_despues}"
        );

        // suelto, vuelve a la física
        l.fijar(2, None);
        l.recalentar(0.3);
        for _ in 0..50 {
            l.tick(&aristas, &pesos, &[], &[], 0);
        }
        assert_ne!(l.pos[2], lejos);
    }
}
