// =====================================================================
//  fc_imu.cpp
// =====================================================================
#include "fc_imu.h"
#include <Wire.h>
#include <SparkFun_BNO08x_Arduino_Library.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

static BNO08x            s_bno;
static portMUX_TYPE      s_lock = portMUX_INITIALIZER_UNLOCKED;
static FcImuSample       s_pub  = {};
static TaskHandle_t      s_task = nullptr;

static uint8_t  s_addr       = FC_IMU_ADDR_A;
static bool     s_useIntPin  = false;   // se decide solo, ver notas abajo
static bool     s_bootConnected = false;
static uint32_t s_reportCount = 0;
static uint32_t s_hzWindow    = 0;

static const uint8_t WANTED_REPORT =
#if FC_IMU_USE_GAME_RV
  SENSOR_REPORTID_GAME_ROTATION_VECTOR;
#else
  SENSOR_REPORTID_ROTATION_VECTOR;
#endif

const char* fcImuStateText(FcImuState s) {
  switch (s) {
    case FC_IMU_OK:    return "OK";
    case FC_IMU_STALE: return "Sin datos";
    case FC_IMU_INIT:  return "Conectando";
    default:           return "IMU no disponible";
  }
}

// Diferencia angular más corta, en grados, siempre en -180..180.
static inline float angDiff(float a, float b) {
  float d = a - b;
  while (d > 180.0f)  d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return d;
}

static inline float wrap180(float a) {
  while (a > 180.0f)  a -= 360.0f;
  while (a < -180.0f) a += 360.0f;
  return a;
}

// Cuaternión -> Tait-Bryan ZYX (roll X, pitch Y, yaw Z), en grados.
static void quatToEuler(float qi, float qj, float qk, float qr,
                        float* roll, float* pitch, float* yaw) {
  float n = sqrtf(qi * qi + qj * qj + qk * qk + qr * qr);
  if (n < 1e-6f) { *roll = *pitch = *yaw = 0.0f; return; }
  float x = qi / n, y = qj / n, z = qk / n, w = qr / n;

  float t0 = 2.0f * (w * x + y * z);
  float t1 = 1.0f - 2.0f * (x * x + y * y);
  *roll = atan2f(t0, t1) * RAD_TO_DEG;

  float t2 = 2.0f * (w * y - z * x);
  if (t2 > 1.0f)  t2 = 1.0f;
  if (t2 < -1.0f) t2 = -1.0f;
  *pitch = asinf(t2) * RAD_TO_DEG;

  float t3 = 2.0f * (w * z + x * y);
  float t4 = 1.0f - 2.0f * (y * y + z * z);
  *yaw = atan2f(t3, t4) * RAD_TO_DEG;
}

// Filtro: media exponencial sobre la diferencia angular más corta (para que
// no pegue saltos al cruzar +-180) + zona muerta para que no tiemble parado.
static inline float filterAngle(float prev, float raw, bool first) {
  if (first) return raw;
  float d = angDiff(raw, prev);
  if (fabsf(d) < FC_IMU_DEADZONE_DEG) return prev;
  return wrap180(prev + d * FC_IMU_SMOOTH);
}

static bool enableReports() {
#if FC_IMU_USE_GAME_RV
  return s_bno.enableGameRotationVector(FC_IMU_REPORT_MS);
#else
  return s_bno.enableRotationVector(FC_IMU_REPORT_MS);
#endif
}

// Nota sobre el pin INT (GPIO1):
// a la librería se le pasa siempre -1 como pin de interrupción a propósito.
// Su hal_wait_for_int() hace un bucle bloqueante de hasta 500 ms y, si expira,
// resetea el sensor por hardware; con el IMU desconectado eso convertiría cada
// lectura en medio segundo perdido. Aquí el INT se lee a mano, sin bloquear:
// sólo sirve de aviso de "hay dato listo".
static bool tryConnect() {
  const uint8_t addrs[2] = { FC_IMU_ADDR_A, FC_IMU_ADDR_B };
  for (int i = 0; i < 2; i++) {
    if (s_bno.begin(addrs[i], FC_IMU_WIRE, -1, FC_IMU_RST)) {
      s_addr = addrs[i];
      if (!enableReports()) {
        Serial.println(F("[IMU] El sensor respondió pero rechazó el informe."));
        continue;
      }
      Serial.printf("[IMU] BNO085 en 0x%02X, %s a %d Hz\n", s_addr,
                    FC_IMU_USE_GAME_RV ? "Game Rotation Vector" : "Rotation Vector",
                    1000 / FC_IMU_REPORT_MS);
      return true;
    }
  }
  return false;
}

static void publish(const FcImuSample& s) {
  portENTER_CRITICAL(&s_lock);
  s_pub = s;
  portEXIT_CRITICAL(&s_lock);
}

void fcImuGet(FcImuSample* out) {
  if (!out) return;
  portENTER_CRITICAL(&s_lock);
  *out = s_pub;
  portEXIT_CRITICAL(&s_lock);
  // Envejecer el estado fuera de la sección crítica.
  if (out->state == FC_IMU_OK &&
      (millis() - out->lastUpdateMs) > FC_IMU_TIMEOUT_MS) {
    out->state = FC_IMU_STALE;
    out->hz = 0.0f;
  }
}

