# include "agujero.h"

# include <algorithm>

# include <cmath>

# include <future>

# include <utility>

# include "Buffer.h"

# include "RefCntAutoPtr.hpp"

# include "cbuffer_agujero.h"

# include "componer.h"

# include "fluido_disco.h"

# include "forma_agujero.h"

# include "polvo_cielo.h"

# include "rayos_kerr.h"

# include "tono_gpu.h"

using namespace Diligent ;

namespace shd {

namespace {

// Lo que el motor necesita de la forma, con los ángulos en radianes

agujero :: DescMotor desc_motor() {

	const CamaraForma & c = forma_agujero.camara ;

	auto radianes = [](double grados) { return grados * 3.14159265358979 / 180 ; } ;

	return { { radianes(c.inclinacion) , radianes(c.azimut) , radianes(c.sensibilidad) , c.frenado , radianes(c.deriva) , radianes(c.rumbo) } , forma_agujero.agitacion.calma , forma_agujero.agitacion.tope } ;

}

// Lo que vive en un dispositivo : el cbuffer, el fluido del disco, el polvo,
// los rayos, la composición y el tono

struct Piezas {

	Gpu gpu ;

	RefCntAutoPtr<IBuffer> uniforme ;

	FluidoDisco fluido ;

	PolvoCielo polvo{ forma_agujero.polvo } ;

	RayosKerr rayos ;

	Componer componer ;

	TonoGpu tono ;

	bool crear(const Gpu & prestada , std :: string & error) {

		gpu = prestada ;

		BufferDesc desc ;

		desc.Name = "Agujero uniforms" ;

		desc.Size = sizeof(AgujeroGpu) ;

		desc.Usage = USAGE_DEFAULT ;

		desc.BindFlags = BIND_UNIFORM_BUFFER ;

		desc.ImmediateContextMask = gpu.mascara ;

		gpu.device->CreateBuffer(desc , nullptr , &uniforme) ;

		if (!uniforme) { return gpu_fallo(error , "No se pudo crear el uniform del agujero") ; }

		// las piezas se crean a la vez, en 3 hilos más el de aquí : casi todo
		// es compilar sus kernels (~250 ms en serie, medido) y el dispositivo
		// de Diligent admite crear desde varios hilos. Componer solo lee las
		// definiciones de los campos (texto, de los parámetros) y los ata al
		// dibujar : también va a la vez, aquí, con el tono

		std :: string error_fluido , error_polvo , error_rayos ;

		auto fluido_listo = std :: async(std :: launch :: async , [&] { return fluido.crear(gpu , error_fluido) ; }) ;

		auto polvo_listo = std :: async(std :: launch :: async , [&] { return polvo.crear(gpu , error_polvo) ; }) ;

		auto rayos_listo = std :: async(std :: launch :: async , [&] { return rayos.crear(gpu , uniforme , error_rayos) ; }) ;

		const bool aqui_bien = componer.crear(gpu , uniforme , fluido , polvo , error) && tono.crear(gpu , forma_agujero.luz.exposicion , error) ;

		// los tres, esperados siempre (antes de mirar sus errores)

		const bool fluido_bien = fluido_listo.get() , polvo_bien = polvo_listo.get() , rayos_bien = rayos_listo.get() ;

		// el primer error, a quien lo pidió

		for (const std :: string & e : { error_fluido , error_polvo , error_rayos }) {

			if (!e.empty()) { gpu_fallo(error , e.c_str()) ; }

		}

		return aqui_bien && fluido_bien && polvo_bien && rayos_bien ;

	}

} ;

}  // namespace

struct Agujero :: Impl {

	Reloj reloj ;

	// La órbita, lo agitado y los ensayos sin ratón son de Rust (ver motor.rs)

	rust :: Box<agujero :: Motor> motor = agujero :: motor_nuevo(desc_motor()) ;

	std :: unique_ptr<Piezas> piezas ;

	Medidor * medidor = nullptr ;

	AgujeroGpu cbuffer{} ;

	double origen = -1 ;

	double anterior = 0 ;

	explicit Impl(Reloj r) : reloj(std :: move(r)) {}

	double transcurrido() const { return origen < 0 ? 0 : anterior - origen ; }

