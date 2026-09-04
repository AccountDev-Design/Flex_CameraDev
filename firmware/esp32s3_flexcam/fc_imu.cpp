// =====================================================================
//  fc_imu.cpp
// =====================================================================
#include "fc_imu.h"
#include "fc_horizon.h"
#include <Wire.h>
#include <SparkFun_BNO08x_Arduino_Library.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <atomic>

static BNO08x        s_bno;
static portMUX_TYPE  s_lock = portMUX_INITIALIZER_UNLOCKED;
static FcImuSample   s_pub  = {};
static TaskHandle_t  s_task = nullptr;

static uint8_t  s_addr          = FC_IMU_ADDR_A;
static bool     s_useIntPin     = false;
static bool     s_bootConnected = false;
static uint32_t s_reportCount   = 0;
static uint32_t s_hzWindow      = 0;

// Configuración de montaje. Volátil porque la escribe el servidor HTTP
// desde otra tarea y la lee la tarea del IMU.
static std::atomic<int32_t>  s_mountDeg{FC_IMU_MOUNT_DEG};
static std::atomic<bool>     s_invert{FC_IMU_INVERT_ROLL != 0};
static std::atomic<uint8_t>  s_plane{FC_IMU_PLANE};
static std::atomic<uint32_t> s_epoch{1};

// La matemática vive en fc_horizon.h, compartida con la prueba de PC.
static inline float wrap180(float a) { return fchWrap180(a); }

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

// Cuaternión -> Tait-Bryan ZYX en grados. Sólo para mostrar en el panel:
// el Horizon Lock NO usa este roll.
static void quatToEuler(float qi, float qj, float qk, float qr,
                        float* roll, float* pitch, float* yaw) {
  float n = sqrtf(qi * qi + qj * qj + qk * qk + qr * qr);
  if (n < 1e-6f) { *roll = *pitch = *yaw = 0.0f; return; }
  float x = qi / n, y = qj / n, z = qk / n, w = qr / n;
  *roll = atan2f(2.0f * (w * x + y * z), 1.0f - 2.0f * (x * x + y * y)) * RAD_TO_DEG;
  float t2 = 2.0f * (w * y - z * x);
  t2 = t2 > 1.0f ? 1.0f : (t2 < -1.0f ? -1.0f : t2);
  *pitch = asinf(t2) * RAD_TO_DEG;
  *yaw = atan2f(2.0f * (w * z + x * y), 1.0f - 2.0f * (y * y + z * z)) * RAD_TO_DEG;
}

// ---------------------------------------------------------------------
static bool enableReports() {
#if FC_IMU_USE_GAME_RV
  return s_bno.enableGameRotationVector(FC_IMU_REPORT_MS);
#else
  return s_bno.enableRotationVector(FC_IMU_REPORT_MS);
#endif
}

