// =====================================================================
//  fc_horizon.h — matemática del Horizon Lock. Sin dependencias de
//  Arduino a propósito: así el mismo código se compila en el PC y se
//  puede verificar numéricamente (ver tools/test_horizon.cpp).
// =====================================================================
#pragma once

#include <math.h>
#include <stdint.h>

#ifndef FC_HORIZON_TAU_SLOW
  #define FC_HORIZON_TAU_SLOW  0.110f
  #define FC_HORIZON_TAU_FAST  0.022f
  #define FC_HORIZON_TAU_REACQ 0.260f
  #define FC_HORIZON_FAST_DEG  6.0f
  #define FC_HORIZON_DEADZONE  0.25f
  #define FC_HORIZON_REACQ_MS  900
#endif

// Planos de imagen expresados en ejes del sensor.
enum { FCH_PLANE_XY = 0, FCH_PLANE_XZ = 1, FCH_PLANE_YZ = 2 };

// --- ángulos ---------------------------------------------------------
static inline float fchWrap180(float a) {
  a = fmodf(a + 180.0f, 360.0f);
  if (a < 0.0f) a += 360.0f;
  return a - 180.0f;
}
static inline float fchAngDiff(float a, float b) { return fchWrap180(a - b); }

// --- cuaternión -> gravedad en ejes del sensor -----------------------
//
// El BNO085 da q como rotación cuerpo -> mundo con Z del mundo hacia
// arriba. La gravedad vale (0,0,-1) en el mundo, luego en el cuerpo es
// R^T·(0,0,-1) = -(tercera fila de R). Con q=(x,y,z,w) normalizado:
//     gx =  2(wy - xz)
//     gy = -2(yz + wx)
//     gz =  2(x² + y²) - 1
// Comprobación: q identidad -> (0,0,-1).
static inline void fchGravityFromQuat(float x, float y, float z, float w,
                                      float* gx, float* gy, float* gz) {
  float n = sqrtf(x * x + y * y + z * z + w * w);
  if (n < 1e-6f) { *gx = 0.0f; *gy = 0.0f; *gz = -1.0f; return; }
  x /= n; y /= n; z /= n; w /= n;
  *gx =  2.0f * (w * y - x * z);
  *gy = -2.0f * (y * z + w * x);
  *gz =  2.0f * (x * x + y * y) - 1.0f;
}

// --- horizonte por proyección de gravedad ----------------------------
//
// Se proyecta la gravedad sobre los ejes reales "derecha" y "abajo" de la
// cámara:
//     horizonte = atan2(-g·derecha, g·abajo)
//
// SIGNO (esto es lo que hace que la imagen quede recta de verdad):
// el valor devuelto es la inclinación de la VERTICAL DEL MUNDO tal y como
// aparece DENTRO de la imagen, no el giro de la cámara. Si la cámara rueda
// +30 grados en sentido horario, el contenido de la foto aparece inclinado
// -30, así que aquí sale -30. Con eso la fórmula
//     compensacion = -(horizonte - referencia)
// da +30 y el visor gira +30, devolviendo el contenido a la vertical.
// Nivelada, la gravedad cae por "abajo": atan2(0,+) = 0.
//
// La confianza es el módulo de esa proyección. Apuntando al cenit o al
// nadir toda la gravedad se va por el eje óptico, la proyección tiende a
// cero y el horizonte queda matemáticamente indeterminado: ahí no se
// inventa un valor, se congela el último bueno.
static inline void fchHorizonFromGravity(float gx, float gy, float gz,
                                         uint8_t plane,
                                         float* angleDeg, float* conf) {
  float right, down;
  switch (plane) {
    case FCH_PLANE_XZ: right = gx; down = gz; break;   // eje óptico ~ Y
    case FCH_PLANE_YZ: right = gy; down = gz; break;   // eje óptico ~ X
    default:           right = gx; down = gy; break;   // eje óptico ~ Z
  }
  float mag = sqrtf(right * right + down * down);
  *conf = mag;                       // g es unitario: mag ya está en 0..1
  *angleDeg = (mag < 1e-4f) ? 0.0f : atan2f(-right, down) * 57.29577951308232f;
}

