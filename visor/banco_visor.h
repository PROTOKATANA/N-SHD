# pragma once

# include <cstddef>

# include <cstdint>

# include <memory>

# include <string>

# include <vector>

# include "DeviceContext.h"

# include "RenderDevice.h"

# include "medidor.h"

// El banco del visor (SHD_BANCO_GPU) : lo que tarda en la GPU cada tramo que
// piden medir las pasadas, con consultas de duración. Un tramo puede abrirse
// varias veces en un fotograma (los subpasos) : se suma. Al terminar cada
// fotograma espera a la GPU, así que solo sirve para medir

class BancoVisor final : public shd :: Medidor {

public:

	BancoVisor() ;

	~BancoVisor() override ;

	BancoVisor(const BancoVisor &) = delete ;

	BancoVisor & operator=(const BancoVisor &) = delete ;

	// El nombre del tramo `pasada` en los informes

	void nombrar(std :: size_t pasada , std :: string nombre) ;

	void abrir(std :: size_t pasada , shd :: Etapa etapa , Diligent :: IRenderDevice * device , Diligent :: IDeviceContext * ctx) override ;

	void cerrar(std :: size_t pasada , shd :: Etapa etapa , Diligent :: IDeviceContext * ctx) override ;

	// Tras un fotograma : espera a la GPU y apunta lo de cada tramo

	void terminar(Diligent :: IDeviceContext * ctx) ;

	// Mínimo, mediana y p95 de cada tramo, en ms, desde el último informe

	void informar() ;

private:

	struct Impl ;

	std :: unique_ptr<Impl> impl_ ;

} ;
