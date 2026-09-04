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
#include <stdarg.h>

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
  "Content-Type: image/jpeg\r\nContent-Length: %u\r\n"
  "X-Ts: %lu\r\nX-Seq: %lu\r\n\r\n";

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
          "\"camReady\":%s,\"pid\":%u,\"fps\":%.1f,\"flip\":%d,\"modes\":[",
          (int)cs.mode, spec->label, cs.width, cs.height,
          cs.ready ? "true" : "false", cs.sensorPid, cs.fps, vf ? 1 : 0);
  for (int i = 0; i < FC_MODE_COUNT; i++) {
    const FcModeSpec* m = &FC_MODES[i];
    uint16_t cw = resolution[m->capture].width, ch = resolution[m->capture].height;
    uint16_t pw = resolution[m->preview].width, ph = resolution[m->preview].height;
    appendf(buf, cap, &n,
            "%s{\"id\":\"%s\",\"label\":\"%s\",\"prev\":\"%ux%u\","
            "\"captureLabel\":\"%ux%u\",\"horizon\":%s}",
            i ? "," : "", m->id, m->label, pw, ph, cw, ch,
            m->horizonHint ? "true" : "false");
  }
  appendf(buf, cap, &n, "]");
  {
    FcImuSample im; fcImuGet(&im);
    appendf(buf, cap, &n, ",\"imu\":{\"mount\":%d,\"invert\":%d,\"plane\":%u,\"epoch\":%lu}",
            (int)im.mountDeg, im.invert ? 1 : 0, (unsigned)im.plane,
            (unsigned long)im.epoch);
  }
  if (errMsg && errMsg[0])   appendf(buf, cap, &n, ",\"error\":\"%s\"", errMsg);
  else if (cs.lastError[0])  appendf(buf, cap, &n, ",\"error\":\"%s\"", cs.lastError);
  appendf(buf, cap, &n, "}");
  return n;
}

