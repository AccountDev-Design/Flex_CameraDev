// =====================================================================
//  fc_imu.h — BNO085 en su propia tarea. Nunca bloquea a la cámara.
// =====================================================================
#pragma once

#include <Arduino.h>
#include "fc_config.h"

enum FcImuState {
  FC_IMU_ABSENT = 0,   // no se encontró en el bus
  FC_IMU_OK     = 1,   // entregando datos
  FC_IMU_STALE  = 2,   // estaba y ha dejado de responder
  FC_IMU_INIT   = 3    // reintentando conectar
};

struct FcImuSample {
  float      roll;      // grados, -180..180
  float      pitch;     // grados, -90..90
  float      yaw;       // grados, -180..180
  float      qi, qj, qk, qr;
  float      hz;        // frecuencia real de informes del sensor
  uint8_t    accuracy;  // 0..3 (calibración que reporta el BNO085)
  FcImuState state;
  uint32_t   lastUpdateMs;
  uint32_t   resets;    // veces que el sensor se ha reiniciado solo
};

// Arranca I2C y la tarea. Devuelve false si el sensor no aparece, pero la
// tarea queda viva reintentando: la cámara nunca depende de esto.
bool fcImuBegin();

// Copia atómica del último dato.
void fcImuGet(FcImuSample* out);

// Texto corto del estado para la web.
const char* fcImuStateText(FcImuState s);
