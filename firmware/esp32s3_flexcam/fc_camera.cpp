// =====================================================================
//  fc_camera.cpp
// =====================================================================
#include "fc_camera.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <atomic>

// ---------------------------------------------------------------------
// Tabla de modos.
//
// LÍMITE REAL DEL DRIVER, no una decisión de diseño: en la tabla
// camera_sensor[] de esp32-camera (comprobado en el .a que enlaza el core
// 3.3.11) el OV5640 declara max_size = FRAMESIZE_QSXGA = 2560x1920.
// esp_camera_init() recorta EN SILENCIO cualquier tamaño mayor:
//     if (frame_size > camera_sensor[model].max_size) frame_size = max_size;
// Es decir, pedir FRAMESIZE_5MP (2592x1944) devuelve igualmente 2560x1920.
// Se pide QSXGA a propósito para que lo que anuncia la web coincida con lo
// que sale del sensor: 2560x1920 = 4,92 MP. El disparo va a esa resolución
// y el preview a 800x600, porque a 5 MP no hay vídeo fluido que prometer.
// ---------------------------------------------------------------------
const FcModeSpec FC_MODES[FC_MODE_COUNT] = {
  // id            label                  preview            capture          qP  qC hz
  { "photo5mp", "Foto 5 MP",        FRAMESIZE_SVGA,  FRAMESIZE_QSXGA,  12,  8, false },
  { "hiq",      "Alta calidad",     FRAMESIZE_UXGA,  FRAMESIZE_UXGA,   10, 10, false },
  { "fluid",    "Vista fluida",     FRAMESIZE_SVGA,  FRAMESIZE_SVGA,   12, 10, false },
  { "hlock",    "Horizon Lock",     FRAMESIZE_SVGA,  FRAMESIZE_SVGA,   12, 10, true  },
  { "hlockul",  "Horizon Lock Ultra", FRAMESIZE_VGA, FRAMESIZE_VGA,    14, 10, true  },
};

static SemaphoreHandle_t s_camMutex = nullptr;
static portMUX_TYPE      s_statLock = portMUX_INITIALIZER_UNLOCKED;

static bool     s_ready      = false;
static uint16_t s_pid        = 0;
static FcMode   s_mode       = FC_MODE_DEFAULT;
static std::atomic<uint32_t> s_streamGen{1};
static uint32_t s_clients   = 0;   // protegido por s_statLock
static uint32_t s_frames    = 0;   // protegido por s_statLock
static uint32_t s_dropped   = 0;   // protegido por s_statLock
static char     s_lastError[64]      = {0};
// Copia local de lo que expone la web. Se refresca dentro de secciones ya
// protegidas para no leer el estado interno mientras cambia el sensor.
static uint16_t s_curW = 0, s_curH = 0;
static bool     s_vflip = false, s_hmirror = false;

// Ventana móvil para los FPS reales.
static uint32_t s_fpsWindowStart = 0;
static uint32_t s_fpsWindowCount = 0;
static float    s_fps            = 0.0f;

static camera_config_t s_cfg;
static framesize_t s_bufferMaxFs = FRAMESIZE_INVALID;

static void setError(const char* msg) {
  if (!msg) { s_lastError[0] = 0; return; }
  strncpy(s_lastError, msg, sizeof(s_lastError) - 1);
  s_lastError[sizeof(s_lastError) - 1] = 0;
}

static void fillConfig(camera_config_t& c, framesize_t fs, int quality, uint8_t fbCount) {
  memset(&c, 0, sizeof(c));
  c.pin_pwdn      = FC_PIN_PWDN;
  c.pin_reset     = FC_PIN_RESET;
  c.pin_xclk      = FC_PIN_XCLK;
  c.pin_sccb_sda  = FC_PIN_SIOD;
  c.pin_sccb_scl  = FC_PIN_SIOC;
  c.pin_d7        = FC_PIN_D7;
  c.pin_d6        = FC_PIN_D6;
  c.pin_d5        = FC_PIN_D5;
  c.pin_d4        = FC_PIN_D4;
  c.pin_d3        = FC_PIN_D3;
  c.pin_d2        = FC_PIN_D2;
  c.pin_d1        = FC_PIN_D1;
  c.pin_d0        = FC_PIN_D0;
  c.pin_vsync     = FC_PIN_VSYNC;
  c.pin_href      = FC_PIN_HREF;
  c.pin_pclk      = FC_PIN_PCLK;
  c.xclk_freq_hz  = FC_XCLK_HZ;
  c.ledc_timer    = LEDC_TIMER_0;
  c.ledc_channel  = LEDC_CHANNEL_0;
  // Siempre JPEG. Nunca RGB565: en el ESP32 no se decodifica ni se recomprime
  // nada, el fotograma sale tal cual lo entrega el sensor.
  c.pixel_format  = PIXFORMAT_JPEG;
  c.frame_size    = fs;
  c.jpeg_quality  = quality;
  c.fb_count      = fbCount;
  c.fb_location   = CAMERA_FB_IN_PSRAM;
  // GRAB_LATEST descarta lo viejo: es lo que baja la latencia percibida.
  c.grab_mode     = (fbCount > 1) ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;
  c.sccb_i2c_port = -1;   // que el driver cree su propio puerto (I2C 1)
}