// A la librería se le pasa siempre INT = -1 a propósito: su
// hal_wait_for_int() bloquea hasta 500 ms y además resetea el sensor por
// hardware al expirar. Todas esas llamadas están dentro de
// "if (_int_pin != -1)", así que con -1 nunca se ejecutan. GPIO1 se lee
// aquí a mano, sin bloquear, sólo como aviso de "hay dato listo".
static bool tryConnect() {
  const uint8_t addrs[2] = { FC_IMU_ADDR_A, FC_IMU_ADDR_B };
  for (int i = 0; i < 2; i++) {
    if (s_bno.begin(addrs[i], FC_IMU_WIRE, -1, FC_IMU_RST)) {
      s_addr = addrs[i];
      if (!enableReports()) {
        Serial.println(F("[IMU] El sensor respondio pero rechazo el informe."));
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
  if (out->state == FC_IMU_OK &&
      (millis() - out->tsMs) > FC_IMU_TIMEOUT_MS) {
    out->state = FC_IMU_STALE;
    out->hz = 0.0f;
  }
}

void fcImuSetMount(int deg) {
  int d = ((deg % 360) + 360) % 360;
  d = (d / 90) * 90;                       // sólo 0/90/180/270
  if (d != s_mountDeg.load()) { s_mountDeg.store(d); s_epoch.fetch_add(1); }
}
void fcImuSetInvert(bool inv) {
  if (inv != s_invert.load()) { s_invert.store(inv); s_epoch.fetch_add(1); }
}
void fcImuSetPlane(uint8_t plane) {
  if (plane > FC_PLANE_YZ) plane = FC_PLANE_XY;
  if (plane != s_plane.load()) { s_plane.store(plane); s_epoch.fetch_add(1); }
}

// ---------------------------------------------------------------------
// Tarea
// ---------------------------------------------------------------------
static void imuTask(void* arg) {
  (void)arg;
  FcImuSample local = {};
  local.state = FC_IMU_INIT;
  local.mountDeg = (int16_t)s_mountDeg.load();
  local.invert   = s_invert.load();
  local.plane    = s_plane.load();
  local.epoch    = s_epoch.load();
  publish(local);

  FcHorizonState hz_st;
  fchReset(&hz_st);

  bool     connected  = s_bootConnected;
  bool     haveHorizon = false;
  bool     wasValid    = false;
  uint32_t lastTry    = 0;
  uint32_t lastData   = millis();
  uint32_t intSeen    = 0;
  uint32_t started    = millis();
  uint32_t lastEpoch  = s_epoch.load();
  const TickType_t period = pdMS_TO_TICKS(1000 / FC_IMU_TASK_HZ);

  if (connected) { local.state = FC_IMU_OK; s_useIntPin = (FC_IMU_INT >= 0); }

  for (;;) {
    uint32_t now = millis();

    if (!connected) {
      if (now - lastTry >= FC_IMU_RETRY_MS) {
        lastTry = now;
        local.state = FC_IMU_INIT;
        publish(local);
        connected = tryConnect();
        if (connected) {
          fchReset(&hz_st); haveHorizon = false; wasValid = false;
          lastData = millis(); started = millis(); intSeen = 0;
          s_useIntPin = (FC_IMU_INT >= 0);
        } else {
          local.state = FC_IMU_ABSENT;
          local.hz = 0.0f;
          publish(local);            // se congela el último horizonte válido
        }
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

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

    // Un cambio de montaje/inversión/plano cambia el ángulo de golpe. Se
    // vuelve a sembrar el continuo para no acumular un salto falso; la web
    // ve subir 'epoch' y retoma su referencia.
    uint32_t epochNow = s_epoch.load();
    if (epochNow != lastEpoch) {
      lastEpoch = epochNow;
      fchReset(&hz_st);
      haveHorizon = false;
      local.epoch = lastEpoch;
    }
    local.mountDeg = (int16_t)s_mountDeg.load();
    local.invert   = s_invert.load();
    local.plane    = s_plane.load();

    bool poll = true;
    if (s_useIntPin) {
      if (digitalRead(FC_IMU_INT) == LOW) { intSeen++; }
      else if (now - started > 1500 && intSeen == 0) {
        s_useIntPin = false;
        Serial.println(F("[IMU] Sin senal en el pin INT: se pasa a sondeo por tiempo."));
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
        uint32_t tNow = millis();

        float gx, gy, gz;
        fchGravityFromQuat(qi, qj, qk, qr, &gx, &gy, &gz);

        float rawPlane, conf;
        fchHorizonFromGravity(gx, gy, gz, local.plane, &rawPlane, &conf);
        float raw = fchApplyMount(rawPlane, local.mountDeg, local.invert);

        // Histéresis para no parpadear justo en el umbral.
        bool valid = wasValid ? (conf > FC_HORIZON_CONF_LO)
                              : (conf > FC_HORIZON_CONF_HI);

        fchUpdate(&hz_st, raw, valid, tNow);
        if (valid) local.horizonRaw = raw;
        local.horizonCont = hz_st.cont;
        local.horizonFilt = hz_st.filt;
        haveHorizon = hz_st.have;

        wasValid = valid;
        local.horizonValid = valid && haveHorizon;
        local.confidence   = conf;
        local.gx = gx; local.gy = gy; local.gz = gz;
        local.qi = qi; local.qj = qj; local.qk = qk; local.qr = qr;
        quatToEuler(qi, qj, qk, qr, &local.roll, &local.pitch, &local.yaw);
        local.accuracy = acc;
        local.state    = FC_IMU_OK;
        local.tsMs     = tNow;
        local.seq++;
        lastData = tNow;

        s_reportCount++;
        if (tNow - s_hzWindow >= 1000) {
          uint32_t dtw = tNow - s_hzWindow;
          local.hz = (s_hzWindow == 0) ? 0.0f
                                       : (float)s_reportCount * 1000.0f / (float)dtw;
          s_reportCount = 0;
          s_hzWindow = tNow;
        }
        publish(local);
      }
    }

    if (millis() - lastData > FC_IMU_TIMEOUT_MS) {
      local.state = FC_IMU_STALE;
      local.hz = 0.0f;
      local.horizonValid = false;      // congelado, no falso
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
  FC_IMU_WIRE.setTimeOut(50);
  if (FC_IMU_INT >= 0) pinMode(FC_IMU_INT, INPUT_PULLUP);

  bool present = tryConnect();
  s_bootConnected = present;
  if (!present) {
    Serial.println(F("[IMU] No se detecta el BNO085. La camara arranca igual; "
                     "la web mostrara 'IMU no disponible' y seguira reintentando."));
  }

  FcImuSample s = {};
  s.state = present ? FC_IMU_OK : FC_IMU_ABSENT;
  s.tsMs  = millis();
  s.mountDeg = (int16_t)s_mountDeg.load();
  s.invert   = s_invert.load();
  s.plane    = s_plane.load();
  s.epoch    = s_epoch.load();
  publish(s);

  // Núcleo 1: el Wi-Fi y los servidores HTTP viven en el 0.
  if (xTaskCreatePinnedToCore(imuTask, "fc_imu", 4608, nullptr, 3, &s_task, 1) != pdPASS) {
    Serial.println(F("[IMU] No se pudo crear la tarea del IMU."));
    return false;
  }
  return present;
}
