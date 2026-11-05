// La galería en una ventana Vulkan propia, sin Flutter : lo mínimo que tiene que
// poner quien integra los shaders. Crea el dispositivo, abre una ventana, pinta
// en una textura y la copia a la ventana.
//
//   shd_visor [agujero|nodos]
//
// Teclas : 1 el agujero, 2 los nodos, q o Esc salir. Agujero : botón izquierdo
// orbita, derecho rueda, doble clic agita. Nodos : arrastrar mueve cualquier nodo,
// también el vórtice, contra otro nodo o contra los bordes, y un clic en el vórtice
// lo agita.
//
// Sin ventana, para comparar : con AGUJERO_VOLCAR=<carpeta> y
// AGUJERO_VOLCAR_TIEMPOS, escribe esos fotogramas en PPM, tal como se ven, y
// sale. Con AGUJERO_RELOJ, el reloj avanza 1/60 s por fotograma (en los nodos, 1/SHD_NODOS_FPS). SHD_TAM=<a>x<b>
// fija el tamaño de la vista, y SHD_LIENZO=<a>x<b> el del arranque. En los nodos,
// SHD_NODOS_VOLCAR=<f1,f2…> vuelca esos fotogramas, y SHD_NODOS_GUION mueve los
// nodos solo, para ver los choques sin ratón ; con SHD_NODOS_GUION=arrastre, el
// vórtice va de lado a lado cada vez más rápido; con SHD_NODOS_GUION=pared, atraviesa una
// pared de nodos y se aleja; con SHD_NODOS_GUION=cerca, se acerca a un enlace ajeno hasta
// cruzarlo. El vórtice es la gota, envuelta en un anillo del líquido de los enlaces que sigue
// su forma.
//
// El banco, en los nodos : con SHD_BANCO_GPU, lo que tarda en la GPU cada pasada y cada tramo
// de dentro (banco_visor.h); con SHD_NODOS_MALLA=<c>x<f>, una malla de nodos en vez de los
// ocho de siempre, para medir el líquido con un grafo grande.

# include <xcb/xcb.h>

# include <algorithm>

# include <chrono>

# include <cmath>

# include <cstdint>

# include <cstdio>

# include <cstdlib>

# include <cstring>

# include <memory>

# include <string>

# include <utility>

# include <vector>

# include "EngineFactoryVk.h"

# include "RefCntAutoPtr.hpp"

# include "SwapChain.h"

# include "Texture.h"

# include "agujero.h"

# include "computo.h"

# include "pasada_gota.h"

# include "pasada_liquido.h"
# include "pipeline_comun.h"

# include "banco_visor.h"

using namespace Diligent ;

namespace {

constexpr std :: uint32_t ancho_inicial = 1280 ;

constexpr std :: uint32_t alto_inicial = 800 ;

// Un toque seguido de otro antes de esto : doble clic

constexpr double doble_clic_s = 0.4 ;

// Cuándo se vuelca la escena de los nodos, si no se dice

constexpr std :: uint32_t fotograma_nodos = 120 ;

// Lo que se recibe de la ventana en un fotograma

struct Raton {

	float x = 0 ;

	float y = 0 ;

	bool izquierdo = false ;

	bool derecho = false ;

	bool dentro = false ;

	// Toques con el izquierdo y dobles clics desde el último fotograma

	std :: uint32_t toques = 0 ;

	std :: uint32_t dobles = 0 ;

} ;

double ahora_s() { return std :: chrono :: duration<double>(std :: chrono :: steady_clock :: now().time_since_epoch()).count() ; }

// «605x933», o `defecto`

void tam_de(const char * variable , std :: uint32_t & ancho , std :: uint32_t & alto) {

	const char * texto = std :: getenv(variable) ;

	unsigned a = 0 , b = 0 ;

	if (texto && std :: sscanf(texto , "%ux%u" , &a , &b) == 2 && a && b) {

		ancho = a ;

		alto = b ;

	}

}

// Los fotogramas de SHD_NODOS_VOLCAR, en orden

std :: vector<std :: uint32_t> fotogramas_pedidos() {

	std :: vector<std :: uint32_t> lista ;

	if (const char * texto = std :: getenv("SHD_NODOS_VOLCAR")) {

		std :: string resto(texto) ;

		while (!resto.empty()) {

			const std :: size_t coma = resto.find(',') ;

			const int f = std :: atoi(resto.substr(0 , coma).c_str()) ;

			if (f > 0) { lista.push_back(static_cast<std :: uint32_t>(f)) ; }

			resto = coma == std :: string :: npos ? std :: string() : resto.substr(coma + 1) ;

		}

	}

	if (lista.empty()) { lista.push_back(fotograma_nodos) ; }

	std :: sort(lista.begin() , lista.end()) ;

	return lista ;

}

std :: string carpeta_cache() {

	if (const char * x = std :: getenv("XDG_CACHE_HOME"); x && * x) { return std :: string(x) + "/shd/shaders" ; }

	if (const char * h = std :: getenv("HOME"); h && * h) { return std :: string(h) + "/.cache/shd/shaders" ; }

	return {} ;

}

// La ventana : XCB, sin más

class Ventana {

public:

