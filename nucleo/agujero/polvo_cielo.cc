# include "polvo_cielo.h"

# include "estructuras_polvo.h"

# include "paleta_polvo.h"

# include <algorithm>

# include <array>

# include <cmath>

# include <string>

# include <vector>

# include "gpu.h"

# include "numero_hlsl.h"

using namespace Diligent ;

namespace shd {

namespace {

// Los parches donde nace cada partícula (ver al_azar) : 64 bandas × 128
// sectores de la misma área

constexpr Uint32 bandas = 64 , sectores = 128 ;

// La pantalla eleva el valor de cada píxel a esta potencia para dar luz
// (sRGB, más o menos)

constexpr float gamma_pantalla = 2.2f ;

// La suma de prefijos, por bloques de 1024 celdas (y una más : el total)

constexpr Uint32 bloque = 1024 ;

// El cbuffer «Polvo»

struct PolvoGpu {

	float paso[4] ;  // dt (s) , t (s) , 0 , 0

	float flujo[4] ;  // frecuencia (celdas por radián) , cambio , velocidad , la cohesión de las hebras

	float vida[4] ;  // vida (s) , vida extra al azar (s) , fundido (s) , 0

	float centro[4] ;  // el centro del casquete (xyz, unitario) , cos(radio)

	float eje_u[4] ;  // su base en el plano tangente (xyz) , tan(radio)

	float eje_v[4] ;  // (xyz) , 0

	float antes[4] ;  // el centro del casquete en el fotograma anterior (xyz) , 0

	float hebra[4] ;  // la vida de una hebra (s) , vida extra al azar (s) , fundido (s) , la luz de sus partículas (en veces la del polvo suelto)

	float cumulo[4] ;  // la vida de un cúmulo (s) , vida extra al azar (s) , fundido (s) , la luz de sus partículas

} ;

constexpr const char * comun_hlsl = R"(
cbuffer Polvo {
  float4 paso;    // dt (s), t (s), 0, 0
  float4 flujo;   // frecuencia (celdas por radián), cambio, velocidad, la cohesión de las hebras
  float4 vida;    // vida (s), vida extra al azar (s), fundido (s), 0
  float4 centro;  // el centro del casquete (xyz, unitario), cos(radio)
  float4 eje_u;   // su base en el plano tangente (xyz), tan(radio)
  float4 eje_v;   // (xyz), 0
  float4 antes;   // el centro del casquete en el fotograma anterior (xyz), 0
  float4 hebra;   // la vida de una hebra (s), vida extra al azar (s), fundido (s), la luz de sus partículas
  float4 cumulo;  // la vida de un cúmulo (s), vida extra al azar (s), fundido (s), la luz de sus partículas
};

static const float TAU = 6.2831853;

static const uint SIN_CELDA = 0xFFFFFFFFu;

uint mezclar(uint x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

float azar(uint x) { return float(mezclar(x) >> 8) * (1.0 / 16777216.0); }

// Una dirección del casquete, de su z (el coseno del ángulo al centro) y su
// ángulo alrededor del centro
float3 en_casquete(float z, float a) {
  float r = sqrt(max(1.0 - z * z, 0.0));
  return centro.xyz * z + (eje_u.xyz * cos(a) + eje_v.xyz * sin(a)) * r;
}

// Una dirección al azar en el parche de la partícula i. El casquete se parte
// en BANDAS × SECTORES parches de la misma área (z y el ángulo por igual :
// Arquímedes) y cada partícula nace y renace en el suyo : el polvo sigue
// repartido por igual, y las partículas seguidas caen cerca en el cielo (sus
// sumas tocan la misma zona de memoria)
float3 al_azar(uint i, uint s) {
  uint parche = min(i / POR_PARCHE, BANDAS * SECTORES - 1u);
  float banda = float(parche / SECTORES), sector = float(parche % SECTORES);
  float z = 1.0 - (1.0 - centro.w) * (banda + azar(s)) / float(BANDAS);
  return en_casquete(z, TAU * (sector + azar(s + 1u)) / float(SECTORES));
}

)" ;

// La celda de una dirección : una rejilla de CELDAS_LADO × CELDAS_LADO en
// el plano tangente al centro del casquete (proyección gnomónica, de −tan R
// a tan R); fuera del casquete, ninguna
constexpr const char * celda_hlsl = R"(
uint celda_casquete(float3 x) {
  float z = dot(x, centro.xyz);
  if (z < centro.w) return 0xFFFFFFFFu;
  float2 uv = float2(dot(x, eje_u.xyz), dot(x, eje_v.xyz)) / (z * eje_u.w);
  uint2 c = min(uint2(saturate(uv * 0.5 + 0.5) * float(CELDAS_LADO)), uint2(CELDAS_LADO - 1u, CELDAS_LADO - 1u));
  return c.y * CELDAS_LADO + c.x;
}
)" ;

constexpr const char * sembrar_hlsl = R"(
RWStructuredBuffer<float4> estado;

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint i = id.x;
  if (i >= PARTICULAS) return;
  float3 x = al_azar(i, i * 11u + 101u);
  float ciclo, edad, dura;
  if (de_hebra(i)) {
    vida_estructura(hebra_de(i), paso.y, ciclo, edad, dura);
    x = en_hebra(i, hebra_de(i), uint(ciclo));
  } else if (de_cumulo(i)) {
    vida_estructura(CURVAS + cumulo_de(i), paso.y, ciclo, edad, dura);
    x = en_cumulo(i, cumulo_de(i), uint(ciclo));
  }
  estado[i] = float4(x, 0.0);
}
)" ;

