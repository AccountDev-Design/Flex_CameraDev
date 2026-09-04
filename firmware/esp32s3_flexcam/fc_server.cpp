// =====================================================================
//  fc_server.cpp
// =====================================================================
#include "fc_server.h"
#include "fc_config.h"
#include "fc_camera.h"
#include "fc_imu.h"
#include "fc_web_ui.h"

#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#include <stdarg.h>
#include <math.h>

static httpd_handle_t s_web    = nullptr;
static httpd_handle_t s_stream = nullptr;
static TaskHandle_t   s_wsTask = nullptr;

static int  s_wsFds[FC_WS_MAX_CLIENTS];
static SemaphoreHandle_t s_wsMutex = nullptr;

#define PART_BOUNDARY "fcframe"
static const char* STREAM_CONTENT_TYPE =
  "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART =
  "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ---------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------
static void noCache(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

static bool queryValue(httpd_req_t* req, const char* key, char* out, size_t len) {
  size_t qlen = httpd_req_get_url_query_len(req) + 1;
  if (qlen <= 1 || qlen > 256) return false;
  char q[256];
  if (httpd_req_get_url_query_str(req, q, qlen) != ESP_OK) return false;
  return httpd_query_key_value(q, key, out, len) == ESP_OK;
}

// snprintf acumulativo que no se puede pasar de largo: snprintf() devuelve
// los bytes que HABRIA escrito, asi que sumarlo a ciegas desborda el hueco
// restante en cuanto hay truncamiento.
static void appendf(char* buf, size_t cap, size_t* n, const char* fmt, ...) {
  if (*n >= cap - 1) return;
  va_list ap;
  va_start(ap, fmt);
  int w = vsnprintf(buf + *n, cap - *n, fmt, ap);
  va_end(ap);
  if (w < 0) return;
  size_t written = (size_t)w;
  *n += (written < (cap - *n)) ? written : (cap - *n - 1);
}

// Escribe el JSON de estado completo. Devuelve los bytes usados.
static size_t buildState(char* buf, size_t cap, const char* errMsg) {
  FcCamStatus cs;
  fcCameraGetStatus(&cs);
  const FcModeSpec* spec = fcCameraModeSpec(cs.mode);
  bool vf = false, hm = false;
  fcCameraGetFlip(&vf, &hm);

  size_t n = 0;
  appendf(buf, cap, &n, "{\"mode\":%d,\"label\":\"%s\",\"w\":%u,\"h\":%u,"
          "\"camReady\":%s,\"pid\":%u,\"fps\":%.1f,\"sendFps\":%.1f,"
          "\"targetFps\":%u,\"flip\":%d,\"af\":%d,\"modes\":[",
          (int)cs.mode, spec->label, cs.width, cs.height,
          cs.ready ? "true" : "false", cs.sensorPid, cs.fps, cs.sendFps,
          (unsigned)cs.targetFps,
          vf ? 1 : 0, fcCameraAutofocusSupported() ? 1 : 0);
  for (int i = 0; i < FC_MODE_COUNT; i++) {
    const FcModeSpec* m = &FC_MODES[i];
    uint16_t cw = 0, ch = 0, pw = 0, ph = 0;
    fcCameraModeCaptureSize((FcMode)i, &cw, &ch);
    fcCameraModePreviewSize((FcMode)i, &pw, &ph);
    appendf(buf, cap, &n,
            "%s{\"id\":\"%s\",\"label\":\"%s\",\"prev\":\"%ux%u\","
            "\"captureLabel\":\"%ux%u\",\"targetFps\":%u,\"horizon\":%s}",
            i ? "," : "", m->id, m->label, pw, ph, cw, ch,
            (unsigned)m->targetFps,
            m->horizonHint ? "true" : "false");
  }
  appendf(buf, cap, &n, "]");
  if (errMsg && errMsg[0])   appendf(buf, cap, &n, ",\"error\":\"%s\"", errMsg);
  else if (cs.lastError[0])  appendf(buf, cap, &n, ",\"error\":\"%s\"", cs.lastError);
  appendf(buf, cap, &n, "}");
  return n;
}

static esp_err_t sendState(httpd_req_t* req, const char* errMsg) {
  char* buf = (char*)malloc(2600);
  if (!buf) { httpd_resp_send_500(req); return ESP_FAIL; }
  size_t n = buildState(buf, 2600, errMsg);
  noCache(req);
  httpd_resp_set_type(req, "application/json");
  esp_err_t r = httpd_resp_send(req, buf, n);
  free(buf);
  return r;
}

// ---------------------------------------------------------------------
// Handlers de la interfaz (:80)
// ---------------------------------------------------------------------
static esp_err_t indexHandler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, FC_INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stateHandler(httpd_req_t* req) { return sendState(req, nullptr); }

static esp_err_t modeHandler(httpd_req_t* req) {
  char id[24] = {0};
  if (!queryValue(req, "m", id, sizeof(id))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "falta el parametro m");
    return ESP_FAIL;
  }
  int found = -1;
  for (int i = 0; i < FC_MODE_COUNT; i++)
    if (strcmp(FC_MODES[i].id, id) == 0) { found = i; break; }
  if (found < 0) return sendState(req, "modo desconocido");

  char err[80] = {0};
  bool ok = fcCameraSetMode((FcMode)found, err, sizeof(err));
  return sendState(req, ok ? nullptr : err);
}