	// Detrás del agujero, desde la cámara (cbuffer.camara, ya rellenado) : en
	// el sistema del cielo (los rayos se trazan con azimut 0; componer gira el
	// cielo con el azimut)
	void centro_del_cielo(float (& centro)[3]) const {

		const double cx = -cbuffer.camara[0] , cy = -cbuffer.camara[1] , cz = -cbuffer.camara[2] ;

		const double n = std :: sqrt(cx * cx + cy * cy + cz * cz) , az = cbuffer.orbita[0] ;

		centro[0] = static_cast<float>((cx * std :: cos(az) - cy * std :: sin(az)) / n) ;

		centro[1] = static_cast<float>((cx * std :: sin(az) + cy * std :: cos(az)) / n) ;

		centro[2] = static_cast<float>(cz / n) ;

	}

} ;

Agujero :: Agujero(Reloj reloj) : impl_(std :: make_unique<Impl>(std :: move(reloj))) {}

Agujero :: ~Agujero() = default ;

std :: vector<std :: string> Agujero :: pasadas() {

	return { "rayos" , "fluido" , "componer" , "tono" , "fluido_advectar" , "fluido_divergir" , "fluido_jacobi" , "fluido_proyectar" , "fluido_transportar" , "rayos_trazar" , "rayos_detectar" , "rayos_bordes" , "polvo" , "polvo_mover" , "polvo_prefijos" , "polvo_repartir" } ;

}

void Agujero :: medir(Medidor * medidor) { impl_->medidor = medidor ; }

bool Agujero :: crear(const Gpu & gpu , std :: string & error) {

	if (impl_->piezas && impl_->piezas->gpu.device == gpu.device) { return true ; }

	auto piezas = std :: make_unique<Piezas>() ;

	if (!piezas->crear(gpu , error)) { return false ; }

	// cada kernel del fluido y de los rayos en su pasada (las de después de las
	// cuatro grandes)

	piezas->fluido.medir(impl_->medidor , 4) ;

	piezas->rayos.medir(impl_->medidor , 9) ;

	piezas->polvo.medir(impl_->medidor , 13) ;

	impl_->piezas = std :: move(piezas) ;

	return true ;

}

bool Agujero :: ajustar(std :: uint32_t ancho , std :: uint32_t alto , std :: string & error) { return impl_->piezas->componer.ajustar(ancho , alto , error) ; }

bool Agujero :: asentar(std :: uint32_t ancho , std :: uint32_t alto) {

	Impl & a = * impl_ ;

	Piezas & p = * a.piezas ;

	const double ahora = a.reloj() ;

	if (a.origen < 0) { a.origen = a.anterior = ahora ; }

	// la cámara de arranque (el tamaño aún no importa : solo la cámara)

	const agujero :: Fotograma f = a.motor->fotograma() ;

	rellenar_agujero({ a.transcurrido() , 0 , std :: max(ancho , 1u) , std :: max(alto , 1u) , f.base_trazado , f.azimut , forma_agujero.camara.reduccion , 0 } , a.cbuffer) ;

	IDeviceContext * ctx = p.gpu.ctx ;

	ctx->UpdateBuffer(p.uniforme , 0 , sizeof(a.cbuffer) , &a.cbuffer , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	p.fluido.agitar(static_cast<float>(f.agitado) , f.dobles_clics) ;

	p.fluido.avanzar(ctx , 0 , a.cbuffer.tiempo[1]) ;

	float centro[3] ;

	a.centro_del_cielo(centro) ;

	p.polvo.avanzar(ctx , 0 , a.cbuffer.tiempo[1] , centro) ;

	ctx->Flush() ;

	return true ;

}

bool Agujero :: avanzar(std :: uint32_t ancho , std :: uint32_t alto , std :: string & error) {

	Impl & a = * impl_ ;

	Piezas & p = * a.piezas ;

	// solo aquí, ya con imagen : con el reloj de pasos, un intento sin imagen
	// no se come un paso y los volcados salen iguales

	const double ahora = a.reloj() ;

	// los ensayos sin ratón miran el segundo del fotograma anterior

	const double antes = a.transcurrido() ;

	if (a.origen < 0) { a.origen = a.anterior = ahora ; }

	const double paso = std :: clamp(ahora - a.anterior , 0.0 , static_cast<double>(forma_agujero.paso_max)) ;

	a.anterior = std :: max(a.anterior , ahora) ;

	const agujero :: Fotograma f = a.motor->avanzar(antes , paso) ;

	p.fluido.agitar(static_cast<float>(f.agitado) , f.dobles_clics) ;

	const std :: uint32_t r = forma_agujero.camara.reduccion ;

	rellenar_agujero({ a.transcurrido() , paso , ancho , alto , f.base_trazado , f.azimut , r , 0 } , a.cbuffer) ;

	IDeviceContext * ctx = p.gpu.ctx ;

	IRenderDevice * device = p.gpu.device ;

	ctx->UpdateBuffer(p.uniforme , 0 , sizeof(a.cbuffer) , &a.cbuffer , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	// los rayos : solo si cambió el tamaño (la cámara es fija)

	bool trazado = false ;

	{

		Tramo tramo(a.medidor , 0 , Etapa :: dibujar , device , ctx) ;

		if (!p.rayos.actualizar(ctx , ancho , alto , f.moviendose , forma_agujero.camara.filas_por_fotograma , r , &trazado , error)) { return false ; }

	}

	// las filas listas del mapa alto, ya trazadas : para componer

	a.cbuffer.orbita[2] = static_cast<float>(p.rayos.filas_listas()) ;

	a.cbuffer.orbita[3] = static_cast<float>(p.rayos.bordes_de()) ;

	ctx->UpdateBuffer(p.uniforme , 0 , sizeof(a.cbuffer) , &a.cbuffer , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	{

		Tramo tramo(a.medidor , 1 , Etapa :: dibujar , device , ctx) ;

		p.fluido.avanzar(ctx , static_cast<float>(paso) , a.cbuffer.tiempo[1]) ;

	}

	{

		Tramo tramo(a.medidor , 12 , Etapa :: dibujar , device , ctx) ;

		float centro[3] ;

		a.centro_del_cielo(centro) ;

		p.polvo.avanzar(ctx , static_cast<float>(paso) , a.cbuffer.tiempo[1] , centro) ;

	}

	{

		Tramo tramo(a.medidor , 2 , Etapa :: dibujar , device , ctx) ;

		p.componer.dibujar(ctx , p.rayos.mapa_alto() , p.rayos.mapa_bajo() , p.rayos.indice_bordes() , p.rayos.bordes()) ;

	}

	return true ;

}

void Agujero :: pintar(std :: uint32_t ancho , std :: uint32_t alto) {

	Piezas & p = * impl_->piezas ;

	Tramo tramo(impl_->medidor , 3 , Etapa :: dibujar , p.gpu.device , p.gpu.ctx) ;

	p.tono.dibujar(p.gpu.ctx , p.componer.acumulado() , ancho , alto) ;

}

double Agujero :: transcurrido() const { return impl_->transcurrido() ; }

agujero :: Motor & Agujero :: motor() { return * impl_->motor ; }

}  // namespace shd