	bool abrir(const char * titulo) {

		conexion_ = xcb_connect(nullptr , nullptr) ;

		if (xcb_connection_has_error(conexion_)) { return false ; }

		xcb_screen_t * pantalla = xcb_setup_roots_iterator(xcb_get_setup(conexion_)).data ;

		ventana_ = xcb_generate_id(conexion_) ;

		const std :: uint32_t eventos = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_STRUCTURE_NOTIFY ;

		const std :: uint32_t valores[] = { pantalla->black_pixel , eventos } ;

		xcb_create_window(conexion_ , XCB_COPY_FROM_PARENT , ventana_ , pantalla->root , 0 , 0 , static_cast<std :: uint16_t>(ancho) , static_cast<std :: uint16_t>(alto) , 0 , XCB_WINDOW_CLASS_INPUT_OUTPUT , pantalla->root_visual , XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK , valores) ;

		xcb_change_property(conexion_ , XCB_PROP_MODE_REPLACE , ventana_ , XCB_ATOM_WM_NAME , XCB_ATOM_STRING , 8 , static_cast<std :: uint32_t>(std :: strlen(titulo)) , titulo) ;

		// que el gestor de ventanas avise al cerrar, en vez de cortar la conexión

		protocolos_ = atomo("WM_PROTOCOLS") ;

		borrar_ = atomo("WM_DELETE_WINDOW") ;

		xcb_change_property(conexion_ , XCB_PROP_MODE_REPLACE , ventana_ , protocolos_ , XCB_ATOM_ATOM , 32 , 1 , &borrar_) ;

		xcb_map_window(conexion_ , ventana_) ;

		xcb_flush(conexion_) ;

		return true ;

	}

	~Ventana() { if (conexion_) { xcb_disconnect(conexion_) ; } }

	// Lo que llegó, sin esperar

	void atender(Raton & raton) {

		while (xcb_generic_event_t * e = xcb_poll_for_event(conexion_)) {

			evento(e , raton) ;

			std :: free(e) ;

		}

	}

	xcb_connection_t * conexion() const { return conexion_ ; }

	std :: uint32_t id() const { return ventana_ ; }

	std :: uint32_t ancho = ancho_inicial ;

	std :: uint32_t alto = alto_inicial ;

	bool cerrar = false ;

	// 1, 2 o 3 si se pulsó, 0 si no

	int tecla = 0 ;

private:

	xcb_connection_t * conexion_ = nullptr ;

	xcb_window_t ventana_ = 0 ;

	xcb_atom_t protocolos_ = 0 ;

	xcb_atom_t borrar_ = 0 ;

	double ultimo_toque_ = -1 ;

	xcb_atom_t atomo(const char * nombre) {

		const auto peticion = xcb_intern_atom(conexion_ , 0 , static_cast<std :: uint16_t>(std :: strlen(nombre)) , nombre) ;

		xcb_intern_atom_reply_t * respuesta = xcb_intern_atom_reply(conexion_ , peticion , nullptr) ;

		const xcb_atom_t a = respuesta ? respuesta->atom : 0 ;

		std :: free(respuesta) ;

		return a ;

	}

	void boton(int cual , bool pulsado , Raton & raton) {

		if (cual == XCB_BUTTON_INDEX_3) { raton.derecho = pulsado ; }

		if (cual != XCB_BUTTON_INDEX_1) { return ; }

		raton.izquierdo = pulsado ;

		if (!pulsado) { return ; }

		raton.toques++ ;

		const double t = ahora_s() ;

		if (ultimo_toque_ >= 0 && t - ultimo_toque_ < doble_clic_s) {

			raton.dobles++ ;

			ultimo_toque_ = -1 ;

			return ;

		}

		ultimo_toque_ = t ;

	}

	void evento(const xcb_generic_event_t * e , Raton & raton) {

		switch (e->response_type & ~0x80) {

			case XCB_CONFIGURE_NOTIFY : {

				const auto * c = reinterpret_cast<const xcb_configure_notify_event_t *>(e) ;

				ancho = c->width ;

				alto = c->height ;

				break ;

			}

			case XCB_MOTION_NOTIFY : {

				const auto * m = reinterpret_cast<const xcb_motion_notify_event_t *>(e) ;

				raton.x = m->event_x ;

				raton.y = m->event_y ;

				raton.dentro = true ;

				break ;

			}

			case XCB_ENTER_NOTIFY : raton.dentro = true ; break ;

			case XCB_LEAVE_NOTIFY : raton.dentro = false ; break ;

			case XCB_BUTTON_PRESS : boton(reinterpret_cast<const xcb_button_press_event_t *>(e)->detail , true , raton) ; break ;

			case XCB_BUTTON_RELEASE : boton(reinterpret_cast<const xcb_button_release_event_t *>(e)->detail , false , raton) ; break ;

			case XCB_KEY_PRESS : {

				// códigos de tecla de X : 10 y 11, el 1 y el 2; 9 Esc, 24 q

				const int k = reinterpret_cast<const xcb_key_press_event_t *>(e)->detail ;

				if (k == 9 || k == 24) { cerrar = true ; }

				if (k == 10) { tecla = 1 ; }

				if (k == 11) { tecla = 2 ; }
				break ;

			}

			case XCB_CLIENT_MESSAGE : {

				const auto * c = reinterpret_cast<const xcb_client_message_event_t *>(e) ;

				if (c->data.data32[0] == borrar_) { cerrar = true ; }

				break ;

			}

			default : break ;

		}

	}

} ;

// Una escena de ejemplo, en cada caso, la pinta en el objetivo fijado

class Escena {

public:

	virtual ~Escena() = default ;

	// Antes de fijar el objetivo; false si falló

