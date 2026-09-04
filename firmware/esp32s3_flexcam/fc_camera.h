// =====================================================================
//  fc_camera.h — gestión de la cámara OV5640 con cambios de modo seguros.
// =====================================================================
#pragma once

#include <Arduino.h>
#include "esp_camera.h"
#include "fc_config.h"

struct FcModeSpec {
  const char* id;            // identificador usado por la web
  const char* label;         // nombre visible
  framesize_t preview;       // resolución del stream
  framesize_t capture;       // resolución del disparo
  int         previewQuality;// 0..63 (menor = mejor calidad, más bytes)
  int         captureQuality;
  uint8_t     targetFps;     // límite del productor; evita cola y calor inútil
  bool        horizonHint;   // la web enciende Horizon Lock al entrar
};

extern const FcModeSpec FC_MODES[FC_MODE_COUNT];

// Estado observable (sólo lectura desde fuera).
struct FcCamStatus {
  bool     ready;            // la cámara arrancó
  uint16_t sensorPid;        // 0x5640 si es un OV5640
  FcMode   mode;
  uint16_t width;
  uint16_t height;
  float    fps;              // FPS capturados/publicados por el productor
  float    sendFps;          // FPS entregados (suma si hay varios clientes)
  float    captureMs;        // media móvil de captura + copia a PSRAM
  float    sendMs;           // media móvil del envío de red
  float    temperatureC;     // sensor interno aproximado del ESP32-S3
  uint8_t  targetFps;        // objetivo efectivo, incluye limitación térmica
  uint8_t  thermalLevel;     // 0 normal, 1 limitado, 2 crítico
  uint32_t streamGen;        // generación: cambia y los streams vivos se cierran
  uint32_t clients;          // clientes MJPEG activos
  uint32_t framesCaptured;
  uint32_t framesSent;
  uint32_t dropped;          // fallos al obtener un framebuffer
  uint32_t poolDropped;      // descartados porque los clientes iban atrasados
  uint32_t lastFrameBytes;
  uint32_t lastFrameAgeMs;
  char     lastError[64];
};

// Referencia de sólo lectura a un JPEG publicado en el pool de PSRAM. El
// handler debe liberarla en cuanto termine de enviarla. La cámara continúa
// capturando en otros slots mientras la red usa éste.
struct FcStreamFrame {
  const uint8_t* data;
  size_t         len;
  uint16_t       width;
  uint16_t       height;
  uint32_t       sequence;
  uint32_t       capturedMs;
  uint8_t        slot;
};

// Fotografía copiada fuera del framebuffer del driver. Así el envío HTTP de
// una foto grande nunca mantiene bloqueada la cámara.
struct FcPhoto {
  uint8_t* data;
  size_t   len;
  uint16_t width;
  uint16_t height;
};

bool  fcCameraBegin();                     // devuelve false si no hay cámara
bool  fcCameraSetMode(FcMode m, char* errOut, size_t errLen);
FcMode fcCameraGetMode();
void  fcCameraGetStatus(FcCamStatus* out);
const FcModeSpec* fcCameraModeSpec(FcMode m);

// Bloqueo del bus de cámara. Sólo lo usan captura/reconfiguración/productor;
// jamás se mantiene durante una escritura de red.
bool  fcCameraLock(uint32_t ms);
void  fcCameraUnlock();

// Generación de stream: cualquier handler MJPEG vivo debe salir si cambia.
uint32_t fcCameraStreamGen();
void     fcCameraBumpStreamGen();

// Pool latest-frame y contadores usados por el handler de stream.
bool  fcCameraAcquireLatest(uint32_t afterSequence, FcStreamFrame* out);
void  fcCameraReleaseFrame(const FcStreamFrame* frame);
void  fcCameraNoteFrame(size_t bytes, uint32_t sendMs);
void  fcCameraNoteDrop();
void  fcCameraClientEnter();
void  fcCameraClientExit();
uint8_t fcCameraTargetFps();
void  fcCameraModePreviewSize(FcMode mode, uint16_t* width, uint16_t* height);
void  fcCameraModeCaptureSize(FcMode mode, uint16_t* width, uint16_t* height);

// Disparo seguro: copia el JPEG a memoria propia, devuelve el framebuffer y
// restaura el preview antes de retornar. Liberar con fcCameraPhotoRelease().
bool fcCameraCapture(FcPhoto* out, char* errOut, size_t errLen);
void fcCameraPhotoRelease(FcPhoto* photo);

// Ajustes finos expuestos por la web (no rompen nada si el sensor los ignora).
bool fcCameraSetFlip(bool vflip, bool hmirror);
bool fcCameraGetFlip(bool* vflip, bool* hmirror);