constexpr const char * limpiar_hlsl = R"(
RWStructuredBuffer<uint> cuentas;

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x < ESCANEADAS) cuentas[id.x] = 0u;
}
)" ;

// El ruido 3D del flujo, con su gradiente

constexpr const char * ruido_hlsl = R"(
// El gradiente de una esquina de la red : un vector al azar en el cubo
// [−1, 1]³, de los bits de su hash
float3 gradiente3(int3 c, uint s) {
  uint h = mezclar((uint(c.x) * 73856093u) ^ (uint(c.y) * 19349663u) ^ (uint(c.z) * 83492791u) ^ (s * 2654435761u));
  return float3(uint3(h, h >> 10, h >> 20) & 1023u) * (2.0 / 1023.0) - 1.0;
}

// Ruido de gradiente 3D y su gradiente, a la vez (x : el valor, yzw : ∇)
float4 ruido3(float3 p, uint s) {
  float3 b = floor(p);
  int3 i = int3(b);
  float3 f = p - b;
  float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  float3 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
  float3 ga = gradiente3(i, s), gb = gradiente3(i + int3(1, 0, 0), s), gc = gradiente3(i + int3(0, 1, 0), s), gd = gradiente3(i + int3(1, 1, 0), s);
  float3 ge = gradiente3(i + int3(0, 0, 1), s), gf = gradiente3(i + int3(1, 0, 1), s), gg = gradiente3(i + int3(0, 1, 1), s), gh = gradiente3(i + int3(1, 1, 1), s);
  float va = dot(ga, f), vb = dot(gb, f - float3(1, 0, 0)), vc = dot(gc, f - float3(0, 1, 0)), vd = dot(gd, f - float3(1, 1, 0));
  float ve = dot(ge, f - float3(0, 0, 1)), vf = dot(gf, f - float3(1, 0, 1)), vg = dot(gg, f - float3(0, 1, 1)), vh = dot(gh, f - float3(1, 1, 1));
  float k1 = vb - va, k2 = vc - va, k3 = ve - va, k4 = va - vb - vc + vd, k5 = va - vc - ve + vg, k6 = va - vb - ve + vf, k7 = -va + vb + vc - vd + ve - vf - vg + vh;
  float valor = va + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z + k6 * u.z * u.x + k7 * u.x * u.y * u.z;
  float3 grad = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.z * (ge - ga) + u.x * u.y * (ga - gb - gc + gd) + u.y * u.z * (ga - gc - ge + gg) + u.z * u.x * (ga - gb - ge + gf) + u.x * u.y * u.z * (-ga + gb + gc - gd + ge - gf - gg + gh)
    + du * float3(k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z, k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x, k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y);
  return float4(valor, grad);
}

// el gradiente del potencial, en unidades de celda del ruido (q = x ·
// frecuencia) : dos escalas que se deslizan
float3 gradiente_potencial(float3 q, float t) {
  return ruido3(q + t * float3(1.0, 0.7, 0.3), 1u).yzw + 0.5 * 2.3 * ruido3(2.3 * q - t * float3(0.4, 1.0, 0.6), 2u).yzw;
}

)" ;

// Mover : la vida (con su fundido) y renacer; el flujo; y contarla en su
// celda (las apagadas, no)

constexpr const char * mover_hlsl = R"(
RWStructuredBuffer<float4> estado;
RWStructuredBuffer<uint2> en_celda;
RWStructuredBuffer<uint> cuentas;
)" ;

// Si x sale del casquete : si es porque la cámara se ha movido, pasa a la
// parte que acaba de quedar a la vista : en coordenadas del casquete (u en el
// sentido del movimiento, v de lado, en ángulo desde el centro), la franja
// que se pierde por detrás a la altura v es la que aparece por delante
// corrida el ancho del casquete a esa altura, 2·√(R² − v²); si la cámara
// saltó más de medio radio, al azar en la parte nueva. Si sale por el flujo
// (o no cabe así), entra por el otro lado. Con el radio que se le dé : el
// del casquete para las sueltas; para el centro de una hebra o un cúmulo,
// más lo que mide

constexpr const char * casquete_hlsl = R"(
// el radio del casquete (rad)
float radio_casquete() { return acos(clamp(centro.w, -1.0, 1.0)); }