	virtual bool avanzar(std :: uint32_t ancho , std :: uint32_t alto , const Raton & raton , std :: string & error) = 0 ;

	virtual void pintar(std :: uint32_t ancho , std :: uint32_t alto) = 0 ;

	virtual const float * fondo() const = 0 ;

} ;

class EscenaAgujero final : public Escena {

public:

	EscenaAgujero(const shd :: Gpu & gpu , std :: uint32_t lienzo_ancho , std :: uint32_t lienzo_alto) :

		gpu_(gpu) ,

		agujero_(std :: getenv("AGUJERO_RELOJ") ? shd :: reloj_de_pasos(1.0 / 60) : shd :: reloj_monotono()) ,

		lienzo_ancho_(lienzo_ancho) ,

		lienzo_alto_(lienzo_alto) {}

	bool avanzar(std :: uint32_t ancho , std :: uint32_t alto , const Raton & raton , std :: string & error) override {

		if (!creado_) {

			if (!agujero_.crear(gpu_ , error)) { return false ; }

			agujero_.asentar(lienzo_ancho_ , lienzo_alto_) ;

			creado_ = true ;

		}

		if (raton.dobles) { for (std :: uint32_t i = 0 ; i < raton.dobles ; i++) { shd :: agujero :: doble_clic() ; } }

		shd :: agujero :: cursor({ raton.x , raton.y , raton.izquierdo , raton.dentro , raton.derecho }) ;

		return agujero_.ajustar(ancho , alto , error) && agujero_.avanzar(ancho , alto , error) ;

	}

	void pintar(std :: uint32_t ancho , std :: uint32_t alto) override { agujero_.pintar(ancho , alto) ; }

	const float * fondo() const override { return negro_ ; }

	shd :: Agujero & agujero() { return agujero_ ; }

private:

	shd :: Gpu gpu_ ;

	shd :: Agujero agujero_ ;

	std :: uint32_t lienzo_ancho_ ;

	std :: uint32_t lienzo_alto_ ;

	bool creado_ = false ;

	const float negro_[4] = { 0 , 0 , 0 , 1 } ;

} ;

class EscenaNodos final : public Escena {

public:

	explicit EscenaNodos(const shd :: Gpu & gpu) : gpu_(gpu) {

		// un vórtice en el centro y ocho nodos alrededor, unidos a él y en anillo

		constexpr int cuantos = 8 ;

		nodos_.push_back({ 0 , 0 , radio_vortice , 0x74c7ecff }) ;

		const std :: uint32_t colores[] = { 0x89b4faff , 0xf38ba8ff , 0xa6e3a1ff , 0xf9e2afff } ;

		for (int i = 0 ; i < cuantos ; i++) {

			const float a = 6.2831853f * static_cast<float>(i) / cuantos ;

			nodos_.push_back({ 170 * std :: cos(a) , 170 * std :: sin(a) , 16 , colores[i % 4] }) ;

			const auto n = static_cast<std :: uint32_t>(i) + 1 ;

			aristas_.insert(aristas_.end() , { 0 , n , n , n % cuantos + 1 }) ;

		}

		// con SHD_NODOS_MALLA=CxF, para el banco, un grafo grande : el vórtice y una malla de C × F
		// nodos, cada uno unido al de su derecha y al de debajo

		std :: uint32_t cols = 0 , filas = 0 ;

		tam_de("SHD_NODOS_MALLA" , cols , filas) ;

		if (cols && filas) {

			nodos_.resize(1) ;

			aristas_.clear() ;

			for (std :: uint32_t y = 0 ; y < filas ; y++) {

				for (std :: uint32_t x = 0 ; x < cols ; x++) {

					const float px = (static_cast<float>(x) - static_cast<float>(cols - 1) * 0.5f) * 40 ;

					const float py = (static_cast<float>(y) - static_cast<float>(filas - 1) * 0.5f) * 40 ;

					nodos_.push_back({ px , py , 8 , colores[(x + y) % 4] }) ;

					const std :: uint32_t i = 1 + y * cols + x ;

					if (x + 1 < cols) { aristas_.insert(aristas_.end() , { i , i + 1 }) ; }

					if (y + 1 < filas) { aristas_.insert(aristas_.end() , { i , i + cols }) ; }

				}

			}

		}

		const char * fps = std :: getenv("SHD_NODOS_FPS") ;

		shd :: Reloj reloj = std :: getenv("AGUJERO_RELOJ") ? shd :: reloj_de_pasos(1.0 / (fps ? std :: atof(fps) : 60.0)) : shd :: reloj_monotono() ;

		auto la_gota = std :: make_unique<shd :: nodos :: PasadaGota>(std :: move(reloj)) ;

		// el líquido lee de la gota lo lejos que queda : su anillo la sigue. Va antes que la gota,
		// que se pinta encima

		pasadas_.push_back(std :: make_unique<shd :: nodos :: PasadaLiquido>(* la_gota)) ;

		pasadas_.push_back(std :: move(la_gota)) ;

		// con SHD_BANCO_GPU, lo que tarda en la GPU cada pasada y lo que mide cada una por dentro

		if (std :: getenv("SHD_BANCO_GPU")) {

			banco_ = std :: make_unique<BancoVisor>() ;

			for (std :: size_t i = 0 ; i < pasadas_.size() ; i++) {

				banco_->nombrar(i , pasadas_[i]->nombre()) ;

				pasadas_[i]->medir(banco_.get() , primer_tramo(i)) ;

			}

		}

	}

