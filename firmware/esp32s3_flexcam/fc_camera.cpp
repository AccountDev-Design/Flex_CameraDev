// =====================================================================
//  fc_camera.cpp — OV5640 con productor único y pool latest-frame.
//
//  Regla central: ninguna escritura Wi-Fi ocurre mientras el mutex de la
//  cámara está tomado. El productor copia el JPEG a PSRAM, devuelve el
//  framebuffer del driver y la red envía después desde un slot independiente.
// =====================================================================
#include "fc_camera.h"
#include "fc_sys.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <atomic>
#include <math.h>
#include <string.h>

const FcModeSpec FC_MODES[FC_MODE_COUNT] = {
  // id          etiqueta                  preview            captura           qP qC fps horizon
  { "photo5mp", "Foto 5 MP",             FRAMESIZE_SVGA,    FRAMESIZE_5MP,    14,  8, 24, false },
  { "live5mp",  "5 MP en vivo",          FRAMESIZE_5MP,     FRAMESIZE_5MP,    16, 10,  4, false },
  { "hiq",      "Alta calidad",          FRAMESIZE_UXGA,    FRAMESIZE_UXGA,   13, 10, 10, false },
  { "fluid",    "Vista fluida",          FRAMESIZE_SVGA,    FRAMESIZE_SVGA,   14, 10, 30, false },
  { "hlock",    "Horizon Lock",          FRAMESIZE_SVGA,    FRAMESIZE_SVGA,   14, 10, 30, true  },
  { "hlockul",  "Horizon Lock Ultra",    FRAMESIZE_VGA,     FRAMESIZE_VGA,    16, 12, 40, true  },
};

struct FrameSlot {
  uint8_t* data;
  size_t capacity;
  size_t len;
  uint16_t width;
  uint16_t height;
  uint32_t sequence;
  uint32_t capturedMs;
  uint16_t refs;
  bool valid;
  bool writing;
};

static SemaphoreHandle_t s_camMutex = nullptr;
static TaskHandle_t      s_captureTask = nullptr;
static portMUX_TYPE      s_statLock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE      s_poolLock = portMUX_INITIALIZER_UNLOCKED;

static std::atomic<bool>     s_ready{false};
static std::atomic<int>      s_mode{FC_MODE_DEFAULT};
static std::atomic<uint32_t> s_streamGen{1};
static std::atomic<uint32_t> s_sequence{0};

static uint16_t s_pid = 0;
static uint16_t s_curW = 0;
static uint16_t s_curH = 0;
static bool     s_vflip = false;
static bool     s_hmirror = false;
static char     s_lastError[64] = {0};

static uint32_t s_clients = 0;
static uint32_t s_framesCaptured = 0;
static uint32_t s_framesSent = 0;
static uint32_t s_dropped = 0;
static uint32_t s_poolDropped = 0;
static uint32_t s_lastFrameBytes = 0;
static uint32_t s_lastFrameMs = 0;

static uint32_t s_captureWindowStart = 0;
static uint32_t s_captureWindowCount = 0;
static uint32_t s_sendWindowStart = 0;
static uint32_t s_sendWindowCount = 0;
static float    s_captureFps = 0.0f;
static float    s_sendFps = 0.0f;
static float    s_captureMs = 0.0f;
static float    s_sendMs = 0.0f;
static float    s_temperatureC = NAN;
static uint8_t  s_effectiveTargetFps = FC_MODES[FC_MODE_DEFAULT].targetFps;
static uint8_t  s_thermalLevel = 0;

static FrameSlot s_pool[FC_STREAM_POOL_SLOTS] = {};
static uint8_t   s_poolCount = 0;
static size_t    s_poolCapacity = 0;

static camera_config_t s_cfg = {};
static framesize_t s_bufferMaxFs = FRAMESIZE_INVALID;

static inline FcMode currentMode() {
  int m = s_mode.load(std::memory_order_relaxed);
  return (m >= 0 && m < FC_MODE_COUNT) ? (FcMode)m : FC_MODE_DEFAULT;
}

static void setError(const char* msg) {
  portENTER_CRITICAL(&s_statLock);
  if (!msg) {
    s_lastError[0] = 0;
  } else {
    strncpy(s_lastError, msg, sizeof(s_lastError) - 1);
    s_lastError[sizeof(s_lastError) - 1] = 0;
  }
  portEXIT_CRITICAL(&s_statLock);
}