static esp_err_t sendState(httpd_req_t* req, const char* errMsg) {
  char* buf = (char*)malloc(1600);
  if (!buf) { httpd_resp_send_500(req); return ESP_FAIL; }
  size_t n = buildState(buf, 1600, errMsg);
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

// Montaje del IMU en caliente: no hace falta reprogramar para colocar el
// módulo del revés o girado.
static esp_err_t imuCfgHandler(httpd_req_t* req) {
  char v[12];
  if (queryValue(req, "mount", v, sizeof(v)))  fcImuSetMount(atoi(v));
  if (queryValue(req, "invert", v, sizeof(v))) fcImuSetInvert(v[0] == '1');
  if (queryValue(req, "plane", v, sizeof(v)))  fcImuSetPlane((uint8_t)atoi(v));
  return sendState(req, nullptr);
}

static esp_err_t photoHandler(httpd_req_t* req) {
  char err[80] = {0};
  camera_fb_t* fb = fcCameraCapture(err, sizeof(err));
  if (!fb) {
    noCache(req);
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_send(req, err[0] ? err : "no se pudo capturar", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  // Ángulo de horizonte del instante del disparo. Sin esto el navegador
  // tendría que adivinarlo, y entre que se pide la foto y llega el JPEG
  // pasan cientos de ms en los que la cámara puede haberse movido.
  FcImuSample im; fcImuGet(&im);
  char hdrH[24], hdrV[4], hdrE[16], disp[64];
  snprintf(hdrH, sizeof(hdrH), "%.3f", im.horizonFilt);
  snprintf(hdrV, sizeof(hdrV), "%d", im.horizonValid ? 1 : 0);
  snprintf(hdrE, sizeof(hdrE), "%lu", (unsigned long)im.epoch);
  snprintf(disp, sizeof(disp), "inline; filename=flexcam_%lu.jpg",
           (unsigned long)(millis() / 1000));
  noCache(req);
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "X-Horizon", hdrH);
  httpd_resp_set_hdr(req, "X-Hvalid", hdrV);
  httpd_resp_set_hdr(req, "X-Hepoch", hdrE);
  httpd_resp_set_hdr(req, "Access-Control-Expose-Headers",
                     "X-Horizon, X-Hvalid, X-Hepoch");
  httpd_resp_set_hdr(req, "Content-Disposition", disp);
  esp_err_t r = httpd_resp_send(req, (const char*)fb->buf, fb->len);
  fcCameraCaptureRelease(fb);          // devuelve el buffer y suelta el mutex
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
  char buf[512];
  for (;;) {
    if (fcServerWsClients() == 0) {        // nadie mirando: no gastar CPU
      vTaskDelay(pdMS_TO_TICKS(120));
      continue;
    }
    FcImuSample s;
    fcImuGet(&s);
    // Se envía SIEMPRE la última muestra, nunca una cola: si el navegador
    // va lento se pierden paquetes intermedios, que es justo lo que se quiere.
    int n = snprintf(buf, sizeof(buf),
        "{\"t\":\"i\",\"sq\":%lu,\"ts\":%lu,\"r\":%.2f,\"p\":%.2f,\"y\":%.2f,"
        "\"hr\":%.3f,\"hc\":%.3f,\"hf\":%.3f,\"cf\":%.3f,\"hv\":%d,"
        "\"qi\":%.4f,\"qj\":%.4f,\"qk\":%.4f,\"qr\":%.4f,"
        "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,"
        "\"hz\":%.0f,\"a\":%u,\"ok\":%d,\"s\":\"%s\",\"rs\":%lu,\"ep\":%lu,"
        "\"mo\":%d,\"iv\":%d,\"pl\":%u}",
        (unsigned long)s.seq, (unsigned long)s.tsMs,
        s.roll, s.pitch, s.yaw,
        s.horizonRaw, s.horizonCont, s.horizonFilt, s.confidence,
        s.horizonValid ? 1 : 0,
        s.qi, s.qj, s.qk, s.qr, s.gx, s.gy, s.gz,
        s.hz, (unsigned)s.accuracy, (s.state == FC_IMU_OK) ? 1 : 0,
        fcImuStateText(s.state), (unsigned long)s.resets,
        (unsigned long)s.epoch, (int)s.mountDeg, s.invert ? 1 : 0,
        (unsigned)s.plane);
    if (n > 0) wsBroadcast(buf, (size_t)n);

    uint32_t now = millis();
    if (now - lastStats >= FC_WS_STATS_MS) {
      lastStats = now;
      FcCamStatus cs;
      fcCameraGetStatus(&cs);
      n = snprintf(buf, sizeof(buf),
          "{\"t\":\"s\",\"fps\":%.1f,\"w\":%u,\"h\":%u,\"mode\":%d,\"cam\":%d,"
          "\"cli\":%lu,\"heap\":%lu,\"ps\":%lu,\"drop\":%lu,\"up\":%lu,"
          "\"tc\":%.1f,\"ts\":%lu,\"sent\":%lu}",
          cs.fps, cs.width, cs.height, (int)cs.mode, cs.ready ? 1 : 0,
          (unsigned long)cs.clients,
          (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
          (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
          (unsigned long)cs.dropped, (unsigned long)(now / 1000),
          temperatureRead(), (unsigned long)now,
          (unsigned long)cs.framesSent);
      if (n > 0) wsBroadcast(buf, (size_t)n);
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
  httpd_resp_set_hdr(req, "X-Framerate", "60");
  httpd_resp_set_hdr(req, "Connection", "close");

  fcCameraClientEnter();
  char part[128];
  static uint32_t frameSeq = 0;

  while (true) {
    // El cambio de modo o el disparo suben la generación: salir en limpio.
    if (fcCameraStreamGen() != myGen) break;

    if (!fcCameraLock(1500)) {           // otro está reconfigurando
      if (fcCameraStreamGen() != myGen) break;
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb || fb->len == 0) {
      if (fb) esp_camera_fb_return(fb);
      fcCameraUnlock();
      fcCameraNoteDrop();
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (fb->format != PIXFORMAT_JPEG) {  // no debería pasar: siempre JPEG
      esp_camera_fb_return(fb);
      fcCameraUnlock();
      fcCameraNoteDrop();
      continue;
    }

    // Cada parte lleva su marca de tiempo y su número de secuencia: el
    // navegador los usa para medir edad de frame y latencia de verdad, en
    // vez de estimarlas a ojo.
    uint32_t fts = (uint32_t)(fb->timestamp.tv_sec * 1000ULL +
                              fb->timestamp.tv_usec / 1000ULL);
    if (fts == 0) fts = millis();
    size_t hlen = snprintf(part, sizeof(part), STREAM_PART, (unsigned)fb->len,
                           (unsigned long)fts, (unsigned long)(++frameSeq));
    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, part, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);

    size_t sent = fb->len;
    esp_camera_fb_return(fb);
    fcCameraUnlock();

    if (res != ESP_OK) break;            // cliente cerrado: se sale y se limpia
    fcCameraNoteFrame(sent);
    vTaskDelay(1);                       // deja respirar al watchdog del núcleo 0
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
static const httpd_uri_t URI_IMUCFG = { "/api/imucfg", HTTP_POST, imuCfgHandler,nullptr, false, false, nullptr };
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
  cw.max_uri_handlers = 10;
  cw.stack_size       = 8192;
  cw.lru_purge_enable = true;         // un cliente muerto no ocupa sitio
  cw.core_id          = 0;
  cw.close_fn         = onSocketClose;
  cw.recv_wait_timeout = 5;
  cw.send_wait_timeout = 5;

  if (httpd_start(&s_web, &cw) != ESP_OK) {
    Serial.println(F("[WEB] No se pudo arrancar el servidor del puerto 80."));
    return false;
  }
  httpd_register_uri_handler(s_web, &URI_INDEX);
  httpd_register_uri_handler(s_web, &URI_STATE);
  httpd_register_uri_handler(s_web, &URI_MODE);
  httpd_register_uri_handler(s_web, &URI_FLIP);
  httpd_register_uri_handler(s_web, &URI_PHOTO);
  httpd_register_uri_handler(s_web, &URI_IMUCFG);
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
  cs.send_wait_timeout = 3;           // acota cuánto puede bloquear un móvil lento

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