	~EscenaNodos() override {

		if (!banco_) { return ; }

		apuntar() ;

		banco_->informar() ;

	}

	bool avanzar(std :: uint32_t ancho , std :: uint32_t alto , const Raton & raton , std :: string & error) override {

		if (!uniforme_) {

			BufferDesc desc ;

			desc.Name = "Visor camara" ;

			desc.Size = sizeof(shd :: nodos :: CamaraGpu) ;

			desc.Usage = USAGE_DEFAULT ;

			desc.BindFlags = BIND_UNIFORM_BUFFER ;

			desc.ImmediateContextMask = gpu_.mascara ;

			gpu_.device->CreateBuffer(desc , nullptr , &uniforme_) ;

			if (!uniforme_) { return shd :: gpu_fallo(error , "No se pudo crear el uniform de la cámara") ; }

		}

		escena_.escala = 1.5f ;

		escena_.factor_radio = 1 ;

		escena_.cuenta_nodos = static_cast<std :: uint32_t>(nodos_.size()) ;

		escena_.vortice = 1 ;

		escena_.ancho = ancho ;

		escena_.alto = alto ;

		agarrar(ancho , alto , raton) ;

		if (guion_) { seguir_guion(ancho) ; }

		// los nodos no se tocan; el vórtice no choca, porque es la gota

		shd :: nodos :: separar_nodos(escena_ , rust :: Slice<shd :: nodos :: Nodo>(nodos_.data() , nodos_.size()) , static_cast<std :: uint32_t>(agarrado_ + 1)) ;

		fotograma_ = { escena_ , &nodos_ , &aristas_ , uniforme_ } ;

		camara_ = shd :: nodos :: camara_de(escena_ , ancho , alto) ;

		for (const auto & p : pasadas_) {

			if (!p->preparar(gpu_ , fotograma_ , camara_ , error)) { return false ; }

		}

		gpu_.ctx->UpdateBuffer(uniforme_ , 0 , sizeof(camara_) , &camara_ , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

		if (banco_ && medidos_++) { apuntar() ; }

		for (std :: size_t i = 0 ; i < pasadas_.size() ; i++) {

			shd :: Tramo tramo(banco_.get() , i , shd :: Etapa :: subir , gpu_.device , gpu_.ctx) ;

			pasadas_[i]->subir(gpu_.ctx , fotograma_) ;

		}

		return true ;

	}

	void pintar(std :: uint32_t , std :: uint32_t) override {

		for (std :: size_t i = 0 ; i < pasadas_.size() ; i++) {

			shd :: Tramo tramo(banco_.get() , i , shd :: Etapa :: dibujar , gpu_.device , gpu_.ctx) ;

			pasadas_[i]->dibujar(gpu_.ctx , fotograma_) ;

		}

	}

	const float * fondo() const override { return fondo_ ; }

private:

	static constexpr float radio_vortice = 60 ;

	// Lo que el vórtice se acerca al borde derecho en el guion, en mundo

	static constexpr float sobra_borde = 42 ;

	// Lo que dura el guion, en fotogramas

	static constexpr std :: uint32_t vuelta_guion = 420 ;

	static constexpr std :: uint32_t vuelta_arrastre = 1700 ;

	static constexpr std :: uint32_t vuelta_pared = 480 ;

	static constexpr std :: uint32_t vuelta_cerca = 360 ;

	const char * nombre_guion_ = std :: getenv("SHD_NODOS_GUION") ;

	const bool guion_ = nombre_guion_ != nullptr ;

	const bool guion_arrastre_ = guion_ && std :: strcmp(nombre_guion_ , "arrastre") == 0 ;

	const bool guion_pared_ = guion_ && std :: strcmp(nombre_guion_ , "pared") == 0 ;

	const bool guion_cerca_ = guion_ && std :: strcmp(nombre_guion_ , "cerca") == 0 ;

	std :: uint32_t cuenta_ = 0 ;

	int agarrado_ = -1 ;

	bool movido_ = false ;

	// Lo que hay del cursor al centro del nodo agarrado, en mundo

	float agarre_x_ = 0 ;

	float agarre_y_ = 0 ;

	// Arrastrar mueve el nodo, y un clic en el vórtice sin moverlo lo agita

	void agarrar(std :: uint32_t ancho , std :: uint32_t alto , const Raton & raton) {

		const float x = (raton.x - static_cast<float>(ancho) * 0.5f) / escena_.escala ;

		const float y = (raton.y - static_cast<float>(alto) * 0.5f) / escena_.escala ;

		if (raton.toques && agarrado_ < 0) {

			for (std :: size_t i = 0 ; i < nodos_.size() ; i++) {

				if (std :: hypot(x - nodos_[i].x , y - nodos_[i].y) > nodos_[i].radio) { continue ; }

				agarrado_ = static_cast<int>(i) ;

				agarre_x_ = nodos_[i].x - x ;

				agarre_y_ = nodos_[i].y - y ;

				movido_ = false ;

				break ;

			}

		}

		if (agarrado_ < 0) { return ; }

		shd :: nodos :: Nodo & n = nodos_[static_cast<std :: size_t>(agarrado_)] ;

		movido_ = movido_ || std :: hypot(x + agarre_x_ - n.x , y + agarre_y_ - n.y) > 2 ;

		if (movido_) {

			// sin salir de la ventana más de lo que se puede aplastar

			const float dentro_x = std :: max(static_cast<float>(ancho) * 0.5f / escena_.escala - n.radio * 0.7f , 0.0f) ;

			const float dentro_y = std :: max(static_cast<float>(alto) * 0.5f / escena_.escala - n.radio * 0.7f , 0.0f) ;

			n.x = std :: clamp(x + agarre_x_ , -dentro_x , dentro_x) ;

			n.y = std :: clamp(y + agarre_y_ , -dentro_y , dentro_y) ;

		}

		if (raton.izquierdo) { return ; }

		if (agarrado_ == 0 && !movido_) { escena_.pulsos++ ; }

		agarrado_ = -1 ;

	}

	// De `a` a `b` entre los fotogramas `desde` y `hasta`

	float tramo(float a , float b , std :: uint32_t desde , std :: uint32_t hasta) const {

		const float t = std :: clamp((static_cast<float>(cuenta_) - static_cast<float>(desde)) / static_cast<float>(hasta - desde) , 0.0f , 1.0f) ;

		return a + (b - a) * t ;

	}

	// Un enlace entre dos nodos que pasa cerca del vórtice, sin ser suyo : el vórtice se le
	// acerca despacio hasta cruzarlo. Mientras no lo toca, no debe unirse a él

	void seguir_cerca() {

		cuenta_ = cuenta_ % vuelta_cerca + 1 ;

		for (std :: size_t i = 1 ; i < nodos_.size() ; i++) {

			const float x = i == 1 ? -170.0f : i == 2 ? 170.0f : (static_cast<float>(i) - 5.5f) * 70 ;

			const float y = i <= 2 ? 90.0f : -190.0f ;

			nodos_[i] = { x , y , 16 , nodos_[i].rgba } ;

		}

		nodos_[0].x = 0 ;

		nodos_[0].y = tramo(0 , 90 , 30 , 330) ;

	}

	// Una pared de tres nodos tan juntos como deja su choque (separar_nodos), con sus gotas
	// rozándose : el vórtice la atraviesa, se queda al otro lado, se aleja y vuelve. Lo que
	// quede atrapado detrás no debe cruzarla

	void seguir_pared() {

		cuenta_ = cuenta_ % vuelta_pared + 1 ;

		for (std :: size_t i = 1 ; i < nodos_.size() ; i++) {

			const float y = i <= 3 ? (static_cast<float>(i) - 2) * 16 * 2 * shd :: nodos :: separacion_nodos() : (i % 2 == 0 ? 180.0f : -180.0f) ;

			const float x = i <= 3 ? 0.0f : (static_cast<float>(i / 2) - 2.5f) * 100 ;

			nodos_[i] = { x , y , 16 , nodos_[i].rgba } ;

		}

		const float x = cuenta_ < 200 ? tramo(-120 , 110 , 40 , 80) : tramo(110 , 200 , 200 , 230) ;

		nodos_[0].x = cuenta_ < 330 ? x : tramo(200 , -120 , 330 , 380) ;

		nodos_[0].y = 0 ;

	}

	// El vórtice de lado a lado sin chocar con nada, a 1, 2,5, 5, 10 y 20 radios por
	// segundo, con una pausa tras cada tramo : para ver si el líquido va con su nodo.
	// Luego, a lo bruto : contra el borde, contra un nodo y barriendo una fila de ellos

	void seguir_arrastre() {

		struct Tramo { float ax ; float ay ; float bx ; float by ; std :: uint32_t desde ; std :: uint32_t hasta ; } ;

		static constexpr Tramo tramos[] = {

			{ 0 , 0 , -150 , 0 , 0 , 30 } , { -150 , 0 , 150 , 0 , 90 , 390 } , { 150 , 0 , -150 , 0 , 450 , 570 } ,

			{ -150 , 0 , 150 , 0 , 630 , 690 } , { 150 , 0 , -150 , 0 , 750 , 780 } , { -150 , 0 , 150 , 0 , 840 , 855 } ,

			{ 150 , 0 , 225 , 0 , 915 , 919 } , { 225 , 0 , 100 , 0 , 975 , 990 } , { 100 , 0 , 100 , -125 , 1035 , 1040 } ,

			{ 100 , -125 , -200 , -125 , 1095 , 1125 } , { -200 , -125 , 0 , 0 , 1185 , 1215 }

		} ;

		cuenta_ = cuenta_ % vuelta_arrastre + 1 ;

		// y al final, doce clics seguidos

		if (cuenta_ >= 1275 && cuenta_ < 1395 && cuenta_ % 10 == 5) { escena_.pulsos++ ; }

		for (const Tramo & t : tramos) {

			if (cuenta_ < t.desde) { continue ; }

			nodos_[0].x = tramo(t.ax , t.bx , t.desde , t.hasta) ;

			nodos_[0].y = tramo(t.ay , t.by , t.desde , t.hasta) ;

		}

		// los demás, arriba y abajo : fuera de su camino

		for (std :: size_t i = 1 ; i < nodos_.size() ; i++) {

			const float lado = i % 2 == 0 ? 1.0f : -1.0f ;

			nodos_[i] = { (static_cast<float>(i / 2) - 2) * 100 , lado * 170 , nodos_[i].radio , nodos_[i].rgba } ;

		}

	}

	// Un nodo entra en el vórtice y sale, y luego el vórtice se aplasta contra el
	// borde derecho

	void seguir_guion(std :: uint32_t ancho) {

		if (guion_arrastre_) {

			seguir_arrastre() ;

			return ;

		}

		if (guion_pared_) {

			seguir_pared() ;

			return ;

		}

		if (guion_cerca_) {

			seguir_cerca() ;

			return ;

		}

		// una vuelta tras otra

		cuenta_ = cuenta_ % vuelta_guion + 1 ;

		nodos_[1].x = cuenta_ < 200 ? tramo(170 , 58 , 60 , 110) : tramo(58 , 170 , 200 , 250) ;

		nodos_[1].y = tramo(0 , -240 , 250 , 290) ;

		const float borde = static_cast<float>(ancho) * 0.5f / escena_.escala ;

		nodos_[0].x = tramo(0 , borde - sobra_borde , 300 , 350) ;

	}

	shd :: Gpu gpu_ ;

	std :: vector<shd :: nodos :: Nodo> nodos_ ;

	std :: vector<std :: uint32_t> aristas_ ;

	std :: vector<std :: unique_ptr<shd :: nodos :: Pasada>> pasadas_ ;

	// El banco (SHD_BANCO_GPU) y los fotogramas medidos. Cada pasada mide lo suyo a
	// partir de su propio tramo : de 16 en 16 detrás de los de las pasadas

	std :: unique_ptr<BancoVisor> banco_ ;

	std :: uint32_t medidos_ = 0 ;

	std :: size_t primer_tramo(std :: size_t pasada) const { return pasadas_.size() + 16 * pasada ; }

	// Lo del fotograma anterior, ya acabado en la GPU, y un informe cada 300

	void apuntar() {

		banco_->terminar(gpu_.ctx) ;

		for (std :: size_t i = 0 ; i < pasadas_.size() ; i++) {

			const std :: vector<std :: string> nombres = pasadas_[i]->tramos() ;

			for (std :: size_t k = 0 ; k < nombres.size() ; k++) { banco_->nombrar(primer_tramo(i) + k , nombres[k]) ; }

		}

		if (medidos_ % 300 == 0) { banco_->informar() ; }

	}

	RefCntAutoPtr<IBuffer> uniforme_ ;

	shd :: nodos :: Escena escena_{} ;

	shd :: nodos :: Fotograma fotograma_{} ;

	shd :: nodos :: CamaraGpu camara_{} ;

	const float fondo_[4] = { 0.07f , 0.07f , 0.1f , 1 } ;

} ;

// Lo que copia la textura a la ventana : los shaders pintan en RGBA8, y la ventana
// tiene el formato que quiera

constexpr const char * copiar_vs = R"(
void main(in uint vid : SV_VertexID, out float4 posicion : SV_POSITION) {
  float2 uv = float2(float((vid << 1u) & 2u), float(vid & 2u));
  posicion = float4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)" ;

constexpr const char * copiar_ps = R"(
Texture2D<float4> imagen;

float4 main(in float4 posicion : SV_POSITION) : SV_TARGET {
  return imagen.Load(int3(int2(posicion.xy), 0));
}
)" ;

struct Copia {