float3 al_casquete(float3 x, uint semilla, float radio) {
  float cos_r = cos(radio);
  float z = dot(x, centro.xyz);
  bool fuera = z < cos_r;
  float movido = acos(clamp(dot(antes.xyz, centro.xyz), -1.0, 1.0));
  if (fuera && movido > 1e-6) {
    if (movido > 0.5 * radio) {
      // al azar en la parte nueva (la que no estaba en el casquete de antes :
      // con un salto así, más de un tercio del casquete)
      [loop] for (uint intento = 0u; intento < 16u && fuera; ++intento) {
        uint s = semilla + 50u + intento * 31u;
        float3 nueva = en_casquete(1.0 - (1.0 - cos_r) * azar(s), TAU * azar(s + 1u));
        if (dot(nueva, antes.xyz) < cos_r) { x = nueva; fuera = false; }
      }
    } else {
      float3 m = normalize(centro.xyz * dot(centro.xyz, antes.xyz) - antes.xyz);
      float3 w = cross(centro.xyz, m);
      float th = acos(clamp(z, -1.0, 1.0));
      float3 lado = x - centro.xyz * z;
      float l = length(lado);
      if (l > 1e-6) {
        float u = th * dot(lado, m) / l, v = th * dot(lado, w) / l;
        if (u < 0.0 && abs(v) < radio) {
          u += 2.0 * sqrt(radio * radio - v * v);
          float th2 = sqrt(u * u + v * v);
          if (th2 <= radio && th2 > 1e-6) {
            x = centro.xyz * cos(th2) + (m * u + w * v) / th2 * sin(th2);
            fuera = false;
          }
        }
      }
    }
  }
  if (fuera) {
    float3 lado = x - centro.xyz * z;
    float l = length(lado);
    float3 t_ = l > 1e-6 ? -lado / l : eje_u.xyz;
    // reflejada en el borde : lo que se pasó, hacia dentro desde el otro
    // lado (con módulo : si la cámara saltó lejos, a una distancia repartida
    // dentro del casquete)
    float dentro = radio - fmod(acos(clamp(z, -1.0, 1.0)) - radio, radio);
    x = centro.xyz * cos(dentro) + t_ * sin(dentro);
  }
  return x;
}

// la velocidad del flujo en x (rad/s, tangente)
float3 flujo_en(float3 x, float t) { return flujo.z * cross(x, gradiente_potencial(x * flujo.x, t * flujo.y)); }
)" ;

constexpr const char * mover_main_hlsl = R"(
StructuredBuffer<float4> centros;
RWStructuredBuffer<uint> alcance;

// la velocidad de la estructura s, llevada al plano tangente en x
float3 del_centro(uint s, float3 x) {
  float3 v = centros[3u * s + 1u].xyz;
  return v - x * dot(x, v);
}

// si el centro de la estructura s saltó al otro lado del casquete, la
// partícula x da el mismo salto (el giro que lleva de donde estaba a donde
// está) : la estructura pasa entera, sin trocearse
float3 con_su_salto(uint s, float3 x) {
  float4 de = centros[3u * s + 2u];
  if (de.w < 0.5) return x;
  float3 a = centros[3u * s].xyz;
  float3 eje = cross(de.xyz, a);
  float l = length(eje);
  if (l < 1e-7) return x;
  return normalize(rotar(x, eje / l, atan2(l, dot(de.xyz, a))));
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint i = id.x;
  if (i >= PARTICULAS) return;
  float3 x = estado[i].xyz;
  float dt = paso.x, t = paso.y;
  float brillo;
  float ciclo;
  float3 v;
  uint estructura = 0xFFFFFFFFu;
  if (de_hebra(i)) {
    // la de una hebra vive con ella (renace con ella en otro sitio) y se
    // mueve sobre todo con su centro : se dobla, pero no se deshilacha
    uint h = hebra_de(i);
    float edad, dura, ciclo_antes, edad_antes, dura_antes;
    vida_estructura(h, t, ciclo, edad, dura);
    vida_estructura(h, t - dt, ciclo_antes, edad_antes, dura_antes);
    if (ciclo != ciclo_antes) x = en_hebra(i, h, uint(ciclo)); else x = con_su_salto(h, x);
    estructura = h;
    // todas con la misma luz (las sueltas, cada una la suya : ver abajo)
    brillo = en_su_hebra(i, h, uint(ciclo)) ? hebra.w * LUZ_IGUAL * smoothstep(0.0, hebra.z, edad) * smoothstep(0.0, hebra.z, dura - edad) : 0.0;
    v = lerp(flujo_en(x, t), del_centro(h, x), flujo.w);
  } else if (de_cumulo(i)) {
    // la de un cúmulo (una estrella lejana), igual, pero va rígida con su
    // centro : el cúmulo nunca se deshace
    uint k = cumulo_de(i), s = CURVAS + k;
    float edad, dura, ciclo_antes, edad_antes, dura_antes;
    vida_estructura(s, t, ciclo, edad, dura);
    vida_estructura(s, t - dt, ciclo_antes, edad_antes, dura_antes);
    if (ciclo != ciclo_antes) x = en_cumulo(i, k, uint(ciclo)); else x = con_su_salto(s, x);
    estructura = s;
    brillo = cumulo.w * LUZ_IGUAL * smoothstep(0.0, cumulo.z, edad) * smoothstep(0.0, cumulo.z, dura - edad);
    v = del_centro(s, x);
  } else {
    // la suelta vive su vida y renace en otro sitio de su parche
    float dura = vida.x + vida.y * azar(i * 11u + 1u);
    float desfase = azar(i * 11u + 2u) * dura;
    ciclo = floor((t + desfase) / dura);
    if (ciclo != floor((t - dt + desfase) / dura)) x = al_azar(i, i * 11u + uint(ciclo) * 7919u + 3u);
    float edad = (t + desfase) - ciclo * dura;
    brillo = smoothstep(0.0, vida.z, edad) * smoothstep(0.0, vida.z, dura - edad) * (0.55 + 0.45 * azar(i * 13u + 17u));
    v = flujo_en(x, t);
  }
  // las sueltas que salen del casquete pasan al otro lado (ver al_casquete);
  // las de una hebra o un cúmulo, no (pasan con su centro) : la que asoma
  // por fuera no se pinta (celda_casquete) y la hebra no se trocea
  x = normalize(x + v * dt);
  if (estructura == 0xFFFFFFFFu) x = al_casquete(x, i * 11u + uint(ciclo) * 7919u, radio_casquete());
  // lo que mide su estructura : hasta dónde llegan sus partículas visibles
  // desde el centro (ver centros)
  else if (brillo > 0.0) InterlockedMax(alcance[estructura], asuint(acos(clamp(dot(x, centros[3u * estructura].xyz), -1.0, 1.0))));
  // su brillo, en luz lineal (la pantalla eleva el valor a GAMMA) : así se
  // reparte entre píxeles sin perder luz (ver color_del_cielo)
  estado[i] = float4(x, pow(brillo, GAMMA));
  if (brillo <= 0.0) { en_celda[i] = uint2(SIN_CELDA, 0u); return; }
  uint c = celda_casquete(x);
  if (c == SIN_CELDA) { en_celda[i] = uint2(SIN_CELDA, 0u); return; }
  uint puesto;
  InterlockedAdd(cuentas[c], 1u, puesto);
  en_celda[i] = uint2(c, puesto);
}
)" ;