static esp_err_t flipHandler(httpd_req_t* req) {
  char v[8] = {0};
  bool on = queryValue(req, "v", v, sizeof(v)) && v[0] == '1';
  fcCameraSetFlip(on, on);
  return sendState(req, nullptr);
}

static esp_err_t photoHandler(httpd_req_t* req) {
  char err[80] = {0};
  FcPhoto photo = {};
  if (!fcCameraCapture(&photo, err, sizeof(err))) {
    noCache(req);
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_send(req, err[0] ? err : "no se pudo capturar", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  char disp[96];
  char width[12], height[12], length[20];
  snprintf(disp, sizeof(disp), "inline; filename=flexcam_%ux%u_%lu.jpg",
           photo.width, photo.height, (unsigned long)(millis() / 1000));
  snprintf(width, sizeof(width), "%u", photo.width);
  snprintf(height, sizeof(height), "%u", photo.height);
  snprintf(length, sizeof(length), "%u", (unsigned)photo.len);
  noCache(req);
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", disp);
  httpd_resp_set_hdr(req, "X-FlexCam-Width", width);
  httpd_resp_set_hdr(req, "X-FlexCam-Height", height);
  httpd_resp_set_hdr(req, "X-FlexCam-Bytes", length);
  esp_err_t r = httpd_resp_send(req, (const char*)photo.data, photo.len);
  fcCameraPhotoRelease(&photo);
  return r;
}

// ---------------------------------------------------------------------
// WebSocket de telemetría (:80 /ws)
// ---------------------------------------------------------------------
static void wsAdd(int fd) {
  if (!s_wsMutex) return;
  xSemaphoreTake(s_wsMutex, portMAX_DELAY);
  for (int i = 0; i < FC_WS_MAX_CLIENTS; i++) if (s_wsFds[i] == fd) { xSemaphoreGive(s_wsMutex); return; }
  for (int i = 0; i < FC_WS_MAX_CLIENTS; i++) if (s_wsFds[i] < 0) { s_wsFds[i] = fd; break; }
  xSemaphoreGive(s_wsMutex);
}

static void wsRemove(int fd) {
  if (!s_wsMutex) return;
  xSemaphoreTake(s_wsMutex, portMAX_DELAY);
  for (int i = 0; i < FC_WS_MAX_CLIENTS; i++) if (s_wsFds[i] == fd) s_wsFds[i] = -1;
  xSemaphoreGive(s_wsMutex);
}

uint32_t fcServerWsClients() {
  uint32_t n = 0;
  if (!s_wsMutex) return 0;
  xSemaphoreTake(s_wsMutex, portMAX_DELAY);
  for (int i = 0; i < FC_WS_MAX_CLIENTS; i++) if (s_wsFds[i] >= 0) n++;
  xSemaphoreGive(s_wsMutex);
  return n;
}

// Se llama cuando el propio servidor cierra un socket: así un cliente que se
// va (pestaña cerrada, wifi caído) no deja el descriptor colgado en la lista.
static void onSocketClose(httpd_handle_t hd, int sockfd) {
  wsRemove(sockfd);
  close(sockfd);
  (void)hd;
}

static esp_err_t wsHandler(httpd_req_t* req) {
  if (req->method == HTTP_GET) {           // handshake
    wsAdd(httpd_req_to_sockfd(req));
    return ESP_OK;
  }
  // Cualquier trama entrante se lee y se descarta: el canal es de bajada.
  httpd_ws_frame_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.type = HTTPD_WS_TYPE_TEXT;
  esp_err_t r = httpd_ws_recv_frame(req, &pkt, 0);
  if (r != ESP_OK) return r;
  if (pkt.len > 0 && pkt.len < 256) {
    uint8_t tmp[256];
    pkt.payload = tmp;
    httpd_ws_recv_frame(req, &pkt, pkt.len);
  }
  if (pkt.type == HTTPD_WS_TYPE_CLOSE) wsRemove(httpd_req_to_sockfd(req));
  return ESP_OK;
}

static void wsBroadcast(const char* text, size_t len) {
  if (!s_web || !s_wsMutex) return;
  int fds[FC_WS_MAX_CLIENTS];
  xSemaphoreTake(s_wsMutex, portMAX_DELAY);
  memcpy(fds, s_wsFds, sizeof(fds));
  xSemaphoreGive(s_wsMutex);

  httpd_ws_frame_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.type    = HTTPD_WS_TYPE_TEXT;
  pkt.payload = (uint8_t*)text;
  pkt.len     = len;

  for (int i = 0; i < FC_WS_MAX_CLIENTS; i++) {
    int fd = fds[i];
    if (fd < 0) continue;
    // Comprobar que el descriptor sigue siendo un WebSocket vivo evita
    // escribir sobre un socket ya reciclado por otra conexión.
    if (httpd_ws_get_fd_info(s_web, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
      wsRemove(fd);
      continue;
    }
    if (httpd_ws_send_frame_async(s_web, fd, &pkt) != ESP_OK) wsRemove(fd);
  }
}

static void telemetryTask(void* arg) {
  (void)arg;
  const TickType_t period = pdMS_TO_TICKS(1000 / FC_WS_IMU_HZ);
  uint32_t lastStats = 0;
  uint32_t lastImuSequence = UINT32_MAX;
  uint32_t lastImuBroadcast = 0;
  char buf[640];
  for (;;) {
    if (fcServerWsClients() == 0) {        // nadie mirando: no gastar CPU
      vTaskDelay(pdMS_TO_TICKS(120));
      continue;
    }
    FcImuSample s;
    fcImuGet(&s);
    int n = 0;
    uint32_t now = millis();
    // No llenar la cola TCP repitiendo la misma muestra. Si llegan 100 Hz del
    // BNO085 se envía siempre la más nueva, con techo de FC_WS_IMU_HZ.
    if (s.sequence != lastImuSequence || now - lastImuBroadcast >= 500) {
      lastImuSequence = s.sequence;
      lastImuBroadcast = now;
      n = snprintf(buf, sizeof(buf),
          "{\"t\":\"i\",\"r\":%.2f,\"p\":%.2f,\"y\":%.2f,"
          "\"h\":%.3f,\"hw\":%.3f,\"hc\":%.3f,\"hv\":%d,"
          "\"qi\":%.5f,\"qj\":%.5f,\"qk\":%.5f,\"qr\":%.5f,"
          "\"gx\":%.4f,\"gy\":%.4f,\"gz\":%.4f,"
          "\"hz\":%.0f,\"a\":%u,\"ok\":%d,\"s\":\"%s\","
          "\"rs\":%lu,\"seq\":%lu}",
          s.roll, s.pitch, s.yaw, s.horizon, s.horizonWrapped,
          s.horizonConfidence, s.horizonValid ? 1 : 0,
          s.qi, s.qj, s.qk, s.qr,
          s.gravityX, s.gravityY, s.gravityZ,
          s.hz, (unsigned)s.accuracy,
          (s.state == FC_IMU_OK) ? 1 : 0, fcImuStateText(s.state),
          (unsigned long)s.resets, (unsigned long)s.sequence);
      if (n > 0 && (size_t)n < sizeof(buf)) wsBroadcast(buf, (size_t)n);
    }

    if (now - lastStats >= FC_WS_STATS_MS) {
      lastStats = now;
      FcCamStatus cs;
      fcCameraGetStatus(&cs);
      n = snprintf(buf, sizeof(buf),
          "{\"t\":\"s\",\"fps\":%.1f,\"sfps\":%.1f,\"tfps\":%u,"
          "\"w\":%u,\"h\":%u,\"mode\":%d,\"cam\":%d,"
          "\"cli\":%lu,\"heap\":%lu,\"largest\":%lu,\"ps\":%lu,"
          "\"drop\":%lu,\"pdrop\":%lu,\"capms\":%.1f,\"sendms\":%.1f,"
          "\"bytes\":%lu,\"age\":%lu,\"temp\":%.1f,\"thermal\":%u,"
          "\"captured\":%lu,\"sent\":%lu,\"up\":%lu}",
          cs.fps, cs.sendFps, (unsigned)cs.targetFps,
          cs.width, cs.height, (int)cs.mode, cs.ready ? 1 : 0,
          (unsigned long)cs.clients,
          (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
          (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
          (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
          (unsigned long)cs.dropped, (unsigned long)cs.poolDropped,
          cs.captureMs, cs.sendMs,
          (unsigned long)cs.lastFrameBytes, (unsigned long)cs.lastFrameAgeMs,
          isfinite(cs.temperatureC) ? cs.temperatureC : -127.0f,
          (unsigned)cs.thermalLevel,
          (unsigned long)cs.framesCaptured, (unsigned long)cs.framesSent,
          (unsigned long)(now / 1000));
      if (n > 0 && (size_t)n < sizeof(buf)) wsBroadcast(buf, (size_t)n);
    }
    vTaskDelay(period ? period : 1);
  }
}

// ---------------------------------------------------------------------
// MJPEG (:81 /stream)
// ---------------------------------------------------------------------
static esp_err_t streamHandler(httpd_req_t* req) {
  const uint32_t myGen = fcCameraStreamGen();
  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  char fpsHeader[8];
  snprintf(fpsHeader, sizeof(fpsHeader), "%u", (unsigned)fcCameraTargetFps());
  httpd_resp_set_hdr(req, "X-Framerate", fpsHeader);
  httpd_resp_set_hdr(req, "Connection", "close");

  fcCameraClientEnter();
  char part[80];
  uint32_t lastSequence = 0;

  while (true) {
    if (fcCameraStreamGen() != myGen) break;

    FcStreamFrame frame = {};
    if (!fcCameraAcquireLatest(lastSequence, &frame)) {
      // No existe cola: se espera el próximo latest-frame sin bloquear cámara.
      vTaskDelay(pdMS_TO_TICKS(3));
      continue;
    }

    lastSequence = frame.sequence;
    size_t hlen = snprintf(part, sizeof(part), STREAM_PART, (unsigned)frame.len);
    uint64_t sendStarted = esp_timer_get_time();
    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, part, hlen);
    if (res == ESP_OK)
      res = httpd_resp_send_chunk(req, (const char*)frame.data, frame.len);
    uint32_t sendMs = (uint32_t)((esp_timer_get_time() - sendStarted) / 1000ULL);
    size_t sent = frame.len;
    fcCameraReleaseFrame(&frame);

    if (res != ESP_OK) break;            // cliente cerrado: se sale y se limpia
    fcCameraNoteFrame(sent, sendMs);
    taskYIELD();
  }

  fcCameraClientExit();
  httpd_resp_send_chunk(req, NULL, 0);   // cierre del multipart
  return ESP_OK;
}

// ---------------------------------------------------------------------
// Arranque
// ---------------------------------------------------------------------
static const httpd_uri_t URI_INDEX  = { "/",           HTTP_GET,  indexHandler, nullptr, false, false, nullptr };
static const httpd_uri_t URI_STATE  = { "/api/state",  HTTP_GET,  stateHandler, nullptr, false, false, nullptr };
static const httpd_uri_t URI_MODE   = { "/api/mode",   HTTP_POST, modeHandler,  nullptr, false, false, nullptr };
static const httpd_uri_t URI_FLIP   = { "/api/flip",   HTTP_POST, flipHandler,  nullptr, false, false, nullptr };
static const httpd_uri_t URI_PHOTO  = { "/api/photo",  HTTP_GET,  photoHandler, nullptr, false, false, nullptr };
static const httpd_uri_t URI_WS     = { "/ws",         HTTP_GET,  wsHandler,    nullptr, true,  false, nullptr };
static const httpd_uri_t URI_STREAM = { "/stream",     HTTP_GET,  streamHandler,nullptr, false, false, nullptr };

bool fcServerBegin() {
  for (int i = 0; i < FC_WS_MAX_CLIENTS; i++) s_wsFds[i] = -1;
  if (!s_wsMutex) s_wsMutex = xSemaphoreCreateMutex();
  if (!s_wsMutex) return false;

  // --- servidor de interfaz ---
  httpd_config_t cw = HTTPD_DEFAULT_CONFIG();
  cw.server_port      = FC_HTTP_PORT;
  cw.ctrl_port        = 32768;
  cw.max_open_sockets = 5;            // + 3 internos = 8 sockets
  cw.max_uri_handlers = 8;
  cw.stack_size       = 8192;
  cw.lru_purge_enable = true;         // un cliente muerto no ocupa sitio
  cw.core_id          = 0;
  cw.close_fn         = onSocketClose;
  cw.recv_wait_timeout = 5;
  cw.send_wait_timeout = 1;

  if (httpd_start(&s_web, &cw) != ESP_OK) {
    Serial.println(F("[WEB] No se pudo arrancar el servidor del puerto 80."));
    return false;
  }
  httpd_register_uri_handler(s_web, &URI_INDEX);
  httpd_register_uri_handler(s_web, &URI_STATE);
  httpd_register_uri_handler(s_web, &URI_MODE);
  httpd_register_uri_handler(s_web, &URI_FLIP);
  httpd_register_uri_handler(s_web, &URI_PHOTO);
  httpd_register_uri_handler(s_web, &URI_WS);

  // --- servidor de vídeo, aparte y con su propio puerto de control ---
  httpd_config_t cs = HTTPD_DEFAULT_CONFIG();
  cs.server_port      = FC_STREAM_PORT;
  cs.ctrl_port        = 32769;        // DISTINTO del anterior o no arranca
  cs.max_open_sockets = 3;            // + 3 internos = 6 sockets
  cs.max_uri_handlers = 2;
  cs.stack_size       = 8192;
  cs.lru_purge_enable = true;
  cs.core_id          = 0;
  cs.recv_wait_timeout = 3;
  cs.send_wait_timeout = 2;           // cliente trabado sale; cámara sigue capturando

  if (httpd_start(&s_stream, &cs) != ESP_OK) {
    Serial.println(F("[WEB] No se pudo arrancar el servidor MJPEG del puerto 81."));
    httpd_stop(s_web); s_web = nullptr;
    return false;
  }
  httpd_register_uri_handler(s_stream, &URI_STREAM);

  // Núcleo 1: la telemetría no compite con el Wi-Fi ni con el MJPEG.
  if (xTaskCreatePinnedToCore(telemetryTask, "fc_ws", 4096, nullptr, 2, &s_wsTask, 1) != pdPASS) {
    Serial.println(F("[WEB] No se pudo crear la tarea de telemetría."));
    return false;
  }
  Serial.printf("[WEB] Interfaz en :%d  ·  MJPEG en :%d\n", FC_HTTP_PORT, FC_STREAM_PORT);
  return true;
}

void fcServerStop() {
  if (s_wsTask) { vTaskDelete(s_wsTask); s_wsTask = nullptr; }
  if (s_stream) { httpd_stop(s_stream); s_stream = nullptr; }
  if (s_web)    { httpd_stop(s_web);    s_web = nullptr; }
}
