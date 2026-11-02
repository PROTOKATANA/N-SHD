# pragma once

# include <charconv>

# include <string>

namespace shd {

// Un float para escribirlo en HLSL : la forma más corta que vuelve al mismo
// float, con punto decimal sea cual sea la configuración regional (en español,
// coma, y con to_string el shader no compila). Pieza pura

inline std :: string numero_hlsl(float v) {

	char texto[32] ;

	const auto r = std :: to_chars(texto , texto + sizeof(texto) , v) ;

	std :: string s(texto , r.ptr) ;

	if (s.find_first_of(".e") == std :: string :: npos) { s += ".0" ; }

	return s ;

}

}  // namespace shd