// La suma de prefijos (exclusiva) de las cuentas, por bloques de 1024 (la
// de Blelloch en memoria compartida : subir y bajar por el árbol). Escribe
// el inicio de cada celda dentro de su bloque y el total del bloque

constexpr const char * escanear_hlsl = R"(
RWStructuredBuffer<uint> entrada;
RWStructuredBuffer<uint> salida;
RWStructuredBuffer<uint> totales;
groupshared uint arbol[1024];

[numthreads(512, 1, 1)]
void main(uint3 grupo : SV_GroupID, uint t : SV_GroupIndex) {
  uint base = grupo.x * 1024u;
  arbol[2u * t] = base + 2u * t < N ? entrada[base + 2u * t] : 0u;
  arbol[2u * t + 1u] = base + 2u * t + 1u < N ? entrada[base + 2u * t + 1u] : 0u;
  uint salto = 1u;
  for (uint d = 512u; d > 0u; d >>= 1) {
    GroupMemoryBarrierWithGroupSync();
    if (t < d) {
      uint a = salto * (2u * t + 1u) - 1u, b = salto * (2u * t + 2u) - 1u;
      arbol[b] += arbol[a];
    }
    salto <<= 1;
  }
  GroupMemoryBarrierWithGroupSync();
  if (t == 0u) { totales[grupo.x] = arbol[1023]; arbol[1023] = 0u; }
  for (uint d2 = 1u; d2 < 1024u; d2 <<= 1) {
    salto >>= 1;
    GroupMemoryBarrierWithGroupSync();
    if (t < d2) {
      uint a = salto * (2u * t + 1u) - 1u, b = salto * (2u * t + 2u) - 1u;
      uint x = arbol[a];
      arbol[a] = arbol[b];
      arbol[b] += x;
    }
  }
  GroupMemoryBarrierWithGroupSync();
  if (base + 2u * t < N) salida[base + 2u * t] = arbol[2u * t];
  if (base + 2u * t + 1u < N) salida[base + 2u * t + 1u] = arbol[2u * t + 1u];
}
)" ;

// Repartir : cada partícula, a su puesto en su celda (el inicio de la celda
// es el de dentro de su bloque más el prefijo del bloque)

constexpr const char * repartir_hlsl = R"(
// El color de la partícula i (su índice en PALETA) : las de hebra, el
// blanco; las de un cúmulo, el de su cúmulo en esta vida (todas igual : una
// estrella de un color); las sueltas, el suyo. Al azar con los pesos
uint color_de(uint i) {
  if (de_hebra(i)) return 0u;
  uint s = i * 2246822519u + 7u;
  if (de_cumulo(i)) {
    float ciclo, edad, dura;
    vida_estructura(CURVAS + cumulo_de(i), paso.y, ciclo, edad, dura);
    s = cumulo_de(i) * 7907u + uint(ciclo) * 3571u + 17u;
  }
  float u = azar(s);
  uint k = 0u;
  [unroll] for (uint j = 0u; j + 1u < COLORES; ++j) k += u >= PESO_HASTA[j] ? 1u : 0u;
  return k;
}

StructuredBuffer<float4> estado;
StructuredBuffer<uint2> en_celda;
StructuredBuffer<uint> inicio;
StructuredBuffer<uint> prefijos;
RWStructuredBuffer<float4> puntos;

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint i = id.x;
  if (i >= PARTICULAS) return;
  uint2 c = en_celda[i];
  if (c.x == SIN_CELDA) return;
  // su luz (en media precisión), su color (4 bits : el índice en PALETA) y,
  // en los 12 bits altos, un número suyo de siempre, al azar : dónde la
  // lente encoge el cielo se ve solo una parte de ellas, siempre las mismas
  // (ver color_del_cielo)
  float4 x = estado[i];
  puntos[inicio[c.x] + prefijos[c.x >> 10] + c.y] = float4(x.xyz, asfloat(f32tof16(x.w) | (color_de(i) << 16) | (((i * 2654435761u) >> 20) << 20)));
}
)" ;