static void imuTask(void* arg) {
  (void)arg;
  FcImuSample local = {};
  local.state = FC_IMU_INIT;
  publish(local);

  bool     connected  = s_bootConnected;   // ya conectado desde fcImuBegin()
  bool     firstValue = true;
  uint32_t lastTry    = 0;
  uint32_t lastData   = millis();
  uint32_t intSeen    = 0;      // cuántas veces el INT ha marcado dato listo
  uint32_t started    = millis();
  if (connected) { local.state = FC_IMU_OK; s_useIntPin = (FC_IMU_INT >= 0); }
  const TickType_t period = pdMS_TO_TICKS(1000 / FC_IMU_TASK_HZ);

  for (;;) {
    uint32_t now = millis();

    if (!connected) {
      if (now - lastTry >= FC_IMU_RETRY_MS) {
        lastTry = now;
        local.state = FC_IMU_INIT;
        publish(local);
        connected = tryConnect();
        if (connected) {
          firstValue = true;
          lastData   = millis();
          started    = millis();
          intSeen    = 0;
          s_useIntPin = (FC_IMU_INT >= 0);
        } else {
          local.state = FC_IMU_ABSENT;
          local.hz = 0.0f;
          publish(local);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // El BNO085 puede reiniciarse solo (pico de tensión, watchdog interno).
    // Cuando pasa hay que volver a pedir los informes o deja de hablar.
    if (s_bno.wasReset()) {
      local.resets++;
      Serial.println(F("[IMU] El BNO085 se ha reiniciado, reactivando informes."));
      if (!enableReports()) {
        connected = false;
        local.state = FC_IMU_ABSENT;
        publish(local);
        continue;
      }
    }

    // El INT en LOW significa "tengo dato". Si tras 1,5 s nunca ha bajado
    // pero sí llegan datos, es que el cable INT no está: se deja de mirar.
    bool poll = true;
    if (s_useIntPin) {
      if (digitalRead(FC_IMU_INT) == LOW) { intSeen++; }
      else if (now - started > 1500 && intSeen == 0) {
        s_useIntPin = false;
        Serial.println(F("[IMU] Sin señal en el pin INT: se pasa a sondeo por tiempo."));
      } else {
        poll = false;
      }
    }

    if (poll && s_bno.getSensorEvent()) {
      if (s_bno.getSensorEventID() == WANTED_REPORT) {
        float qi, qj, qk, qr;
#if FC_IMU_USE_GAME_RV
        qi = s_bno.getGameQuatI();  qj = s_bno.getGameQuatJ();
        qk = s_bno.getGameQuatK();  qr = s_bno.getGameQuatReal();
        uint8_t acc = 3;   // el Game Rotation Vector no reporta precisión
#else
        qi = s_bno.getQuatI();  qj = s_bno.getQuatJ();
        qk = s_bno.getQuatK();  qr = s_bno.getQuatReal();
        uint8_t acc = s_bno.getQuatAccuracy();
#endif
        float r, p, y;
        quatToEuler(qi, qj, qk, qr, &r, &p, &y);
#if FC_IMU_INVERT_ROLL
        r = -r;
#endif
#if FC_IMU_INVERT_PITCH
        p = -p;
#endif
#if FC_IMU_INVERT_YAW
        y = -y;
#endif
        local.roll  = filterAngle(local.roll,  r, firstValue);
        local.pitch = filterAngle(local.pitch, p, firstValue);
        local.yaw   = filterAngle(local.yaw,   y, firstValue);
        local.qi = qi; local.qj = qj; local.qk = qk; local.qr = qr;
        local.accuracy = acc;
        local.state = FC_IMU_OK;
        local.lastUpdateMs = millis();
        firstValue = false;
        lastData = local.lastUpdateMs;

        s_reportCount++;
        if (local.lastUpdateMs - s_hzWindow >= 1000) {
          uint32_t dt = local.lastUpdateMs - s_hzWindow;
          local.hz = (s_hzWindow == 0) ? 0.0f
                                       : (float)s_reportCount * 1000.0f / (float)dt;
          s_reportCount = 0;
          s_hzWindow = local.lastUpdateMs;
        }
        publish(local);
      }
    }

    // Sin datos durante demasiado tiempo: marcar y reintentar desde cero.
    if (millis() - lastData > FC_IMU_TIMEOUT_MS) {
      local.state = FC_IMU_STALE;
      local.hz = 0.0f;
      publish(local);
      if (!s_bno.isConnected()) {
        Serial.println(F("[IMU] BNO085 desaparecido del bus, reintentando."));
        connected = false;
        lastTry = 0;
        local.state = FC_IMU_ABSENT;
        publish(local);
      } else {
        lastData = millis();
        enableReports();
      }
    }

    vTaskDelay(period ? period : 1);
  }
}

bool fcImuBegin() {
  FC_IMU_WIRE.begin(FC_IMU_SDA, FC_IMU_SCL, FC_IMU_I2C_HZ);
  FC_IMU_WIRE.setTimeOut(50);          // ms; un bus colgado no bloquea la tarea
  if (FC_IMU_INT >= 0) pinMode(FC_IMU_INT, INPUT_PULLUP);

  bool present = tryConnect();
  s_bootConnected = present;
  if (!present) {
    Serial.println(F("[IMU] No se detecta el BNO085. La cámara arranca igual; "
                     "la web mostrará 'IMU no disponible' y seguirá reintentando."));
  }

  FcImuSample s = {};
  s.state = present ? FC_IMU_OK : FC_IMU_ABSENT;
  s.lastUpdateMs = millis();
  publish(s);

  // Núcleo 1: el Wi-Fi y los servidores HTTP viven en el 0. Así una lectura
  // I2C lenta no le quita tiempo al streaming.
  BaseType_t ok = xTaskCreatePinnedToCore(imuTask, "fc_imu", 4096, nullptr,
                                          3, &s_task, 1);
  if (ok != pdPASS) {
    Serial.println(F("[IMU] No se pudo crear la tarea del IMU."));
    return false;
  }
  return present;
}
