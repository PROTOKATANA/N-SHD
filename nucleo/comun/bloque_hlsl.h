# pragma once

// Iterar un estencil en bloques : cada grupo carga su bloque de T_B × T_B celdas con un
// borde de K en memoria compartida, hace ahí K iteraciones (el borde se va gastando, una
// celda por iteración) y escribe solo el centro. Si cada celda hace las mismas operaciones en
// el mismo orden que en una pasada por iteración, el resultado es el mismo bit a bit, con K
// veces menos lanzamientos. Quien lo incluye define K y T_B antes; FUERA marca, en la
// máscara del bloque, lo que cae fuera de la rejilla

namespace shd {

inline constexpr const char * bloque_hlsl = R"(
static const int T = T_B;
static const uint HILOS_B = uint(T_B * T_B);
static const int R = T + 2 * K;
static const uint CELDAS_B = uint(R * R);
static const uint POR_HILO = (CELDAS_B + HILOS_B - 1u) / HILOS_B;
static const uint FUERA = 3u;
// la celda de la rejilla de un puesto del bloque, y el puesto de una celda vecina
int2 celda_b(int2 origen, uint s) { return origen + int2(int(s % uint(R)), int(s / uint(R))); }
uint puesto_b(uint s, int x, int y) { return uint(int(s) + y * R + x); }
// el anillo de fuera no se calcula : sus vecinas no están en el bloque
bool interior_b(uint s) { int lx = int(s % uint(R)), ly = int(s / uint(R)); return lx > 0 && ly > 0 && lx < R - 1 && ly < R - 1; }
)" ;

}  // namespace shd