// Lo que lee quien pinta : las partículas de las celdas que cubre la huella
// del píxel (sus cuatro esquinas, d ± jx ± jy, sin repetir celda), y cada
// una, por mínimos cuadrados, en píxeles desde el centro : si cae a menos de
// un píxel, su parte (bilineal, como el polvo de liquido)

constexpr const char * cbuffer_leer_hlsl = R"(
cbuffer Polvo {
  float4 paso;
  float4 flujo;
  float4 vida;
  float4 centro;
  float4 eje_u;
  float4 eje_v;
  float4 antes;
  float4 hebra;
  float4 cumulo;
};
)" ;

constexpr const char * leer_hlsl = R"(
StructuredBuffer<float4> cielo_puntos;
StructuredBuffer<uint> cielo_inicio;
StructuredBuffer<uint> cielo_prefijos;

uint cielo_empieza(uint c) { return cielo_inicio[c] + cielo_prefijos[c >> 10]; }

// La luz de las partículas de la celda c en el píxel, en coma fija (1/65536)
// : la suma de enteros no depende del orden, y el de las partículas dentro
// de una celda cambia de un fotograma a otro (sale de los atómicos)
// La luz de los puntos de la celda c en el píxel, por canal (con su color);
// solo los de número (ver repartir) por debajo de umbral (de 0 a 4096 : todos)
uint3 cielo_luz_celda(uint c, float3 d, float3 jx, float3 jy, float a, float b, float e, float det, uint umbral) {
  uint3 luz = uint3(0u, 0u, 0u);
  if (c == 0xFFFFFFFFu) return luz;
  uint fin = cielo_empieza(c + 1u);
  for (uint k = cielo_empieza(c); k < fin; ++k) {
    float4 p = cielo_puntos[k];
    uint datos = asuint(p.w);
    if ((datos >> 20) >= umbral) continue;
    if (dot(p.xyz, d) <= 0.0) continue;
    float3 o = p.xyz - d;
    float u = dot(jx, o), w = dot(jy, o);
    float2 px = float2(e * u - b * w, a * w - b * u) / det;
    if (abs(px.x) < 1.0 && abs(px.y) < 1.0) luz += uint3(PALETA[(datos >> 16) & 15u] * (f16tof32(datos & 0xFFFFu) * (1.0 - abs(px.x)) * (1.0 - abs(px.y)) * 65536.0) + 0.5);
  }
  return luz;
}

float3 color_del_cielo(float3 d, float3 jx, float3 jy, float omega) {
  float a = dot(jx, jx), b = dot(jx, jy), e = dot(jy, jy);
  float det = a * e - b * b;
  if (det <= 1e-24) return float3(0.0, 0.0, 0.0);
  // todo en luz lineal, y al final a valor de pantalla (la potencia
  // 1/GAMMA) : repartir el valor de pantalla entre píxeles perdía luz (en la
  // esquina entre cuatro, un punto se veía unas 5 veces más tenue) y los
  // puntos titilaban al moverse.
  // Fuera del casquete no hay partículas : junto a la sombra, donde la lente
  // enseña cielo de muy lejos y lo encoge tanto que los puntos ya casi no se
  // verían (ver abajo), negro
  if (dot(d, centro.xyz) < centro.w) return float3(0.0, 0.0, 0.0);
  // El aumento de la lente : el ángulo sólido del píxel desde la cámara
  // entre el del cielo que ve (con tope : en el anillo no lo tiene). Donde
  // encoge el cielo (aumento < 1), al píxel le caerían 1/aumento veces más
  // partículas : se ve solo una parte, √aumento de ellas (siempre las
  // mismas), √aumento veces más tenues. Se ven 1/√aumento veces más puntos
  // que sin lente y la luz media es la misma (la lente conserva el brillo
  // por ángulo sólido), pero siguen siendo puntos que se mueven con la lente.
  // Antes, su media : un gris liso que se veía como una capa opaca (la media
  // de un campo de puntos sueltos se ve unas 4 veces más clara que el campo)
  float aumento = min(omega / sqrt(det), POLVO_AUMENTO);
  float encoge = sqrt(min(aumento, 1.0));
  uint umbral = uint(encoge * 4096.0);
  uint c0 = celda_casquete(normalize(d - jx - jy)), c1 = celda_casquete(normalize(d + jx - jy));
  uint c2 = celda_casquete(normalize(d - jx + jy)), c3 = celda_casquete(normalize(d + jx + jy));
  uint3 fija = cielo_luz_celda(c0, d, jx, jy, a, b, e, det, umbral);
  if (c1 != c0) fija += cielo_luz_celda(c1, d, jx, jy, a, b, e, det, umbral);
  if (c2 != c0 && c2 != c1) fija += cielo_luz_celda(c2, d, jx, jy, a, b, e, det, umbral);
  if (c3 != c0 && c3 != c1 && c3 != c2) fija += cielo_luz_celda(c3, d, jx, jy, a, b, e, det, umbral);
  // la luz de sus puntos (lineal; el aumento de la lente la multiplica; donde
  // encoge, √aumento)
  float3 luz = float3(fija) / 65536.0 * max(aumento, encoge);
  // la aureola de la lente, limpia (con fundido)
  float limpio = 1.0 - smoothstep(AUREOLA_DESDE, AUREOLA_HASTA, omega / sqrt(det));
  return limpio * POLVO_LUZ * pow(luz, 1.0 / GAMMA);
}
)" ;

