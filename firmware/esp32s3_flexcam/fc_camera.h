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
  uint8_t     fbCount;       // buffers de la cámara en PSRAM
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
  float    fps;              // FPS reales medidos en el stream
  uint32_t streamGen;        // generación: cambia y los streams vivos se cierran
  uint32_t clients;          // clientes MJPEG activos
  uint32_t framesSent;
  uint32_t dropped;          // frames perdidos (fb nulo)
  char     lastError[64];
};

bool  fcCameraBegin();                     // devuelve false si no hay cámara
bool  fcCameraSetMode(FcMode m, char* errOut, size_t errLen);
FcMode fcCameraGetMode();
void  fcCameraGetStatus(FcCamStatus* out);
const FcModeSpec* fcCameraModeSpec(FcMode m);

// Bloqueo del bus de cámara. El stream lo toma por fotograma.
bool  fcCameraLock(uint32_t ms);
void  fcCameraUnlock();

// Generación de stream: cualquier handler MJPEG vivo debe salir si cambia.
uint32_t fcCameraStreamGen();
void     fcCameraBumpStreamGen();

// Contadores usados por el handler de stream.
void  fcCameraNoteFrame(size_t bytes);
void  fcCameraNoteDrop();
void  fcCameraClientEnter();
void  fcCameraClientExit();

// Disparo. Deja el fb reservado: hay que devolverlo con esp_camera_fb_return().
// Si el modo pide una resolución de captura distinta, reconfigura y restaura.
camera_fb_t* fcCameraCapture(char* errOut, size_t errLen);
void         fcCameraCaptureRelease(camera_fb_t* fb);

// Ajustes finos expuestos por la web (no rompen nada si el sensor los ignora).
bool fcCameraSetFlip(bool vflip, bool hmirror);
bool fcCameraGetFlip(bool* vflip, bool* hmirror);
