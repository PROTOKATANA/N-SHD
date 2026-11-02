#!/bin/bash
# Compila y corre las pruebas de las piezas de C++ que no necesitan GPU, con los
# mismos avisos que el núcleo : todo aviso es un error. Con SANITIZAR=1, además
# con AddressSanitizer y UBSan.
set -euo pipefail
aqui="$(cd "$(dirname "$0")" && pwd)"
raiz="$aqui/.."
salida="$(mktemp -d)"
trap 'rm -rf "$salida"' EXIT
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I"$raiz" \
  "$aqui/pruebas.cc" "$raiz/pintura_disco.cc" "$raiz/paleta_polvo.cc" \
  ${SANITIZAR:+-fsanitize=address,undefined -fno-sanitize-recover=all} \
  -o "$salida/pruebas"
"$salida/pruebas"