	RefCntAutoPtr<IPipelineState> pipeline ;

	RefCntAutoPtr<IShaderResourceBinding> binding ;

	bool crear(const shd :: Gpu & gpu , TEXTURE_FORMAT formato , std :: string & error) {

		RefCntAutoPtr<IShader> vs = shd :: gpu_compilar(gpu , { SHADER_TYPE_VERTEX , "Visor copia VS" , copiar_vs }) ;

		RefCntAutoPtr<IShader> ps = shd :: gpu_compilar(gpu , { SHADER_TYPE_PIXEL , "Visor copia PS" , copiar_ps }) ;

		if (!vs || !ps) { return shd :: gpu_fallo(error , "No se pudo compilar la copia a la ventana") ; }

		GraphicsPipelineStateCreateInfo pso ;

		pso.PSODesc.Name = "Visor copia" ;

		pso.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS ;

		pso.GraphicsPipeline.NumRenderTargets = 1 ;

		pso.GraphicsPipeline.RTVFormats[0] = formato ;

		pso.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ;

		pso.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE ;

		pso.GraphicsPipeline.DepthStencilDesc.DepthEnable = False ;

		pso.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC ;

		pso.pVS = vs ;

		pso.pPS = ps ;

		gpu.device->CreateGraphicsPipelineState(pso , &pipeline) ;

		if (!pipeline) { return shd :: gpu_fallo(error , "No se pudo crear la copia a la ventana") ; }

		pipeline->CreateShaderResourceBinding(&binding , true) ;

		return true ;

	}

