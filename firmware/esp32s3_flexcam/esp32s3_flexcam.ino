// =====================================================================
//  FlexCam S26 — cámara web para ESP32-S3-N16R8 + OV5640 (5 MP) + BNO085
//
//  Reparto de trabajo (nada bloquea a nada):
//    núcleo 0 : Wi-Fi/LWIP, servidor web (:80) y servidor MJPEG (:81)
//    núcleo 1 : tarea del BNO085, tarea de telemetría WebSocket, loop()
//
//  El ESP32 no rota ni recomprime un solo píxel: manda JPEG tal cual sale
//  del OV5640 y los ángulos del IMU. El giro del visor lo hace el navegador.
//
//  Este archivo se deja con sólo setup() y loop() a propósito: así el
//  generador de prototipos del Arduino IDE no tiene nada que inventar.
//  Todo lo ajustable está en fc_config.h. Menú del IDE: README_FLEXCAM.md.
// =====================================================================
#include <Arduino.h>
#include <esp_heap_caps.h>

#include "fc_config.h"
#include "fc_sys.h"
#include "fc_camera.h"
#include "fc_imu.h"
#include "fc_server.h"

static bool     g_camOk = false;
static bool     g_imuOk = false;
static uint32_t g_lastBeat = 0;
static uint32_t g_lastCamRetry = 0;

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1200) { delay(10); }

  fcSysBanner();
  fcSysMemoria("antes de la camara");

  // 1) La cámara primero: es lo único imprescindible.
  g_camOk = fcCameraBegin();
  fcSysMemoria("tras la camara");

  // 2) Wi-Fi en modo AP. Sin AP no hay interfaz posible.
  if (!fcSysWifiAP()) {
    Serial.println(F("[SYS] Sin AP no hay interfaz. Reiniciando en 3 s."));
    delay(3000);
    ESP.restart();
  }

  // 3) IMU. Si falla se sigue igual: la web mostrará "IMU no disponible" y
  //    la tarea reintentará por su cuenta sin tocar la cámara.
  g_imuOk = fcImuBegin();

  // 4) Servidores HTTP y WebSocket.
  if (!fcServerBegin()) {
    Serial.println(F("[SYS] Los servidores HTTP no arrancaron. Reiniciando en 3 s."));
    delay(3000);
    ESP.restart();
  }

  fcSysMemoria("todo arrancado");
  Serial.println(F("------------------------------------------------------"));
  Serial.printf("  Conectate al Wi-Fi  %s  (clave %s)\n", FC_AP_SSID, FC_AP_PASSWORD);
  Serial.printf("  Y abre              http://%d.%d.%d.%d/\n",
                FC_AP_IP_1, FC_AP_IP_2, FC_AP_IP_3, FC_AP_IP_4);
  Serial.printf("  Camara: %s   IMU: %s\n",
                g_camOk ? "OK" : "FALLO", g_imuOk ? "OK" : "no disponible");
  Serial.println(F("------------------------------------------------------"));
}

void loop() {
  // Aquí no se hace trabajo real: todo vive en tareas. Sólo un latido de
  // diagnóstico cada 10 s. Ningún delay() en las rutas de vídeo o IMU.
  uint32_t now = millis();

  if (now - g_lastBeat >= 10000) {
    g_lastBeat = now;
    FcCamStatus cs; fcCameraGetStatus(&cs);
    FcImuSample is; fcImuGet(&is);
    Serial.printf("[LAT] up=%lus  cam=%s %ux%u  fps=%.1f  mjpeg=%lu  ws=%lu  "
                  "imu=%s %.0fHz  roll=%+.1f  DRAM=%uKB  PSRAM=%uKB  drops=%lu\n",
      (unsigned long)(now / 1000), cs.ready ? "OK" : "KO", cs.width, cs.height,
      cs.fps, (unsigned long)cs.clients, (unsigned long)fcServerWsClients(),
      fcImuStateText(is.state), is.hz, is.roll,
      (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
      (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
      (unsigned long)cs.dropped);
  }

  // Si la cámara no arrancó (cable suelto al encender) se reintenta cada
  // 30 s, en vez de reiniciar la placa una y otra vez.
  if (!g_camOk && now - g_lastCamRetry >= 30000) {
    g_lastCamRetry = now;
    Serial.println(F("[CAM] Reintentando iniciar la camara..."));
    g_camOk = fcCameraBegin();
  }

  delay(50);   // sólo aquí, fuera de las rutas de vídeo y telemetría
}