// Refresca la copia local. Llamar siempre con el mutex tomado (o en el
// arranque, antes de que exista ninguna otra tarea).
static void refreshCache() {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) { s_curW = s_curH = 0; return; }
  framesize_t fs = s->status.framesize;
  if (fs < FRAMESIZE_INVALID) {
    s_curW = resolution[fs].width;
    s_curH = resolution[fs].height;
  }
  s_vflip       = s->status.vflip != 0;
  s_hmirror     = s->status.hmirror != 0;
}

static void applySensorTweaks(framesize_t fs);

// Cambia resolución/calidad usando únicamente la API pública y estable de
// sensor_t. Los framebuffers se reservan una sola vez al arrancar para QSXGA,
// por lo que después se puede alternar entre preview y foto sin deinit/init.
// Debe llamarse siempre con el mutex de cámara tomado.
static bool configureSensor(framesize_t fs, int quality,
                            char* errOut, size_t errLen) {
  if (s_bufferMaxFs != FRAMESIZE_INVALID && fs > s_bufferMaxFs) {
    if (errOut) snprintf(errOut, errLen, "resolución requiere PSRAM activa");
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  if (!s || !s->set_framesize || !s->set_quality) {
    if (errOut) snprintf(errOut, errLen, "controlador de cámara incompleto");
    return false;
  }

  const framesize_t oldFs = s->status.framesize;
  const int oldQuality = s->status.quality;

  if (s->set_framesize(s, fs) != 0) {
    if (errOut) snprintf(errOut, errLen, "no se pudo cambiar resolución");
    return false;
  }
  if (s->set_quality(s, quality) != 0) {
    // Restauración de mejor esfuerzo: no dejar el sensor a medias.
    s->set_framesize(s, oldFs);
    s->set_quality(s, oldQuality);
    refreshCache();
    if (errOut) snprintf(errOut, errLen, "no se pudo cambiar calidad JPEG");
    return false;
  }

  s_cfg.frame_size = fs;
  s_cfg.jpeg_quality = quality;
  applySensorTweaks(fs);
  return true;
}

static void applySensorTweaks(framesize_t fs) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  // Ajustes conservadores: mejoran nitidez/latencia sin tocar nada exótico.
  if (s->set_gainceiling) s->set_gainceiling(s, GAINCEILING_2X);
  if (s->set_whitebal)    s->set_whitebal(s, 1);
  if (s->set_awb_gain)    s->set_awb_gain(s, 1);
  if (s->set_exposure_ctrl) s->set_exposure_ctrl(s, 1);
  if (s->set_gain_ctrl)   s->set_gain_ctrl(s, 1);
  if (s->set_bpc)         s->set_bpc(s, 1);
  if (s->set_wpc)         s->set_wpc(s, 1);
  if (s->set_lenc)        s->set_lenc(s, 1);
  (void)fs;
  refreshCache();
}

