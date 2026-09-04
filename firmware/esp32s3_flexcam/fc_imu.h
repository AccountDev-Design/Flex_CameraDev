// =====================================================================
//  fc_imu.h — BNO085: cuaternión -> horizonte por proyección de gravedad.
//
//  El ángulo de horizonte NO es el roll de Euler. Se calcula proyectando
//  el vector gravedad sobre los ejes reales "derecha" y "abajo" de la
//  cámara. Eso es lo que hace que la corrección siga siendo correcta con
//  la cámara inclinada hacia arriba o hacia abajo, donde el roll de Euler
//  se vuelve inestable y cerca de la vertical se dispara.
//
//  Todo corre en su propia tarea. Un fallo del IMU no toca la cámara.
// =====================================================================
#pragma once

#include <Arduino.h>
#include "fc_config.h"

enum FcImuState {
  FC_IMU_ABSENT = 0,   // no aparece en el bus
  FC_IMU_OK     = 1,   // entregando datos
  FC_IMU_STALE  = 2,   // estaba y ha dejado de responder
  FC_IMU_INIT   = 3    // reintentando conectar
};

// Plano de la imagen expresado en ejes del sensor. Sirve para colocar el
// módulo en cualquier orientación física sin reprogramar nada.
enum FcImuPlane {
  FC_PLANE_XY = 0,   // eje óptico ~ Z del sensor (módulo paralelo a la cámara)
  FC_PLANE_XZ = 1,   // eje óptico ~ Y del sensor
  FC_PLANE_YZ = 2    // eje óptico ~ X del sensor
};

struct FcImuSample {
  // --- orientación cruda (informativa, para el panel) ---
  float roll, pitch, yaw;        // grados
  float qi, qj, qk, qr;          // cuaternión tal cual lo da el BNO085
  float gx, gy, gz;              // gravedad en ejes del sensor (unitario)

  // --- horizonte: lo que realmente usa el Horizon Lock ---
  float horizonRaw;              // -180..180, ya con montaje e inversión
  float horizonCont;             // continuo, sin envolver (720, -900, ...)
  float horizonFilt;             // continuo + filtrado -> el que se emite
  float confidence;              // 0..1, cuánta gravedad cae en el plano imagen
  bool  horizonValid;            // false = "Horizonte sin referencia"

  // --- salud y sincronía ---
  float      hz;                 // frecuencia real de informes
  uint32_t   seq;                // secuencia incremental de muestra
  uint32_t   tsMs;               // millis() de la muestra
  uint8_t    accuracy;           // 0..3 si la librería lo reporta
  FcImuState state;
  uint32_t   resets;             // reinicios propios del BNO085
  uint32_t   epoch;              // sube al cambiar montaje/inversión/plano

  // --- configuración vigente ---
  int16_t    mountDeg;           // 0/90/180/270
  bool       invert;
  uint8_t    plane;              // FcImuPlane
};

bool fcImuBegin();
void fcImuGet(FcImuSample* out);
const char* fcImuStateText(FcImuState s);

// Ajustes en caliente. Cambiarlos sube 'epoch' para que la web vuelva a
// tomar referencia en vez de dar un salto.
void fcImuSetMount(int deg);
void fcImuSetInvert(bool inv);
void fcImuSetPlane(uint8_t plane);
