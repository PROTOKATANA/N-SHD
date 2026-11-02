cmake_minimum_required(VERSION 3.25)
project(shd LANGUAGES C CXX)

# El visor: la galería en una ventana Vulkan propia, sin Flutter. Quien solo
# integra los shaders no pasa por aquí: hace add_subdirectory de nucleo/ (ver
# README). Este archivo pone lo que en una app pondría la app: DiligentCore, la
# compilación de Rust y el orden entre ambas.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

# DiligentCore v2.5.6 (solo Vulkan). Un checkout local con
# -DDILIGENT_CORE_SOURCE_DIR=...; si no, se descarga, fijado al commit (una
# etiqueta se puede mover) y con sus submódulos fijados por ese commit.
set(DILIGENT_CORE_SOURCE_DIR "" CACHE PATH "Checkout local de DiligentCore v2.5.6")
set(DILIGENT_NO_OPENGL ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_VULKAN OFF CACHE BOOL "" FORCE)
set(DILIGENT_NO_WEBGPU ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_DIRECT3D12 ON CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# DiligentCore 2.5.6 declara versiones antiguas de CMake: son avisos de terceros.
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE INTERNAL "" FORCE)
if(DILIGENT_CORE_SOURCE_DIR)
  if(NOT EXISTS "${DILIGENT_CORE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "DILIGENT_CORE_SOURCE_DIR no existe: ${DILIGENT_CORE_SOURCE_DIR}")
  endif()
  add_subdirectory("${DILIGENT_CORE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/diligent" EXCLUDE_FROM_ALL SYSTEM)
else()
  include(FetchContent)
  FetchContent_Declare(diligent_core
    GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentCore.git
    GIT_TAG b036337d68be2353c9950a85929acf796b9a6d50
    GIT_SUBMODULES_RECURSE TRUE
    SYSTEM)
  FetchContent_MakeAvailable(diligent_core)
endif()
# El glslang que empaqueta DiligentCore 2.5.6 no incluye <cstdint>.
target_compile_options(SPIRV PRIVATE -include cstdint)

# Lo que genera cxx y la biblioteca estática de Rust: los hace cargo.
set(SHD_CXX "${CMAKE_CURRENT_SOURCE_DIR}/motor/cxx")
set(SHD_RUST "${CMAKE_CURRENT_SOURCE_DIR}/target/release/libshd_visor.a")
add_custom_target(shd_rust
  COMMAND ${CMAKE_COMMAND} -E env "SHD_CXX=${SHD_CXX}" cargo build --release -p shd_visor
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  BYPRODUCTS "${SHD_RUST}"
  COMMENT "cargo: la lógica de los shaders"
  VERBATIM)

add_subdirectory(nucleo)
add_dependencies(shd_nucleo shd_rust)
target_compile_options(shd_nucleo PRIVATE -Werror)

add_subdirectory(visor)