	void dibujar(IDeviceContext * ctx , ITexture * imagen) {

		binding->GetVariableByName(SHADER_TYPE_PIXEL , "imagen")->Set(imagen->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)) ;

		ctx->SetPipelineState(pipeline) ;

		ctx->CommitShaderResources(binding , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

		DrawAttribs draw ;

		draw.NumVertices = 3 ;

		ctx->Draw(draw) ;

	}

} ;

// La textura en la que pintan los shaders y la copia para leerla desde la CPU

struct Lienzo {

	RefCntAutoPtr<ITexture> imagen ;

	RefCntAutoPtr<ITexture> lectura ;

	std :: uint32_t ancho = 0 ;

	std :: uint32_t alto = 0 ;

	bool ajustar(const shd :: Gpu & gpu , std :: uint32_t w , std :: uint32_t h , std :: string & error) {

		if (imagen && ancho == w && alto == h) { return true ; }

		// la anterior puede seguir en uso

		gpu.ctx->Flush() ;

		gpu.ctx->WaitForIdle() ;

		imagen.Release() ;

		lectura.Release() ;

		TextureDesc d ;

		d.Name = "Visor lienzo" ;

		d.Type = RESOURCE_DIM_TEX_2D ;

		d.Width = w ;

		d.Height = h ;

		d.Format = TEX_FORMAT_RGBA8_UNORM ;

		d.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE ;

		d.Usage = USAGE_DEFAULT ;

		gpu.device->CreateTexture(d , nullptr , &imagen) ;

		d.Name = "Visor lectura" ;

		d.BindFlags = BIND_NONE ;

		d.Usage = USAGE_STAGING ;

		d.CPUAccessFlags = CPU_ACCESS_READ ;

		gpu.device->CreateTexture(d , nullptr , &lectura) ;

		if (!imagen || !lectura) { return shd :: gpu_fallo(error , "No se pudo crear el lienzo") ; }

		ancho = w ;

		alto = h ;

		return true ;

	}

