# include "computo.h"

# include <atomic>

# include <cstdint>

# include <cstdio>

# include <cstring>

# include <filesystem>

# include <fstream>

# include <iterator>

# include <string>

# include <system_error>

# include <vector>

# include "APIInfo.h"

# include "DataBlob.h"

using namespace Diligent ;

namespace shd {

namespace {

// La caché de shaders compilados : el SPIR-V de cada HLSL, en disco, para no
// compilarlo otra vez en el siguiente arranque. La clave lleva el HLSL, el
// tipo, la versión de Diligent y la de la caché : si cambia cualquiera, es
// otro archivo. En Vulkan, Diligent saca la reflexión del propio SPIR-V, así
// que el shader cargado es el mismo que el compilado

constexpr const char * version_cache = "shd-spirv-1" ;

std :: uint64_t fnv(std :: uint64_t h , const void * datos , std :: size_t n) {

	const auto * b = static_cast<const std :: uint8_t *>(datos) ;

	for (std :: size_t i = 0 ; i < n ; i++) h = (h ^ b[i]) * 1099511628211ULL ;

	return h ;

}

std :: string ruta_cache(const Gpu & gpu , const DescShader & d) {

	std :: uint64_t h = 1469598103934665603ULL ;

	h = fnv(h , version_cache , std :: strlen(version_cache)) ;

	const int api = DILIGENT_API_VERSION , tipo = static_cast<int>(d.tipo) ;

	h = fnv(h , &api , sizeof(api)) ;

	h = fnv(h , &tipo , sizeof(tipo)) ;

	h = fnv(h , d.codigo.data() , d.codigo.size()) ;

	char nombre[32] ;

	std :: snprintf(nombre , sizeof(nombre) , "%016llx.spv" , static_cast<unsigned long long>(h)) ;

	return gpu.cache + "/" + nombre ;

}

// Lo mínimo para dárselo al driver : palabras enteras, la cabecera de SPIR-V
// (5 palabras) y su número mágico. Un archivo estropeado o ajeno no pasa (se
// vuelve a compilar)
bool es_spirv(const std :: vector<char> & datos) {

	constexpr std :: uint32_t magico = 0x07230203 ;

	if (datos.size() < 5 * 4 || datos.size() % 4 != 0) return false ;

	std :: uint32_t primera = 0 ;

	std :: memcpy(&primera , datos.data() , sizeof(primera)) ;

	return primera == magico ;

}

std :: vector<char> leer(const std :: string & ruta) {

	std :: ifstream archivo(ruta , std :: ios :: binary) ;

	return { std :: istreambuf_iterator<char>(archivo) , std :: istreambuf_iterator<char>() } ;

}

// Aparte y renombrado : dos hilos o dos procesos que compilen el mismo shader
// no dejan un archivo a medias. Sin disco o sin permiso, nada : se compila
// cada vez
void guardar(const std :: string & ruta , const void * datos , Uint64 bytes) {

	static std :: atomic<std :: uint32_t> escrituras{ 0 } ;

	std :: error_code fallo ;

	const std :: filesystem :: path destino(ruta) ;

	if (std :: filesystem :: create_directories(destino.parent_path() , fallo)) {

		std :: filesystem :: permissions(destino.parent_path() , std :: filesystem :: perms :: owner_all , fallo) ;

	}

	const std :: string aparte = ruta + "." + std :: to_string(reinterpret_cast<std :: uintptr_t>(&fallo)) + "." + std :: to_string(escrituras++) ;

	{

		std :: ofstream archivo(aparte , std :: ios :: binary) ;

		archivo.write(static_cast<const char *>(datos) , static_cast<std :: streamsize>(bytes)) ;

		if (!archivo) {

			archivo.close() ;

			std :: filesystem :: remove(aparte , fallo) ;

			return ;

		}

	}

	std :: filesystem :: rename(aparte , destino , fallo) ;

	if (fallo) { std :: filesystem :: remove(aparte , fallo) ; }

}

ShaderCreateInfo base_de(const DescShader & d) {

	ShaderCreateInfo shader ;

	shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL ;

	shader.Desc.UseCombinedTextureSamplers = true ;

	shader.EntryPoint = "main" ;

	shader.Desc.Name = d.nombre ;

	shader.Desc.ShaderType = d.tipo ;

	return shader ;

}

}  // namespace

RefCntAutoPtr<IShader> gpu_compilar(const Gpu & gpu , const DescShader & d) {

	RefCntAutoPtr<IShader> s ;

	const bool con_cache = !gpu.cache.empty() ;

	const std :: string ruta = con_cache ? ruta_cache(gpu , d) : std :: string() ;

	// ya compilado en otro arranque : su SPIR-V

	if (con_cache) {

		const std :: vector<char> guardado = leer(ruta) ;

		if (es_spirv(guardado)) {

			ShaderCreateInfo desde = base_de(d) ;

			desde.ByteCode = guardado.data() ;

			desde.ByteCodeSize = guardado.size() ;

			gpu.device->CreateShader(desde , &s) ;

		}

		if (s) return s ;

		// estropeado o de otra versión : se vuelve a compilar y a guardar

		std :: error_code fallo ;

		std :: filesystem :: remove(ruta , fallo) ;

	}

	ShaderCreateInfo shader = base_de(d) ;

	shader.Source = d.codigo.c_str() ;

	RefCntAutoPtr<IDataBlob> salida ;

	gpu.device->CreateShader(shader , &s , &salida) ;

	// si no compila, lo que dijo el compilador (la primera de sus dos cadenas)

	if (!s && salida && salida->GetSize() > 0 && gpu.aviso) {

		gpu.aviso(std :: string(d.nombre) + " no compila:\n" + static_cast<const char *>(salida->GetConstDataPtr())) ;

	}

	if (!s || !con_cache) return s ;

	const void * spirv = nullptr ;

	Uint64 tamano = 0 ;

	s->GetBytecode(&spirv , tamano) ;

	if (spirv && tamano) { guardar(ruta , spirv , tamano) ; }

	return s ;

}

Kernel gpu_kernel(const Gpu & gpu , const DescKernel & d , std :: string & error) {

	Kernel k ;

	RefCntAutoPtr<IShader> cs = gpu_compilar(gpu , { SHADER_TYPE_COMPUTE , d.nombre , d.codigo }) ;

	if (!cs) {

		gpu_fallo(error , (std :: string("No se pudo compilar el kernel ") + d.nombre).c_str()) ;

		return k ;

	}

	std :: vector<ShaderResourceVariableDesc> estaticas ;

	for (const Recurso & r : d.fijos) { estaticas.emplace_back(SHADER_TYPE_COMPUTE , r.nombre , SHADER_RESOURCE_VARIABLE_TYPE_STATIC) ; }

	ComputePipelineStateCreateInfo pso ;

	pso.PSODesc.Name = d.nombre ;

	pso.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE ;

	pso.PSODesc.ResourceLayout.Variables = estaticas.data() ;

	pso.PSODesc.ResourceLayout.NumVariables = static_cast<Uint32>(estaticas.size()) ;

	pso.pCS = cs ;

	pso.PSODesc.ImmediateContextMask = gpu.mascara ;

	gpu.device->CreateComputePipelineState(pso , &k.pipeline) ;

	if (!k.pipeline) {

		gpu_fallo(error , (std :: string("No se pudo crear el pipeline ") + d.nombre).c_str()) ;

		return k ;

	}

	for (const Recurso & r : d.fijos) {

		if (auto * v = k.pipeline->GetStaticVariableByName(SHADER_TYPE_COMPUTE , r.nombre)) { v->Set(r.objeto) ; }

	}

	k.grupos_x = d.grupos_x ;

	k.grupos_y = d.grupos_y ;

	return k ;

}

RefCntAutoPtr<IShaderResourceBinding> gpu_atar(const Kernel & k , std :: initializer_list<Recurso> recursos) { return gpu_atar(k , std :: vector<Recurso>(recursos)) ; }

RefCntAutoPtr<IShaderResourceBinding> gpu_atar(const Kernel & k , const std :: vector<Recurso> & recursos) {

	RefCntAutoPtr<IShaderResourceBinding> b ;

	k.pipeline->CreateShaderResourceBinding(&b , true) ;

	for (const Recurso & r : recursos) {

		if (auto * v = b->GetVariableByName(SHADER_TYPE_COMPUTE , r.nombre)) { v->Set(r.objeto) ; }

	}

	return b ;

}

void gpu_lanzar(IDeviceContext * ctx , const Kernel & k , IShaderResourceBinding * atado) {

	ctx->SetPipelineState(k.pipeline) ;

	ctx->CommitShaderResources(atado , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	DispatchComputeAttribs d ;

	d.ThreadGroupCountX = k.grupos_x ;

	d.ThreadGroupCountY = k.grupos_y ;

	ctx->DispatchCompute(d) ;

}

void gpu_lanzar(IDeviceContext * ctx , const Kernel & k , IShaderResourceBinding * atado , std :: initializer_list<IBuffer *> escritos) {

	gpu_lanzar(ctx , k , atado) ;

	std :: vector<StateTransitionDesc> barreras ;

	for (IBuffer * e : escritos) { barreras.emplace_back(e , RESOURCE_STATE_UNORDERED_ACCESS , RESOURCE_STATE_UNORDERED_ACCESS , STATE_TRANSITION_FLAG_UPDATE_STATE) ; }

	ctx->TransitionResourceStates(static_cast<Uint32>(barreras.size()) , barreras.data()) ;

}

RefCntAutoPtr<IBuffer> gpu_campo(const Gpu & gpu , const DescCampo & d) {

	BufferDesc desc ;

	desc.Name = d.nombre ;

	desc.Usage = USAGE_DEFAULT ;

	desc.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS ;

	desc.Mode = BUFFER_MODE_STRUCTURED ;

	desc.ElementByteStride = d.paso ;

	desc.Size = d.elementos * d.paso ;

	const std :: vector<std :: uint8_t> ceros(desc.Size) ;

	BufferData datos(ceros.data() , desc.Size) ;

	RefCntAutoPtr<IBuffer> b ;

	desc.ImmediateContextMask = gpu.mascara ;

	gpu.device->CreateBuffer(desc , &datos , &b) ;

	return b ;

}

IBufferView * gpu_lectura(IBuffer * b) { return b->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) ; }

IBufferView * gpu_escritura(IBuffer * b) { return b->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS) ; }

}  // namespace shd