static void fillConfig(camera_config_t& c, framesize_t fs, int quality,
                       uint8_t fbCount) {
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
  c.pixel_format  = PIXFORMAT_JPEG;
  c.frame_size    = fs;
  c.jpeg_quality  = quality;
  c.fb_count      = fbCount;
  c.fb_location   = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  c.grab_mode     = (fbCount > 1) ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;
  c.sccb_i2c_port = -1;
}

// El core 3.3.11 limita el OV5640 a QSXGA; versiones nuevas pueden aceptar
// FRAMESIZE_5MP. Tras iniciar se conserva el máximo realmente aceptado.
static framesize_t effectiveFs(framesize_t requested) {
  framesize_t maxFs = s_bufferMaxFs;
  if (maxFs == FRAMESIZE_INVALID) maxFs = FRAMESIZE_QSXGA;
  return (requested > maxFs) ? maxFs : requested;
}

static void sizeForFs(framesize_t fs, uint16_t* width, uint16_t* height) {
  fs = effectiveFs(fs);
  if (fs >= FRAMESIZE_INVALID) fs = FRAMESIZE_SVGA;
  if (width)  *width = resolution[fs].width;
  if (height) *height = resolution[fs].height;
}

void fcCameraModePreviewSize(FcMode mode, uint16_t* width, uint16_t* height) {
  sizeForFs(fcCameraModeSpec(mode)->preview, width, height);
}

void fcCameraModeCaptureSize(FcMode mode, uint16_t* width, uint16_t* height) {
  sizeForFs(fcCameraModeSpec(mode)->capture, width, height);
}

// Llamar con el mutex de cámara tomado, salvo durante el arranque monohilo.
static void refreshCache() {
  sensor_t* s = esp_camera_sensor_get();
  uint16_t w = 0, h = 0;
  bool vf = false, hm = false;
  if (s) {
    framesize_t fs = s->status.framesize;
    if (fs < FRAMESIZE_INVALID) {
      w = resolution[fs].width;
      h = resolution[fs].height;
    }
    vf = s->status.vflip != 0;
    hm = s->status.hmirror != 0;
  }
  portENTER_CRITICAL(&s_statLock);
  s_curW = w;
  s_curH = h;
  s_vflip = vf;
  s_hmirror = hm;
  portEXIT_CRITICAL(&s_statLock);
}

static void applySensorTweaks(framesize_t fs) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  if (s->set_gainceiling)   s->set_gainceiling(s, GAINCEILING_2X);
  if (s->set_whitebal)      s->set_whitebal(s, 1);
  if (s->set_awb_gain)      s->set_awb_gain(s, 1);
  if (s->set_exposure_ctrl) s->set_exposure_ctrl(s, 1);
  if (s->set_gain_ctrl)     s->set_gain_ctrl(s, 1);
  if (s->set_bpc)           s->set_bpc(s, 1);
  if (s->set_wpc)           s->set_wpc(s, 1);
  if (s->set_lenc)          s->set_lenc(s, 1);
  (void)fs;
  refreshCache();
}