// Una pasada : un kernel con sus buffers, y la barrera de después (Diligent
// no la pone entre dos kernels que usan un buffer como UAV)

void lanzar_y_esperar(IDeviceContext * ctx , const Kernel & k , IShaderResourceBinding * b , std :: initializer_list<IBuffer *> escritos) {

	gpu_lanzar(ctx , k , b) ;

	std :: vector<StateTransitionDesc> barreras ;

	for (IBuffer * e : escritos) { barreras.emplace_back(e , RESOURCE_STATE_UNORDERED_ACCESS , RESOURCE_STATE_UNORDERED_ACCESS , STATE_TRANSITION_FLAG_UPDATE_STATE) ; }

	ctx->TransitionResourceStates(static_cast<Uint32>(barreras.size()) , barreras.data()) ;

}

}  // namespace

std :: vector<Recurso> PolvoCielo :: recursos() const {

	return { { "Polvo" , uniforme_ } , { "cielo_puntos" , gpu_lectura(puntos_) } , { "cielo_inicio" , gpu_lectura(inicio_) } , { "cielo_prefijos" , gpu_lectura(prefijos_) } } ;

}

std :: string PolvoCielo :: definiciones() const {

	const PolvoForma & f = forma_ ;

	return "#define CELDAS_LADO " + std :: to_string(f.celdas) + "u\n"
		"#define POLVO_LUZ " + numero_hlsl(f.luz) + "\n"
		"#define POLVO_AUMENTO " + numero_hlsl(f.aumento_max) + "\n"
		"#define GAMMA " + numero_hlsl(gamma_pantalla) + "\n"
		"#define AUREOLA_DESDE " + numero_hlsl(f.aureola_desde) + "\n"
		"#define AUREOLA_HASTA " + numero_hlsl(f.aureola_hasta) + "\n" +
		paleta_hlsl(paleta_polvo(f , gamma_pantalla)) +
		cbuffer_leer_hlsl + celda_hlsl + leer_hlsl ;

}