// Descarta N fotogramas para que el sensor asiente tras un cambio de tamaño.
static void settle(int frames) {
  for (int i = 0; i < frames; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

bool fcCameraBegin() {
  if (!s_camMutex) s_camMutex = xSemaphoreCreateMutex();
  if (!s_camMutex) { setError("sin memoria para el mutex"); return false; }

  const FcModeSpec* spec = &FC_MODES[FC_MODE_DEFAULT];
  const bool hasPsram = psramFound();
  const uint8_t fb = hasPsram ? 2 : 1;
  if (!hasPsram) {
    Serial.println(F("[CAM] AVISO: no se detecta PSRAM. Sin PSRAM el OV5640 no "
                     "puede pasar de resoluciones bajas. Revisa que en el IDE "
                     "esté 'PSRAM: OPI PSRAM'."));
  }

  // Con PSRAM se reserva desde el inicio para QSXGA. Esto es intencional:
  // set_framesize() puede reducir/aumentar luego la salida del sensor sin
  // necesitar la API no pública esp_camera_reconfigure().
  const framesize_t initialFs = hasPsram ? FRAMESIZE_QSXGA : spec->preview;
  s_bufferMaxFs = initialFs;
  fillConfig(s_cfg, initialFs, spec->previewQuality, fb);
  esp_err_t err = esp_camera_init(&s_cfg);
  if (err != ESP_OK) {
    char b[64];
    snprintf(b, sizeof(b), "esp_camera_init falló: 0x%x", (int)err);
    setError(b);
    Serial.printf("[CAM] %s\n", b);
    Serial.println(F("[CAM] Causas típicas: perfil de pines equivocado, cámara "
                     "mal insertada o PSRAM desactivada en el menú del IDE."));
    s_ready = false;
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s_pid = s->id.PID;
    Serial.printf("[CAM] PID sensor=0x%04X %s\n", s_pid,
                  (s_pid == OV5640_PID) ? "(OV5640 correcto)" : "(NO es un OV5640)");
    if (s_pid != OV5640_PID) {
      Serial.println(F("[CAM] AVISO: el sensor detectado no es un OV5640. Los "
                       "modos de 5 MP no funcionarán con este sensor."));
    }
  }
  if (initialFs != spec->preview &&
      !configureSensor(spec->preview, spec->previewQuality, nullptr, 0)) {
    setError("no se pudo configurar el preview inicial");
    Serial.println(F("[CAM] No se pudo seleccionar la resolución inicial."));
    esp_camera_deinit();
    s_ready = false;
    return false;
  } else if (initialFs == spec->preview) {
    applySensorTweaks(spec->preview);
  }
  settle(FC_SETTLE_FRAMES);

  s_mode  = FC_MODE_DEFAULT;
  s_ready = true;
  setError(nullptr);
  Serial.printf("[CAM] Lista. Modo=%s  fb_count=%u  PSRAM libre=%u KB\n",
                spec->label, (unsigned)fb,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  return true;
}

bool fcCameraLock(uint32_t ms) {
  if (!s_camMutex) return false;
  return xSemaphoreTake(s_camMutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}

void fcCameraUnlock() {
  if (s_camMutex) xSemaphoreGive(s_camMutex);
}

uint32_t fcCameraStreamGen() { return s_streamGen.load(std::memory_order_relaxed); }
void     fcCameraBumpStreamGen() { s_streamGen.fetch_add(1, std::memory_order_relaxed); }

bool fcCameraSetMode(FcMode m, char* errOut, size_t errLen) {
  if (m < 0 || m >= FC_MODE_COUNT) {
    if (errOut) snprintf(errOut, errLen, "modo desconocido");
    return false;
  }
  if (!s_ready) {
    if (errOut) snprintf(errOut, errLen, "la cámara no está iniciada");
    return false;
  }
  if (m == s_mode) return true;

  const FcModeSpec* spec = &FC_MODES[m];

  // 1) Cerrar los streams vivos ANTES de tocar el driver.
  fcCameraBumpStreamGen();

  // 2) Tomar el bus. Si un cliente lento lo tiene, se avisa y no se fuerza.
  if (!fcCameraLock(FC_CAM_LOCK_MS)) {
    if (errOut) snprintf(errOut, errLen, "cámara ocupada, inténtalo otra vez");
    return false;
  }

  // Guardar el modo anterior por si el sensor rechaza el cambio.
  const FcModeSpec* oldSpec = &FC_MODES[s_mode];
  bool ok = configureSensor(spec->preview, spec->previewQuality, errOut, errLen);
  if (ok) {
    settle(FC_SETTLE_FRAMES);
    s_mode = m;
    portENTER_CRITICAL(&s_statLock);
    s_fps = 0.0f; s_fpsWindowCount = 0; s_fpsWindowStart = millis();
    portEXIT_CRITICAL(&s_statLock);
    setError(nullptr);
  } else {
    // configureSensor ya revierte en caso de fallo de calidad. Esta segunda
    // restauración cubre sensores que alteren parcialmente el tamaño.
    configureSensor(oldSpec->preview, oldSpec->previewQuality, nullptr, 0);
  }
  fcCameraUnlock();
  return ok;
}

FcMode fcCameraGetMode() { return s_mode; }

const FcModeSpec* fcCameraModeSpec(FcMode m) {
  if (m < 0 || m >= FC_MODE_COUNT) return &FC_MODES[FC_MODE_DEFAULT];
  return &FC_MODES[m];
}

void fcCameraGetStatus(FcCamStatus* out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  out->ready      = s_ready;
  out->sensorPid  = s_pid;
  out->mode       = s_mode;
  out->streamGen  = fcCameraStreamGen();
  portENTER_CRITICAL(&s_statLock);
  out->fps        = s_fps;
  out->clients    = s_clients;
  out->framesSent = s_frames;
  out->dropped    = s_dropped;
  portEXIT_CRITICAL(&s_statLock);
  out->width      = s_curW;
  out->height     = s_curH;
  strncpy(out->lastError, s_lastError, sizeof(out->lastError) - 1);
}

void fcCameraNoteFrame(size_t bytes) {
  (void)bytes;
  portENTER_CRITICAL(&s_statLock);
  s_frames++;
  s_fpsWindowCount++;
  uint32_t now = millis();
  if (s_fpsWindowStart == 0) s_fpsWindowStart = now;
  uint32_t dt = now - s_fpsWindowStart;
  if (dt >= 1000) {
    s_fps = (float)s_fpsWindowCount * 1000.0f / (float)dt;
    s_fpsWindowCount = 0;
    s_fpsWindowStart = now;
  }
  portEXIT_CRITICAL(&s_statLock);
}

void fcCameraNoteDrop() {
  portENTER_CRITICAL(&s_statLock);
  s_dropped++;
  portEXIT_CRITICAL(&s_statLock);
}

void fcCameraClientEnter() {
  portENTER_CRITICAL(&s_statLock);
  s_clients++;
  portEXIT_CRITICAL(&s_statLock);
}

void fcCameraClientExit() {
  portENTER_CRITICAL(&s_statLock);
  if (s_clients) s_clients--;
  if (s_clients == 0) { s_fps = 0.0f; s_fpsWindowCount = 0; s_fpsWindowStart = 0; }
  portEXIT_CRITICAL(&s_statLock);
}

// Vuelve al preview del modo actual. Debe llamarse con el mutex tomado.
static void restorePreview() {
  const FcModeSpec* spec = &FC_MODES[s_mode];
  if (spec->capture == spec->preview && spec->captureQuality == spec->previewQuality) return;
  char err[64];
  if (configureSensor(spec->preview, spec->previewQuality, err, sizeof(err))) {
    settle(1);
  } else {
    Serial.printf("[CAM] %s\n", err);
  }
}

// Contrato: si devuelve un puntero, el mutex de cámara queda TOMADO y hay que
// cerrar con fcCameraCaptureRelease(). Si devuelve nullptr, ya está liberado.
camera_fb_t* fcCameraCapture(char* errOut, size_t errLen) {
  if (!s_ready) {
    if (errOut) snprintf(errOut, errLen, "la camara no esta iniciada");
    return nullptr;
  }
  const FcModeSpec* spec = &FC_MODES[s_mode];
  bool needSwitch = (spec->capture != spec->preview) ||
                    (spec->captureQuality != spec->previewQuality);

  fcCameraBumpStreamGen();          // saca a los streams vivos
  if (!fcCameraLock(FC_CAM_LOCK_MS)) {
    if (errOut) snprintf(errOut, errLen, "camara ocupada");
    return nullptr;
  }

  if (needSwitch) {
    // Los buffers para QSXGA ya se reservaron al arrancar. Aquí sólo cambia
    // la salida del sensor, sin liberar memoria ni reinicializar la cámara.
    if (!configureSensor(spec->capture, spec->captureQuality, errOut, errLen)) {
      restorePreview();
      fcCameraUnlock();
      return nullptr;
    }
    settle(FC_SETTLE_FRAMES);
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb || fb->len == 0) {
    if (fb) esp_camera_fb_return(fb);
    if (errOut) snprintf(errOut, errLen, "el sensor no devolvio imagen");
    restorePreview();
    fcCameraUnlock();
    return nullptr;
  }
  return fb;
}

void fcCameraCaptureRelease(camera_fb_t* fb) {
  if (fb) esp_camera_fb_return(fb);
  restorePreview();
  fcCameraUnlock();
}

bool fcCameraSetFlip(bool vflip, bool hmirror) {
  if (!s_ready) return false;
  if (!fcCameraLock(1500)) return false;      // no tocar el sensor a ciegas
  sensor_t* s = esp_camera_sensor_get();
  bool ok = false;
  if (s) {
    if (s->set_vflip)   s->set_vflip(s, vflip ? 1 : 0);
    if (s->set_hmirror) s->set_hmirror(s, hmirror ? 1 : 0);
    refreshCache();
    ok = true;
  }
  fcCameraUnlock();
  return ok;
}

bool fcCameraGetFlip(bool* vflip, bool* hmirror) {
  if (vflip)   *vflip   = s_vflip;
  if (hmirror) *hmirror = s_hmirror;
  return s_ready;
}

// El sensor_t incluido en varias versiones estables de Arduino-ESP32 no expone
// la API de autofocus. Mantenerla desactivada evita depender de headers nuevos;
// el stream y las capturas continúan funcionando con lentes fijas o AF pasivo.
bool fcCameraAutofocusSupported() { return false; }

bool fcCameraTriggerAutofocus() {
  return false;
}
