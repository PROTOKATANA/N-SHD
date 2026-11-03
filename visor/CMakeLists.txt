cmake_minimum_required(VERSION 3.25)

# La ventana es XCB (también bajo XWayland), sin bibliotecas de ventanas: lo que
# ya pide el swapchain de Diligent en Linux.
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)

add_executable(shd_visor
  "visor.cc"
  "banco_visor.cc"
)
target_compile_options(shd_visor PRIVATE -Wall -Wextra -Wpedantic -Werror "$<$<NOT:$<CONFIG:Debug>>:-O3>")
target_compile_definitions(shd_visor PRIVATE "$<$<NOT:$<CONFIG:Debug>>:NDEBUG>")
add_dependencies(shd_visor shd_rust)
target_link_libraries(shd_visor PRIVATE
  shd_nucleo
  "${SHD_RUST}"
  Diligent-GraphicsEngineVk-static
  Vulkan::Vulkan
  PkgConfig::XCB
  Threads::Threads
  ${CMAKE_DL_LIBS}
)
