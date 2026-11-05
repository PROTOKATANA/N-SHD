cmake_minimum_required(VERSION 3.25)

# La parte de GPU de los shaders. Solo necesita un dispositivo Diligent: no sabe
# nada de Flutter, de GTK ni de la ventana. Quien lo integra añade antes
# DiligentCore y dice en SHD_CXX adónde copia el build.rs de motor lo que genera
# cxx. Si cxx lo genera cargo dentro del mismo proyecto, el objetivo shd_nucleo
# debe compilar después (add_dependencies).
if(NOT SHD_CXX)
  message(FATAL_ERROR "Falta SHD_CXX: la carpeta adonde el build.rs de motor copia lo que genera cxx")
endif()
if(NOT TARGET Diligent-GraphicsEngineInterface OR NOT TARGET Diligent-Common)
  message(FATAL_ERROR "Falta DiligentCore: hay que añadirlo antes que shd_nucleo")
endif()

add_library(shd_nucleo STATIC
  "comun/computo.cc"
  "comun/copia_cpu.cc"
  "comun/pipeline_comun.cc"
  "comun/reloj.cc"
  "agujero/agujero.cc"
  "agujero/cbuffer_agujero.cc"
  "agujero/componer.cc"
  "agujero/estructuras_polvo.cc"
  "agujero/fluido_disco.cc"
  "agujero/paleta_polvo.cc"
  "agujero/pintura_disco.cc"
  "agujero/polvo_cielo.cc"
  "agujero/rayos_kerr.cc"
  "agujero/tono_gpu.cc"
  "nodos/gota_campo.cc"
  "nodos/gota_canales.cc"
  "nodos/gota_comun.cc"
  "nodos/gota_particulas.cc"
  "nodos/gota_pintura.cc"
  "nodos/gota_sueltos.cc"
  "nodos/pasada_gota.cc"
  "nodos/pasada_liquido.cc"
  "${SHD_CXX}/agujero/puente.rs.cc"
  "${SHD_CXX}/nodos/puente.rs.cc"
  "${SHD_CXX}/src/cxx.cc"
)
target_compile_features(shd_nucleo PUBLIC cxx_std_17)
target_compile_options(shd_nucleo PRIVATE -Wall -Wextra -Wpedantic "$<$<NOT:$<CONFIG:Debug>>:-O3>")
target_compile_definitions(shd_nucleo PRIVATE "$<$<NOT:$<CONFIG:Debug>>:NDEBUG>")
# el código generado no es nuestro: sus avisos no deben parar la compilación
set_source_files_properties("${SHD_CXX}/agujero/puente.rs.cc" "${SHD_CXX}/nodos/puente.rs.cc" "${SHD_CXX}/src/cxx.cc"
  PROPERTIES GENERATED TRUE COMPILE_OPTIONS "-w")
target_include_directories(shd_nucleo PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/comun"
  "${CMAKE_CURRENT_SOURCE_DIR}/agujero"
  "${CMAKE_CURRENT_SOURCE_DIR}/nodos")
target_include_directories(shd_nucleo SYSTEM PUBLIC "${SHD_CXX}")
target_link_libraries(shd_nucleo PUBLIC Diligent-GraphicsEngineInterface Diligent-Common)