bool PolvoCielo :: crear(const Gpu & gpu , std :: string & error) {

	gpu_ = gpu ;

	sembrado_ = false ;

	const PolvoForma & f = forma_ ;

	const Uint32 celdas = f.celdas * f.celdas ;

	// las celdas y una más (el total), en bloques enteros

	const Uint32 escaneadas = celdas + 1 , bloques = (escaneadas + bloque - 1) / bloque ;

	BufferDesc desc ;

	desc.Name = "Agujero polvo uniforms" ;

	desc.Size = sizeof(PolvoGpu) ;

	desc.Usage = USAGE_DEFAULT ;

	desc.BindFlags = BIND_UNIFORM_BUFFER ;

	desc.ImmediateContextMask = gpu_.mascara ;

	gpu_.device->CreateBuffer(desc , nullptr , &uniforme_) ;

	estado_ = gpu_campo(gpu_ , { f.particulas , 16 , "Agujero polvo estado" }) ;

	en_celda_ = gpu_campo(gpu_ , { f.particulas , 8 , "Agujero polvo en celda" }) ;

	cuentas_ = gpu_campo(gpu_ , { escaneadas , 4 , "Agujero polvo cuentas" }) ;

	inicio_ = gpu_campo(gpu_ , { escaneadas , 4 , "Agujero polvo inicio" }) ;

	sumas_ = gpu_campo(gpu_ , { bloques , 4 , "Agujero polvo sumas" }) ;

	prefijos_ = gpu_campo(gpu_ , { bloques , 4 , "Agujero polvo prefijos" }) ;

	puntos_ = gpu_campo(gpu_ , { f.particulas , 16 , "Agujero polvo puntos" }) ;

	// los centros de las hebras y los cúmulos : dónde están y su velocidad

	centros_ = gpu_campo(gpu_ , { 3 * (std :: max(f.hebras.curvas , 1u) + std :: max(f.cumulos.cuantos , 1u)) , 16 , "Agujero polvo centros" }) ;

	// y lo que mide cada una (hasta dónde llegan sus partículas visibles)

	alcance_ = gpu_campo(gpu_ , { std :: max(f.hebras.curvas , 1u) + std :: max(f.cumulos.cuantos , 1u) , 4 , "Agujero polvo alcance" }) ;

	if (!uniforme_ || !estado_ || !en_celda_ || !cuentas_ || !inicio_ || !sumas_ || !prefijos_ || !puntos_ || !centros_ || !alcance_) { return gpu_fallo(error , "OUT_OF_GPU_MEMORY: el polvo") ; }

	static_assert(bloque == 1024 , "escanear_hlsl va en bloques de 1024") ;

	if (bloques > bloque) { return gpu_fallo(error , "el polvo : demasiadas celdas para sumar sus prefijos en dos pasos") ; }

	const std :: string base = std :: string("#define PARTICULAS ") + std :: to_string(f.particulas) + "u\n"
		"#define BANDAS " + std :: to_string(bandas) + "u\n"
		"#define SECTORES " + std :: to_string(sectores) + "u\n"
		"#define POR_PARCHE " + std :: to_string(std :: max(f.particulas / (bandas * sectores) , 1u)) + "u\n"
		"#define CELDAS_LADO " + std :: to_string(f.celdas) + "u\n"
		"#define CADA_HEBRA " + std :: to_string(std :: max(f.hebras.cada , 1u)) + "u\n"
		"#define CURVAS " + std :: to_string(std :: max(f.hebras.curvas , 1u)) + "u\n"
		"#define LARGO_MIN " + numero_hlsl(static_cast<float>(f.hebras.largo_min * 3.14159265358979 / 180)) + "\n"
		"#define LARGO_MAX " + numero_hlsl(static_cast<float>(f.hebras.largo_max * 3.14159265358979 / 180)) + "\n"
		"#define CURVA_MIN " + numero_hlsl(static_cast<float>(f.hebras.curva_min * 3.14159265358979 / 180)) + "\n"
		"#define CURVA_MAX " + numero_hlsl(static_cast<float>(f.hebras.curva_max * 3.14159265358979 / 180)) + "\n"
		"#define CADA_CUMULO " + std :: to_string(std :: max(f.cumulos.cada , 3u)) + "u\n"
		"#define CUMULOS " + std :: to_string(std :: max(f.cumulos.cuantos , 1u)) + "u\n"
		"#define ESTRUCTURAS " + std :: to_string(std :: max(f.hebras.curvas , 1u) + std :: max(f.cumulos.cuantos , 1u)) + "u\n"
		"#define RADIO_CUMULO " + numero_hlsl(static_cast<float>(f.cumulos.radio * 3.14159265358979 / 180)) + "\n"
		"#define GAMMA " + numero_hlsl(gamma_pantalla) + "\n"
		"#define ESCANEADAS " + std :: to_string(escaneadas) + "u\n" + paleta_hlsl(paleta_polvo(f , gamma_pantalla)) + comun_hlsl + estructuras_hlsl() + celda_hlsl ;

	const std :: vector<Recurso> fijos{ { "Polvo" , uniforme_ } } ;

	const Uint32 grupos = (f.particulas + 255) / 256 ;

	sembrar_ = gpu_kernel(gpu_ , { "Agujero polvo sembrar" , base + sembrar_hlsl , fijos , grupos , 1 } , error) ;

	limpiar_ = gpu_kernel(gpu_ , { "Agujero polvo limpiar" , base + limpiar_hlsl , fijos , (escaneadas + 255) / 256 , 1 } , error) ;

	mover_ = gpu_kernel(gpu_ , { "Agujero polvo mover" , base + mover_hlsl + ruido_hlsl + casquete_hlsl + mover_main_hlsl , fijos , grupos , 1 } , error) ;

	const Uint32 estructuras = std :: max(f.hebras.curvas , 1u) + std :: max(f.cumulos.cuantos , 1u) ;

	centros_k_ = gpu_kernel(gpu_ , { "Agujero polvo centros" , base + ruido_hlsl + casquete_hlsl + centros_hlsl() , fijos , (estructuras + 63) / 64 , 1 } , error) ;

	escanear_ = gpu_kernel(gpu_ , { "Agujero polvo prefijos" , "#define N " + std :: to_string(escaneadas) + "u\n" + escanear_hlsl , {} , bloques , 1 } , error) ;

	escanear_sumas_ = gpu_kernel(gpu_ , { "Agujero polvo prefijos de los bloques" , "#define N " + std :: to_string(bloques) + "u\n" + escanear_hlsl , {} , 1 , 1 } , error) ;

	repartir_ = gpu_kernel(gpu_ , { "Agujero polvo repartir" , base + repartir_hlsl , fijos , grupos , 1 } , error) ;

	if (!centros_k_.pipeline || !sembrar_.pipeline || !limpiar_.pipeline || !mover_.pipeline || !escanear_.pipeline || !escanear_sumas_.pipeline || !repartir_.pipeline) { return false ; }

	sembrar_b_ = gpu_atar(sembrar_ , { { "estado" , gpu_escritura(estado_) } }) ;

	limpiar_b_ = gpu_atar(limpiar_ , { { "cuentas" , gpu_escritura(cuentas_) } }) ;

	mover_b_ = gpu_atar(mover_ , { { "estado" , gpu_escritura(estado_) } , { "en_celda" , gpu_escritura(en_celda_) } , { "cuentas" , gpu_escritura(cuentas_) } , { "centros" , gpu_lectura(centros_) } , { "alcance" , gpu_escritura(alcance_) } }) ;

	centros_b_ = gpu_atar(centros_k_ , { { "centros" , gpu_escritura(centros_) } , { "alcance" , gpu_escritura(alcance_) } }) ;

	escanear_b_ = gpu_atar(escanear_ , { { "entrada" , gpu_escritura(cuentas_) } , { "salida" , gpu_escritura(inicio_) } , { "totales" , gpu_escritura(sumas_) } }) ;

	// los bloques : sus totales, a sus prefijos (y otro total, que no se usa :
	// a las propias sumas, que ya están leídas)

	escanear_sumas_b_ = gpu_atar(escanear_sumas_ , { { "entrada" , gpu_escritura(sumas_) } , { "salida" , gpu_escritura(prefijos_) } , { "totales" , gpu_escritura(sumas_) } }) ;

	repartir_b_ = gpu_atar(repartir_ , { { "estado" , gpu_lectura(estado_) } , { "en_celda" , gpu_lectura(en_celda_) } , { "inicio" , gpu_lectura(inicio_) } , { "prefijos" , gpu_lectura(prefijos_) } , { "puntos" , gpu_escritura(puntos_) } }) ;

	return true ;

}