	void fijar(IDeviceContext * ctx , const float fondo[4]) {

		ITextureView * rtv = imagen->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) ;

		ctx->SetRenderTargets(1 , &rtv , nullptr , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

		ctx->ClearRenderTarget(rtv , fondo , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

		Viewport vista(0.0f , 0.0f , static_cast<float>(ancho) , static_cast<float>(alto)) ;

		ctx->SetViewports(1 , &vista , ancho , alto) ;

	}

	// Tal como se ve

	bool volcar(IDeviceContext * ctx , const std :: string & ruta) {

		CopyTextureAttribs copia(imagen , RESOURCE_STATE_TRANSITION_MODE_TRANSITION , lectura , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

		ctx->CopyTexture(copia) ;

		ctx->Flush() ;

		ctx->WaitForIdle() ;

		MappedTextureSubresource m ;

		ctx->MapTextureSubresource(lectura , 0 , 0 , MAP_READ , MAP_FLAG_NONE , nullptr , m) ;

		if (!m.pData) { return false ; }

		std :: FILE * f = std :: fopen(ruta.c_str() , "wb") ;

		if (!f) {

			ctx->UnmapTextureSubresource(lectura , 0 , 0) ;

			return false ;

		}

		std :: fprintf(f , "P6\n%u %u\n255\n" , ancho , alto) ;

		std :: vector<std :: uint8_t> fila(static_cast<std :: size_t>(ancho) * 3) ;

		for (std :: uint32_t y = 0 ; y < alto ; y++) {

			const auto * p = static_cast<const std :: uint8_t *>(m.pData) + m.Stride * y ;

			for (std :: uint32_t x = 0 ; x < ancho ; x++) {

				fila[x * 3] = p[x * 4] ;

				fila[x * 3 + 1] = p[x * 4 + 1] ;

				fila[x * 3 + 2] = p[x * 4 + 2] ;

			}

			std :: fwrite(fila.data() , 1 , fila.size() , f) ;

		}

		std :: fclose(f) ;

		ctx->UnmapTextureSubresource(lectura , 0 , 0) ;

		return true ;

	}

} ;

std :: unique_ptr<Escena> escena_de(int cual , const shd :: Gpu & gpu , std :: uint32_t lienzo_ancho , std :: uint32_t lienzo_alto) {

	if (cual == 2) { return std :: make_unique<EscenaNodos>(gpu) ; }

	return std :: make_unique<EscenaAgujero>(gpu , lienzo_ancho , lienzo_alto) ;

}

}  // namespace

int main(int argc , char ** argv) {

	int cual = 1 ;

	if (argc > 1 && std :: strcmp(argv[1] , "nodos") == 0) { cual = 2 ; }
	Ventana ventana ;

	if (!ventana.abrir("N-SHD")) {

		std :: fprintf(stderr , "No se pudo abrir la ventana\n") ;

		return 1 ;

	}

	IEngineFactoryVk * fabrica = GetEngineFactoryVk() ;

	RefCntAutoPtr<IRenderDevice> device ;

	RefCntAutoPtr<IDeviceContext> ctx ;

	RefCntAutoPtr<ISwapChain> cadena ;

	EngineVkCreateInfo ci ;

	// el adaptador (el dedicado, si lo hay) y su cola gráfica

	Uint32 cuantos = 0 ;

	fabrica->EnumerateAdapters(ci.GraphicsAPIVersion , cuantos , nullptr) ;

	std :: vector<GraphicsAdapterInfo> adaptadores(cuantos) ;

	if (cuantos) { fabrica->EnumerateAdapters(ci.GraphicsAPIVersion , cuantos , adaptadores.data()) ; }

	ImmediateContextCreateInfo contexto ;

	contexto.Name = "Visor" ;

	contexto.Priority = QUEUE_PRIORITY_MEDIUM ;

	bool hay = false ;

	for (Uint32 i = 0 ; i < cuantos ; i++) {

		const GraphicsAdapterInfo & a = adaptadores[i] ;

		for (Uint32 k = 0 ; k < a.NumQueues ; k++) {

			if ((a.Queues[k].QueueType & COMMAND_QUEUE_TYPE_GRAPHICS) != COMMAND_QUEUE_TYPE_GRAPHICS) { continue ; }

			contexto.QueueId = static_cast<Uint8>(k) ;

			ci.AdapterId = i ;

			hay = true ;

			break ;

		}

		if (hay && a.Type == ADAPTER_TYPE_DISCRETE) { break ; }

	}

	ci.NumImmediateContexts = 1 ;

	ci.pImmediateContextInfo = &contexto ;

	IDeviceContext * crudo = nullptr ;

	fabrica->CreateDeviceAndContextsVk(ci , &device , &crudo) ;

	if (!device) {

		std :: fprintf(stderr , "No se pudo crear el dispositivo Vulkan\n") ;

		return 1 ;

	}

	ctx.Attach(crudo) ;

	SwapChainDesc cadena_desc ;

	cadena_desc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM ;

	cadena_desc.DepthBufferFormat = TEX_FORMAT_UNKNOWN ;

	LinuxNativeWindow nativa ;

	nativa.WindowId = ventana.id() ;

	nativa.pXCBConnection = ventana.conexion() ;

	fabrica->CreateSwapChainVk(device , ctx , cadena_desc , NativeWindow{ nativa } , &cadena) ;

	if (!cadena) {

		std :: fprintf(stderr , "No se pudo crear la cadena de imágenes\n") ;

		return 1 ;

	}

	shd :: Gpu gpu ;

	gpu.device = device ;

	gpu.ctx = ctx ;

	gpu.mascara = 1 ;

	gpu.cache = carpeta_cache() ;

	gpu.aviso = [](const std :: string & m) { std :: printf("%s\n" , m.c_str()) ; } ;

	std :: string error ;

	Copia copia ;

	if (!copia.crear(gpu , cadena->GetDesc().ColorBufferFormat , error)) {

		std :: fprintf(stderr , "%s\n" , error.c_str()) ;

		return 1 ;

	}

	std :: uint32_t ancho_fijo = 0 , alto_fijo = 0 , lienzo_ancho = 1920 , lienzo_alto = 1080 ;

	tam_de("SHD_TAM" , ancho_fijo , alto_fijo) ;

	tam_de("SHD_LIENZO" , lienzo_ancho , lienzo_alto) ;

	const char * volcados = std :: getenv("AGUJERO_VOLCAR") ;

	const bool presentar_volcando = std :: getenv("SHD_VOLCAR_PRESENTANDO") != nullptr ;

	Lienzo lienzo ;

	Raton raton ;

	std :: uint32_t fotogramas = 0 ;

	const std :: vector<std :: uint32_t> pedidos = fotogramas_pedidos() ;

	std :: unique_ptr<Escena> escena = escena_de(cual , gpu , lienzo_ancho , lienzo_alto) ;

	while (!ventana.cerrar) {

		ventana.atender(raton) ;

		if (ventana.tecla && ventana.tecla != cual) {

			ctx->Flush() ;

			ctx->WaitForIdle() ;

			cual = ventana.tecla ;

			escena = escena_de(cual , gpu , lienzo_ancho , lienzo_alto) ;

		}

		ventana.tecla = 0 ;

		const std :: uint32_t ancho = ancho_fijo ? ancho_fijo : std :: clamp(ventana.ancho , 1u , 4096u) ;

		const std :: uint32_t alto = alto_fijo ? alto_fijo : std :: clamp(ventana.alto , 1u , 4096u) ;

		if (ventana.ancho != cadena->GetDesc().Width || ventana.alto != cadena->GetDesc().Height) { cadena->Resize(ventana.ancho , ventana.alto) ; }

		if (!lienzo.ajustar(gpu , ancho , alto , error) || !escena->avanzar(ancho , alto , raton , error)) {

			std :: fprintf(stderr , "%s\n" , error.c_str()) ;

			return 1 ;

		}

		raton.toques = raton.dobles = 0 ;

		lienzo.fijar(ctx , escena->fondo()) ;

		escena->pintar(ancho , alto) ;

		// los nodos, en los fotogramas pedidos

		if (volcados && cual == 2) {

			fotogramas++ ;

			if (std :: find(pedidos.begin() , pedidos.end() , fotogramas) != pedidos.end()) {

				lienzo.volcar(ctx , std :: string(volcados) + "/nodos-" + std :: to_string(fotogramas) + ".ppm") ;

			}

			if (fotogramas >= pedidos.back()) { break ; }

		}

		// los volcados que toquen; sin ninguno por hacer, a salir

		auto * a = dynamic_cast<EscenaAgujero *>(escena.get()) ;

		if (volcados && a) {

			shd :: agujero :: Motor & motor = a->agujero().motor() ;

			const double t = a->agujero().transcurrido() ;

			for (const rust :: String & archivo : motor.volcados_hasta(t)) {

				const std :: string ruta = std :: string(volcados) + "/" + std :: string(archivo) ;

				lienzo.volcar(ctx , ruta) ;

				std :: printf("volcado %s a los %.2f s\n" , ruta.c_str() , t) ;

			}

			if (!motor.volcados_pendientes()) { break ; }

		}

		// con volcados, solo a la textura : cada fotograma, igual en cada ejecución. Con
		// SHD_VOLCAR_PRESENTANDO, también a la ventana, como se usa de verdad

		if (volcados && !presentar_volcando) {

			ctx->Flush() ;

			ctx->WaitForIdle() ;

			continue ;

		}

		// a la ventana

		ITextureView * rtv = cadena->GetCurrentBackBufferRTV() ;

		ctx->SetRenderTargets(1 , &rtv , nullptr , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

		copia.dibujar(ctx , lienzo.imagen) ;

		cadena->Present(1) ;

	}

	ctx->Flush() ;

	ctx->WaitForIdle() ;

	return 0 ;

}
