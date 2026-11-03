# include "banco_visor.h"

# include <algorithm>

# include <array>

# include <cstdio>

# include "Query.h"

# include "RefCntAutoPtr.hpp"

using namespace Diligent ;

namespace {

constexpr const char * nombres_etapa[] = { "preparar" , "subir" , "dibujar" } ;

// Las consultas de un tramo en un fotograma : una por cada vez que se abre

struct Tramo {

	std :: vector<RefCntAutoPtr<IQuery>> consultas ;

	std :: size_t usadas = 0 ;

	std :: vector<double> muestras ;

} ;

double cuantil(std :: vector<double> & v , double q) {

	std :: sort(v.begin() , v.end()) ;

	return v[std :: min(v.size() - 1 , static_cast<std :: size_t>(q * static_cast<double>(v.size())))] ;

}

}  // namespace

struct BancoVisor :: Impl {

	std :: vector<std :: string> nombres ;

	std :: vector<std :: array<Tramo , 3>> tramos ;

	Tramo & de(std :: size_t pasada , shd :: Etapa etapa) {

		if (pasada >= tramos.size()) { tramos.resize(pasada + 1) ; }

		return tramos[pasada][static_cast<std :: size_t>(etapa)] ;

	}

} ;

BancoVisor :: BancoVisor() : impl_(std :: make_unique<Impl>()) {}

BancoVisor :: ~BancoVisor() = default ;

void BancoVisor :: nombrar(std :: size_t pasada , std :: string nombre) {

	if (pasada >= impl_->nombres.size()) { impl_->nombres.resize(pasada + 1) ; }

	impl_->nombres[pasada] = std :: move(nombre) ;

}

void BancoVisor :: abrir(std :: size_t pasada , shd :: Etapa etapa , IRenderDevice * device , IDeviceContext * ctx) {

	if (!ctx || !device) { return ; }

	Tramo & t = impl_->de(pasada , etapa) ;

	if (t.usadas == t.consultas.size()) {

		QueryDesc d ;

		d.Name = "Visor banco" ;

		d.Type = QUERY_TYPE_DURATION ;

		RefCntAutoPtr<IQuery> q ;

		device->CreateQuery(d , &q) ;

		if (!q) { return ; }

		t.consultas.push_back(q) ;

	}

	ctx->BeginQuery(t.consultas[t.usadas]) ;

}

void BancoVisor :: cerrar(std :: size_t pasada , shd :: Etapa etapa , IDeviceContext * ctx) {

	if (!ctx) { return ; }

	Tramo & t = impl_->de(pasada , etapa) ;

	if (t.usadas == t.consultas.size()) { return ; }

	ctx->EndQuery(t.consultas[t.usadas]) ;

	t.usadas++ ;

}

void BancoVisor :: terminar(IDeviceContext * ctx) {

	ctx->WaitForIdle() ;

	for (auto & etapas : impl_->tramos) {

		for (Tramo & t : etapas) {

			if (!t.usadas) { continue ; }

			double ms = 0 ;

			for (std :: size_t k = 0 ; k < t.usadas ; k++) {

				QueryDataDuration d ;

				if (t.consultas[k]->GetData(&d , sizeof(d)) && d.Frequency) {

					ms += static_cast<double>(d.Duration) * 1e3 / static_cast<double>(d.Frequency) ;

				}

			}

			t.muestras.push_back(ms) ;

			t.usadas = 0 ;

		}

	}

}

void BancoVisor :: informar() {

	for (std :: size_t i = 0 ; i < impl_->tramos.size() ; i++) {

		for (std :: size_t e = 0 ; e < 3 ; e++) {

			Tramo & t = impl_->tramos[i][e] ;

			if (t.muestras.empty()) { continue ; }

			const std :: string nombre = i < impl_->nombres.size() ? impl_->nombres[i] : std :: to_string(i) ;

			std :: printf(

				"BANCO %-28s %-8s %5zu fotogramas  mín %.3f  mediana %.3f  p95 %.3f ms\n" ,

				nombre.c_str() ,

				nombres_etapa[e] ,

				t.muestras.size() ,

				cuantil(t.muestras , 0) ,

				cuantil(t.muestras , 0.5) ,

				cuantil(t.muestras , 0.95)

			) ;

			t.muestras.clear() ;

		}

	}

	std :: fflush(stdout) ;

}