void PolvoCielo :: avanzar(IDeviceContext * ctx , float dt , float t , const float (& centro)[3]) {

	const PolvoForma & f = forma_ ;

	// la base del plano tangente al centro : u, lo más horizontal posible
	// (perpendicular al eje z); en el eje, x

	const double cx = centro[0] , cy = centro[1] , cz = centro[2] ;

	double ux = -cy , uy = cx , uz = 0 ;

	double n = std :: sqrt(ux * ux + uy * uy) ;

	if (n < 1e-6) { ux = 1 ; uy = 0 ; n = 1 ; }

	ux /= n ; uy /= n ;

	const double vx = cy * uz - cz * uy , vy = cz * ux - cx * uz , vz = cx * uy - cy * ux ;

	const double radio = f.radio * 3.14159265358979 / 180 ;

	const PolvoGpu datos{

		{ dt , t , 0 , 0 } ,

		{ f.frecuencia , f.cambio , f.velocidad , f.hebras.cohesion } ,

		{ f.vida , f.vida_extra , f.fundido , 0 } ,

		{ centro[0] , centro[1] , centro[2] , static_cast<float>(std :: cos(radio)) } ,

		{ static_cast<float>(ux) , static_cast<float>(uy) , static_cast<float>(uz) , static_cast<float>(std :: tan(radio)) } ,

		{ static_cast<float>(vx) , static_cast<float>(vy) , static_cast<float>(vz) , 0 } ,

		{ antes_[0] , antes_[1] , antes_[2] , 0 } ,

		{ f.hebras.vida , f.hebras.vida_extra , f.hebras.fundido , f.hebras.luz } ,

		{ f.cumulos.vida , f.cumulos.vida_extra , f.cumulos.fundido , f.cumulos.luz }

	} ;

	PolvoGpu enviar = datos ;

	if (!sembrado_) { std :: copy(centro , centro + 3 , enviar.antes) ; }

	ctx->UpdateBuffer(uniforme_ , 0 , sizeof(enviar) , &enviar , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

	std :: copy(centro , centro + 3 , antes_) ;

	if (!sembrado_) {

		// sembrado en el instante en que empieza a asentarse (las hebras, ya
		// en su sitio), y asentado `asentar` s en pasos de `paso_asentar` s
		// hasta el de ahora : el flujo ya ha doblado las hebras, que tienen
		// edades distintas

		const float pasito = std :: max(f.paso_asentar , 1.0e-3f) ;

		const int pasos = static_cast<int>(std :: max(f.asentar , 0.0f) / pasito + 0.5f) ;

		PolvoGpu al_sembrar = enviar ;

		al_sembrar.paso[1] = t - pasos * pasito ;

		al_sembrar.paso[2] = 1 ;  // los centros, en su sitio de nacer

		ctx->UpdateBuffer(uniforme_ , 0 , sizeof(al_sembrar) , &al_sembrar , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

		lanzar_y_esperar(ctx , centros_k_ , centros_b_ , { centros_ , alcance_ }) ;

		lanzar_y_esperar(ctx , sembrar_ , sembrar_b_ , { estado_ }) ;

		for (int k = pasos ; k > 0 ; k--) {

			PolvoGpu antes_de = enviar ;

			antes_de.paso[0] = pasito ;

			antes_de.paso[1] = t - k * pasito ;

			ctx->UpdateBuffer(uniforme_ , 0 , sizeof(antes_de) , &antes_de , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

			lanzar_y_esperar(ctx , centros_k_ , centros_b_ , { centros_ , alcance_ }) ;

			lanzar_y_esperar(ctx , mover_ , mover_b_ , { estado_ , en_celda_ , cuentas_ , alcance_ }) ;

		}

		ctx->UpdateBuffer(uniforme_ , 0 , sizeof(enviar) , &enviar , RESOURCE_STATE_TRANSITION_MODE_TRANSITION) ;

		sembrado_ = true ;

	}

	{

		Tramo tramo(banco_ , primera_medida_ , Etapa :: dibujar , gpu_.device , ctx) ;

		lanzar_y_esperar(ctx , centros_k_ , centros_b_ , { centros_ , alcance_ }) ;

		lanzar_y_esperar(ctx , limpiar_ , limpiar_b_ , { cuentas_ }) ;

		lanzar_y_esperar(ctx , mover_ , mover_b_ , { estado_ , en_celda_ , cuentas_ , alcance_ }) ;

	}

	{

		Tramo tramo(banco_ , primera_medida_ + 1 , Etapa :: dibujar , gpu_.device , ctx) ;

		lanzar_y_esperar(ctx , escanear_ , escanear_b_ , { inicio_ , sumas_ }) ;

		lanzar_y_esperar(ctx , escanear_sumas_ , escanear_sumas_b_ , { prefijos_ }) ;

	}

	{

		Tramo tramo(banco_ , primera_medida_ + 2 , Etapa :: dibujar , gpu_.device , ctx) ;

		gpu_lanzar(ctx , repartir_ , repartir_b_) ;

	}

}

}  // namespace shd