static bool configureSensor(framesize_t requested, int quality,
                            char* errOut, size_t errLen) {
  framesize_t fs = effectiveFs(requested);
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

static void settle(int frames) {
  for (int i = 0; i < frames; ++i) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void freePool() {
  for (uint8_t i = 0; i < FC_STREAM_POOL_SLOTS; ++i) {
    if (s_pool[i].data) heap_caps_free(s_pool[i].data);
    s_pool[i] = {};
  }
  s_poolCount = 0;
  s_poolCapacity = 0;
}

static bool initPool(uint16_t maxW, uint16_t maxH, bool hasPsram) {
  freePool();
  s_poolCapacity = ((size_t)maxW * (size_t)maxH) / 5U + FC_STREAM_SLOT_HEADROOM;
  uint32_t caps = MALLOC_CAP_8BIT | (hasPsram ? MALLOC_CAP_SPIRAM : 0);
  for (uint8_t i = 0; i < FC_STREAM_POOL_SLOTS; ++i) {
    uint8_t* p = (uint8_t*)heap_caps_malloc(s_poolCapacity, caps);
    if (!p) break;
    s_pool[i].data = p;
    s_pool[i].capacity = s_poolCapacity;
    ++s_poolCount;
  }
  if (s_poolCount == 0) {
    setError("sin memoria para el pool de vídeo");
    return false;
  }
  Serial.printf("[CAM] Pool latest-frame: %u slots x %u KB\n",
                (unsigned)s_poolCount, (unsigned)(s_poolCapacity / 1024));
  if (s_poolCount < 2) {
    Serial.println(F("[CAM] AVISO: sólo hay un slot; un cliente lento bajará los FPS."));
  }
  return true;
}

static void flushPool() {
  portENTER_CRITICAL(&s_poolLock);
  for (uint8_t i = 0; i < s_poolCount; ++i) s_pool[i].valid = false;
  portEXIT_CRITICAL(&s_poolLock);
}

static int reservePoolSlot() {
  int chosen = -1;
  uint32_t oldest = UINT32_MAX;
  portENTER_CRITICAL(&s_poolLock);
  for (uint8_t i = 0; i < s_poolCount; ++i) {
    FrameSlot& slot = s_pool[i];
    if (slot.refs != 0 || slot.writing) continue;
    if (!slot.valid) { chosen = i; break; }
    if (slot.sequence < oldest) { oldest = slot.sequence; chosen = i; }
  }
  if (chosen >= 0) s_pool[chosen].writing = true;
  portEXIT_CRITICAL(&s_poolLock);
  return chosen;
}

static void cancelPoolWrite(uint8_t index) {
  portENTER_CRITICAL(&s_poolLock);
  if (index < s_poolCount) s_pool[index].writing = false;
  portEXIT_CRITICAL(&s_poolLock);
}

static void publishPoolSlot(uint8_t index, size_t len, uint16_t width,
                            uint16_t height, uint32_t capturedMs) {
  uint32_t seq = s_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
  portENTER_CRITICAL(&s_poolLock);
  FrameSlot& slot = s_pool[index];
  slot.len = len;
  slot.width = width;
  slot.height = height;
  slot.sequence = seq;
  slot.capturedMs = capturedMs;
  slot.valid = true;
  slot.writing = false;
  portEXIT_CRITICAL(&s_poolLock);
}

bool fcCameraAcquireLatest(uint32_t afterSequence, FcStreamFrame* out) {
  if (!out) return false;
  int best = -1;
  uint32_t bestSeq = afterSequence;
  portENTER_CRITICAL(&s_poolLock);
  for (uint8_t i = 0; i < s_poolCount; ++i) {
    FrameSlot& slot = s_pool[i];
    // Sólo secuencias posteriores: sin esto el handler podría volver a enviar
    // un slot viejo mientras espera el próximo frame del productor.
    if (!slot.valid || slot.writing ||
        (int32_t)(slot.sequence - afterSequence) <= 0) continue;
    if (best < 0 || (int32_t)(slot.sequence - bestSeq) > 0) {
      best = i;
      bestSeq = slot.sequence;
    }
  }
  if (best >= 0) {
    FrameSlot& slot = s_pool[best];
    ++slot.refs;
    out->data = slot.data;
    out->len = slot.len;
    out->width = slot.width;
    out->height = slot.height;
    out->sequence = slot.sequence;
    out->capturedMs = slot.capturedMs;
    out->slot = (uint8_t)best;
  }
  portEXIT_CRITICAL(&s_poolLock);
  return best >= 0;
}

void fcCameraReleaseFrame(const FcStreamFrame* frame) {
  if (!frame) return;
  portENTER_CRITICAL(&s_poolLock);
  if (frame->slot < s_poolCount && s_pool[frame->slot].refs > 0) {
    --s_pool[frame->slot].refs;
  }
  portEXIT_CRITICAL(&s_poolLock);
}

static uint32_t clientCount() {
  portENTER_CRITICAL(&s_statLock);
  uint32_t count = s_clients;
  portEXIT_CRITICAL(&s_statLock);
  return count;
}

static void noteCapture(uint32_t elapsedUs, size_t bytes, uint32_t capturedMs) {
  portENTER_CRITICAL(&s_statLock);
  ++s_framesCaptured;
  ++s_captureWindowCount;
  s_lastFrameBytes = (uint32_t)bytes;
  s_lastFrameMs = capturedMs;
  float ms = (float)elapsedUs / 1000.0f;
  s_captureMs = (s_captureMs <= 0.0f) ? ms : (s_captureMs * 0.85f + ms * 0.15f);
  if (s_captureWindowStart == 0) s_captureWindowStart = capturedMs;
  uint32_t dt = capturedMs - s_captureWindowStart;
  if (dt >= 1000) {
    s_captureFps = (float)s_captureWindowCount * 1000.0f / (float)dt;
    s_captureWindowCount = 0;
    s_captureWindowStart = capturedMs;
  }
  portEXIT_CRITICAL(&s_statLock);
}

static void notePoolDrop() {
  portENTER_CRITICAL(&s_statLock);
  ++s_poolDropped;
  portEXIT_CRITICAL(&s_statLock);
}

static uint8_t updateThermalAndTarget(uint32_t now) {
  static uint32_t lastSample = 0;
  if (lastSample == 0 || now - lastSample >= FC_THERMAL_SAMPLE_MS) {
    lastSample = now;
    float temp = fcSysTemperatureC();
    if (isfinite(temp) && temp > -20.0f && temp < 150.0f) {
      portENTER_CRITICAL(&s_statLock);
      s_temperatureC = temp;
      if (s_thermalLevel == 2) {
        if (temp < FC_THERMAL_CRITICAL_C - 3.0f) s_thermalLevel = 1;
      } else if (s_thermalLevel == 1) {
        if (temp >= FC_THERMAL_CRITICAL_C) s_thermalLevel = 2;
        else if (temp < FC_THERMAL_THROTTLE_C - 3.0f) s_thermalLevel = 0;
      } else if (temp >= FC_THERMAL_CRITICAL_C) {
        s_thermalLevel = 2;
      } else if (temp >= FC_THERMAL_THROTTLE_C) {
        s_thermalLevel = 1;
      }
      portEXIT_CRITICAL(&s_statLock);
    }
  }

  const uint8_t base = FC_MODES[currentMode()].targetFps;
  portENTER_CRITICAL(&s_statLock);
  uint8_t target = base;
  if (s_thermalLevel == 2 && target > FC_THERMAL_CRITICAL_FPS)
    target = FC_THERMAL_CRITICAL_FPS;
  else if (s_thermalLevel == 1 && target > FC_THERMAL_THROTTLE_FPS)
    target = FC_THERMAL_THROTTLE_FPS;
  s_effectiveTargetFps = target;
  portEXIT_CRITICAL(&s_statLock);
  return target ? target : 1;
}

static void captureProducerTask(void* arg) {
  (void)arg;
  for (;;) {
    uint32_t now = millis();
    uint8_t targetFps = updateThermalAndTarget(now);
    if (!s_ready.load(std::memory_order_relaxed) || clientCount() == 0) {
      vTaskDelay(pdMS_TO_TICKS(FC_CAMERA_IDLE_WAIT_MS));
      continue;
    }

    uint64_t startedUs = esp_timer_get_time();
    if (!fcCameraLock(500)) {
      vTaskDelay(pdMS_TO_TICKS(3));
      continue;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb || fb->len == 0 || fb->format != PIXFORMAT_JPEG) {
      if (fb) esp_camera_fb_return(fb);
      fcCameraUnlock();
      fcCameraNoteDrop();
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    int index = reservePoolSlot();
    if (index < 0 || fb->len > s_poolCapacity) {
      if (index >= 0) cancelPoolWrite((uint8_t)index);
      if (fb->len > s_poolCapacity) setError("JPEG mayor que el pool reservado");
      esp_camera_fb_return(fb);
      fcCameraUnlock();
      notePoolDrop();
    } else {
      memcpy(s_pool[index].data, fb->buf, fb->len);
      const size_t len = fb->len;
      const uint16_t width = fb->width;
      const uint16_t height = fb->height;
      const uint32_t capturedMs = millis();
      esp_camera_fb_return(fb);
      publishPoolSlot((uint8_t)index, len, width, height, capturedMs);
      fcCameraUnlock();
      noteCapture((uint32_t)(esp_timer_get_time() - startedUs), len, capturedMs);
    }

    uint32_t elapsedMs = (uint32_t)((esp_timer_get_time() - startedUs) / 1000ULL);
    uint32_t periodMs = 1000U / targetFps;
    if (elapsedMs < periodMs) vTaskDelay(pdMS_TO_TICKS(periodMs - elapsedMs));
    else taskYIELD();
  }
}

bool fcCameraBegin() {
  if (s_ready.load(std::memory_order_relaxed)) return true;
  if (!s_camMutex) s_camMutex = xSemaphoreCreateMutex();
  if (!s_camMutex) { setError("sin memoria para el mutex"); return false; }

  const FcModeSpec* spec = &FC_MODES[FC_MODE_DEFAULT];
  const bool hasPsram = psramFound();
  const uint8_t fbCount = hasPsram ? 2 : 1;
  if (!hasPsram) {
    Serial.println(F("[CAM] AVISO: no se detecta PSRAM. Selecciona 'OPI PSRAM'."));
  }

  const framesize_t requestedMax = hasPsram ? FRAMESIZE_5MP : spec->preview;
  fillConfig(s_cfg, requestedMax, spec->previewQuality, fbCount);
  esp_err_t err = esp_camera_init(&s_cfg);
  if (err != ESP_OK) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "esp_camera_init falló: 0x%x", (int)err);
    setError(buffer);
    Serial.printf("[CAM] %s\n", buffer);
    Serial.println(F("[CAM] Revisa pines, flex y que PSRAM sea OPI PSRAM."));
    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor || sensor->status.framesize >= FRAMESIZE_INVALID) {
    setError("el driver no publicó el estado del sensor");
    esp_camera_deinit();
    return false;
  }
  s_bufferMaxFs = sensor->status.framesize;
  s_pid = sensor->id.PID;
  Serial.printf("[CAM] PID sensor=0x%04X %s · máximo driver=%ux%u\n", s_pid,
                (s_pid == OV5640_PID) ? "(OV5640 correcto)" : "(sensor distinto)",
                resolution[s_bufferMaxFs].width, resolution[s_bufferMaxFs].height);

  if (!configureSensor(spec->preview, spec->previewQuality, nullptr, 0)) {
    setError("no se pudo configurar el preview inicial");
    esp_camera_deinit();
    s_bufferMaxFs = FRAMESIZE_INVALID;
    return false;
  }
  settle(FC_SETTLE_FRAMES);

  uint16_t maxW = resolution[s_bufferMaxFs].width;
  uint16_t maxH = resolution[s_bufferMaxFs].height;
  if (!initPool(maxW, maxH, hasPsram)) {
    esp_camera_deinit();
    s_bufferMaxFs = FRAMESIZE_INVALID;
    return false;
  }

  s_mode.store(FC_MODE_DEFAULT, std::memory_order_relaxed);
  s_ready.store(true, std::memory_order_relaxed);
  if (!s_captureTask &&
      xTaskCreatePinnedToCore(captureProducerTask, "fc_capture",
                             FC_CAMERA_TASK_STACK, nullptr, 4,
                             &s_captureTask, 1) != pdPASS) {
    s_ready.store(false, std::memory_order_relaxed);
    setError("no se pudo crear la tarea de cámara");
    freePool();
    esp_camera_deinit();
    return false;
  }

  setError(nullptr);
  Serial.printf("[CAM] Lista. Modo=%s · fb=%u · PSRAM libre=%u KB\n",
                spec->label, (unsigned)fbCount,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  return true;
}

bool fcCameraLock(uint32_t ms) {
  return s_camMutex && xSemaphoreTake(s_camMutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}

void fcCameraUnlock() {
  if (s_camMutex) xSemaphoreGive(s_camMutex);
}

uint32_t fcCameraStreamGen() {
  return s_streamGen.load(std::memory_order_relaxed);
}

void fcCameraBumpStreamGen() {
  s_streamGen.fetch_add(1, std::memory_order_relaxed);
}

bool fcCameraSetMode(FcMode mode, char* errOut, size_t errLen) {
  if ((int)mode < 0 || mode >= FC_MODE_COUNT) {
    if (errOut) snprintf(errOut, errLen, "modo desconocido");
    return false;
  }
  if (!s_ready.load(std::memory_order_relaxed)) {
    if (errOut) snprintf(errOut, errLen, "la cámara no está iniciada");
    return false;
  }
  FcMode oldMode = currentMode();
  if (mode == oldMode) return true;

  fcCameraBumpStreamGen();
  if (!fcCameraLock(FC_CAM_LOCK_MS)) {
    if (errOut) snprintf(errOut, errLen, "cámara ocupada, inténtalo otra vez");
    return false;
  }

  const FcModeSpec* spec = &FC_MODES[mode];
  const FcModeSpec* oldSpec = &FC_MODES[oldMode];
  bool ok = configureSensor(spec->preview, spec->previewQuality, errOut, errLen);
  if (ok) {
    settle(FC_SETTLE_FRAMES);
    flushPool();
    s_mode.store(mode, std::memory_order_relaxed);
    portENTER_CRITICAL(&s_statLock);
    s_captureFps = 0.0f;
    s_sendFps = 0.0f;
    s_captureWindowCount = s_sendWindowCount = 0;
    s_captureWindowStart = s_sendWindowStart = millis();
    portEXIT_CRITICAL(&s_statLock);
    setError(nullptr);
  } else {
    configureSensor(oldSpec->preview, oldSpec->previewQuality, nullptr, 0);
  }
  fcCameraUnlock();
  return ok;
}

FcMode fcCameraGetMode() { return currentMode(); }

const FcModeSpec* fcCameraModeSpec(FcMode mode) {
  if ((int)mode < 0 || mode >= FC_MODE_COUNT) return &FC_MODES[FC_MODE_DEFAULT];
  return &FC_MODES[mode];
}

uint8_t fcCameraTargetFps() {
  portENTER_CRITICAL(&s_statLock);
  uint8_t fps = s_effectiveTargetFps;
  portEXIT_CRITICAL(&s_statLock);
  return fps;
}

void fcCameraGetStatus(FcCamStatus* out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  out->ready = s_ready.load(std::memory_order_relaxed);
  out->mode = currentMode();
  out->streamGen = fcCameraStreamGen();
  uint32_t now = millis();
  portENTER_CRITICAL(&s_statLock);
  out->sensorPid = s_pid;
  out->width = s_curW;
  out->height = s_curH;
  out->fps = s_captureFps;
  out->sendFps = s_sendFps;
  out->captureMs = s_captureMs;
  out->sendMs = s_sendMs;
  out->temperatureC = s_temperatureC;
  out->targetFps = s_effectiveTargetFps;
  out->thermalLevel = s_thermalLevel;
  out->clients = s_clients;
  out->framesCaptured = s_framesCaptured;
  out->framesSent = s_framesSent;
  out->dropped = s_dropped;
  out->poolDropped = s_poolDropped;
  out->lastFrameBytes = s_lastFrameBytes;
  out->lastFrameAgeMs = s_lastFrameMs ? (now - s_lastFrameMs) : 0;
  strncpy(out->lastError, s_lastError, sizeof(out->lastError) - 1);
  portEXIT_CRITICAL(&s_statLock);
}

void fcCameraNoteFrame(size_t bytes, uint32_t sendMs) {
  (void)bytes;
  uint32_t now = millis();
  portENTER_CRITICAL(&s_statLock);
  ++s_framesSent;
  ++s_sendWindowCount;
  float ms = (float)sendMs;
  s_sendMs = (s_sendMs <= 0.0f) ? ms : (s_sendMs * 0.85f + ms * 0.15f);
  if (s_sendWindowStart == 0) s_sendWindowStart = now;
  uint32_t dt = now - s_sendWindowStart;
  if (dt >= 1000) {
    s_sendFps = (float)s_sendWindowCount * 1000.0f / (float)dt;
    s_sendWindowCount = 0;
    s_sendWindowStart = now;
  }
  portEXIT_CRITICAL(&s_statLock);
}

void fcCameraNoteDrop() {
  portENTER_CRITICAL(&s_statLock);
  ++s_dropped;
  portEXIT_CRITICAL(&s_statLock);
}

void fcCameraClientEnter() {
  bool first = false;
  portENTER_CRITICAL(&s_statLock);
  first = (s_clients == 0);
  ++s_clients;
  portEXIT_CRITICAL(&s_statLock);
  if (first) flushPool();
}

void fcCameraClientExit() {
  portENTER_CRITICAL(&s_statLock);
  if (s_clients > 0) --s_clients;
  if (s_clients == 0) {
    s_captureFps = 0.0f;
    s_sendFps = 0.0f;
    s_captureWindowCount = s_sendWindowCount = 0;
    s_captureWindowStart = s_sendWindowStart = 0;
  }
  portEXIT_CRITICAL(&s_statLock);
}

static void restorePreview() {
  const FcModeSpec* spec = &FC_MODES[currentMode()];
  char err[64] = {0};
  if (configureSensor(spec->preview, spec->previewQuality, err, sizeof(err))) {
    settle(1);
  } else {
    Serial.printf("[CAM] No se pudo restaurar preview: %s\n", err);
  }
}

bool fcCameraCapture(FcPhoto* out, char* errOut, size_t errLen) {
  if (out) *out = {};
  if (!out) return false;
  if (!s_ready.load(std::memory_order_relaxed)) {
    if (errOut) snprintf(errOut, errLen, "la cámara no está iniciada");
    return false;
  }

  const FcModeSpec* spec = &FC_MODES[currentMode()];
  framesize_t captureFs = effectiveFs(spec->capture);
  framesize_t previewFs = effectiveFs(spec->preview);
  bool needSwitch = captureFs != previewFs ||
                    spec->captureQuality != spec->previewQuality;

  fcCameraBumpStreamGen();
  if (!fcCameraLock(FC_CAM_LOCK_MS)) {
    if (errOut) snprintf(errOut, errLen, "cámara ocupada");
    return false;
  }

  if (needSwitch) {
    if (!configureSensor(captureFs, spec->captureQuality, errOut, errLen)) {
      restorePreview();
      fcCameraUnlock();
      return false;
    }
    settle(FC_SETTLE_FRAMES);
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb || fb->len == 0 || fb->format != PIXFORMAT_JPEG) {
    if (fb) esp_camera_fb_return(fb);
    if (errOut) snprintf(errOut, errLen, "el sensor no devolvió un JPEG válido");
    if (needSwitch) restorePreview();
    fcCameraUnlock();
    return false;
  }

  uint32_t caps = MALLOC_CAP_8BIT | (psramFound() ? MALLOC_CAP_SPIRAM : 0);
  uint8_t* copy = (uint8_t*)heap_caps_malloc(fb->len, caps);
  if (!copy) {
    esp_camera_fb_return(fb);
    if (errOut) snprintf(errOut, errLen, "sin PSRAM para copiar la fotografía");
    if (needSwitch) restorePreview();
    fcCameraUnlock();
    return false;
  }

  memcpy(copy, fb->buf, fb->len);
  out->data = copy;
  out->len = fb->len;
  out->width = fb->width;
  out->height = fb->height;
  esp_camera_fb_return(fb);
  if (needSwitch) restorePreview();
  flushPool();
  fcCameraUnlock();
  setError(nullptr);
  return true;
}

void fcCameraPhotoRelease(FcPhoto* photo) {
  if (!photo) return;
  if (photo->data) heap_caps_free(photo->data);
  *photo = {};
}

bool fcCameraSetFlip(bool vflip, bool hmirror) {
  if (!s_ready.load(std::memory_order_relaxed) || !fcCameraLock(1500)) return false;
  sensor_t* s = esp_camera_sensor_get();
  bool ok = false;
  if (s) {
    int vr = s->set_vflip ? s->set_vflip(s, vflip ? 1 : 0) : -1;
    int hr = s->set_hmirror ? s->set_hmirror(s, hmirror ? 1 : 0) : -1;
    refreshCache();
    flushPool();
    ok = (vr == 0 && hr == 0);
  }
  fcCameraUnlock();
  return ok;
}

bool fcCameraGetFlip(bool* vflip, bool* hmirror) {
  portENTER_CRITICAL(&s_statLock);
  if (vflip) *vflip = s_vflip;
  if (hmirror) *hmirror = s_hmirror;
  portEXIT_CRITICAL(&s_statLock);
  return s_ready.load(std::memory_order_relaxed);
}

// Arduino-ESP32 3.3.11 aún no expone los punteros AF del sensor_t. Mantener
// esto desactivado conserva compatibilidad; no se inventa un autofocus falso.