// Aplica montaje (0/90/180/270) e inversión de sentido.
static inline float fchApplyMount(float rawDeg, int mountDeg, bool invert) {
  float a = fchWrap180(rawDeg - (float)mountDeg);
  return invert ? fchWrap180(-a) : a;
}

// --- seguimiento continuo + filtro -----------------------------------
typedef struct {
  bool     have;            // ya hay un continuo sembrado
  bool     wasValid;
  float    lastRaw;         // último crudo envuelto, para el unwrap
  float    cont;            // ángulo continuo (puede pasar de 720)
  float    filt;            // continuo filtrado: el que se emite
  uint32_t lastMs;
  uint32_t reacquireUntil;
} FcHorizonState;

static inline void fchReset(FcHorizonState* st) {
  st->have = false; st->wasValid = false;
  st->lastRaw = 0.0f; st->cont = 0.0f; st->filt = 0.0f;
  st->lastMs = 0; st->reacquireUntil = 0;
}

// raw: ángulo crudo ya con montaje aplicado, en -180..180.
// valid: si la confianza supera el umbral (con histéresis fuera).
// Devuelve true si el estado quedó utilizable.
static inline bool fchUpdate(FcHorizonState* st, float raw, bool valid,
                             uint32_t nowMs) {
  if (!valid) {
    // Congelado: ni cont ni filt se tocan. Ningún valor inventado.
    st->wasValid = false;
    return st->have;
  }

  if (!st->have) {
    st->cont = raw; st->filt = raw; st->lastRaw = raw;
    st->have = true; st->lastMs = nowMs;
    st->wasValid = true;
    return true;
  }

  if (!st->wasValid) {
    // Volvemos de "sin referencia": se elige el representante del
    // horizonte verdadero más cercano al valor congelado, para deslizar
    // hasta él en vez de pegar un salto.
    st->cont = st->filt + fchAngDiff(raw, fchWrap180(st->filt));
    st->lastRaw = raw;
    st->reacquireUntil = nowMs + FC_HORIZON_REACQ_MS;
    // Clave: reiniciar la base de tiempo. Si no, el dt de esta muestra sería
    // todo el rato que estuvimos sin referencia y el filtro daría en un solo
    // paso casi todo el error, que es justo el salto que hay que evitar.
    st->lastMs = nowMs;
  } else {
    // Unwrap: se acumula la diferencia más corta, así el ángulo cruza
    // 180, 360, 720... sin discontinuidad.
    st->cont += fchAngDiff(raw, st->lastRaw);
    st->lastRaw = raw;
  }
  st->wasValid = true;

  float dt = (st->lastMs == 0) ? 0.005f : (float)(nowMs - st->lastMs) * 0.001f;
  if (dt <= 0.0f) dt = 0.001f;
  // Techo bajo a propósito: una sola muestra nunca puede mover la imagen
  // más de lo que movería un intervalo normal, pase lo que pase con el reloj.
  if (dt > 0.05f) dt = 0.05f;
  st->lastMs = nowMs;

  float err  = st->cont - st->filt;
  float aerr = fabsf(err);
  if (aerr >= FC_HORIZON_DEADZONE) {
    float k = aerr / FC_HORIZON_FAST_DEG;
    if (k > 1.0f) k = 1.0f;
    // Constante de tiempo adaptativa: quieto suaviza, en giro real corre.
    float tau = FC_HORIZON_TAU_SLOW +
                (FC_HORIZON_TAU_FAST - FC_HORIZON_TAU_SLOW) * k;
    if (nowMs < st->reacquireUntil) tau = FC_HORIZON_TAU_REACQ;
    st->filt += err * (1.0f - expf(-dt / tau));
  }
  return true;
}
