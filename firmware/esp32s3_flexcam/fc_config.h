// =====================================================================
//  FlexCam S26 — ESP32-S3-N16R8 + OV5640 (5 MP) + BNO085
//  fc_config.h — TODO lo que se puede ajustar sin tocar la lógica.
// =====================================================================
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------
// 1. Red Wi-Fi propia (modo AP, funciona sin Internet)
// ---------------------------------------------------------------------
#define FC_AP_SSID        "FlexCam-S26"
#define FC_AP_PASSWORD    "FlexCam2026"      // WPA2, mínimo 8 caracteres
#define FC_AP_CHANNEL     6                  // 1, 6 u 11 suelen ser los más limpios
#define FC_AP_MAX_CLIENTS 4
#define FC_AP_IP_1        192
#define FC_AP_IP_2        168
#define FC_AP_IP_3        4
#define FC_AP_IP_4        1                  // -> http://192.168.4.1

#define FC_HTTP_PORT      80                 // interfaz web + control + WebSocket
#define FC_STREAM_PORT    81                 // MJPEG en un servidor aparte

// ---------------------------------------------------------------------
// 2. Perfil de pines de la cámara OV5640
// ---------------------------------------------------------------------
// IMPORTANTE: no hay un único "ESP32-S3-N16R8 con OV5640". El pinout depende
// de la placa portadora. El perfil por defecto es el estándar de Espressif
// para ESP32-S3 + cámara (ESP32-S3-EYE) que es el MISMO que usa la placa
// Freenove ESP32-S3-WROOM CAM: usa sólo GPIO4..GPIO18, que es exactamente
// el rango que has declarado como ocupado por la cámara, y deja la microSD
// en GPIO38..40. Si tu placa fuese otra, cambia el perfil aquí abajo.
//
// Comprueba en el monitor serie al arrancar: debe imprimir "PID sensor=0x5640".
// Si imprime 0x0000 o "Camera detect failed", el perfil de pines no es el tuyo.
#define FC_CAM_PROFILE_S3_STD    1   // ESP32-S3-EYE / Freenove ESP32-S3-WROOM CAM
#define FC_CAM_PROFILE_XIAO_S3   2   // Seeed XIAO ESP32S3 Sense
#define FC_CAM_PROFILE_CUSTOM    3   // rellena tú los pines más abajo

#define FC_CAM_PROFILE  FC_CAM_PROFILE_S3_STD

#if FC_CAM_PROFILE == FC_CAM_PROFILE_S3_STD
  #define FC_PIN_PWDN   -1
  #define FC_PIN_RESET  -1
  #define FC_PIN_XCLK   15
  #define FC_PIN_SIOD    4      // SCCB SDA (bus I2C propio de la cámara)
  #define FC_PIN_SIOC    5      // SCCB SCL
  #define FC_PIN_D7     16
  #define FC_PIN_D6     17
  #define FC_PIN_D5     18
  #define FC_PIN_D4     12
  #define FC_PIN_D3     10
  #define FC_PIN_D2      8
  #define FC_PIN_D1      9
  #define FC_PIN_D0     11
  #define FC_PIN_VSYNC   6
  #define FC_PIN_HREF    7
  #define FC_PIN_PCLK   13
#elif FC_CAM_PROFILE == FC_CAM_PROFILE_XIAO_S3
  #define FC_PIN_PWDN   -1
  #define FC_PIN_RESET  -1
  #define FC_PIN_XCLK   10
  #define FC_PIN_SIOD   40
  #define FC_PIN_SIOC   39
  #define FC_PIN_D7     48
  #define FC_PIN_D6     11
  #define FC_PIN_D5     12
  #define FC_PIN_D4     14
  #define FC_PIN_D3     16
  #define FC_PIN_D2     18
  #define FC_PIN_D1     17
  #define FC_PIN_D0     15
  #define FC_PIN_VSYNC  38
  #define FC_PIN_HREF   47
  #define FC_PIN_PCLK   13
#else
  #define FC_PIN_PWDN   -1
  #define FC_PIN_RESET  -1
  #define FC_PIN_XCLK   -1
  #define FC_PIN_SIOD   -1
  #define FC_PIN_SIOC   -1
  #define FC_PIN_D7     -1
  #define FC_PIN_D6     -1
  #define FC_PIN_D5     -1
  #define FC_PIN_D4     -1
  #define FC_PIN_D3     -1
  #define FC_PIN_D2     -1
  #define FC_PIN_D1     -1
  #define FC_PIN_D0     -1
  #define FC_PIN_VSYNC  -1
  #define FC_PIN_HREF   -1
  #define FC_PIN_PCLK   -1
#endif

// Reloj del sensor. 20 MHz es el valor de referencia del driver para OV5640.
// 24 MHz da algo más de FPS pero en varias placas produce líneas/artefactos.
#define FC_XCLK_HZ  20000000

// ---------------------------------------------------------------------
// 3. BNO085 por I2C
// ---------------------------------------------------------------------
// AVISO DE CONFLICTO REAL (verificado en el core esp32 3.3.11):
//   el sdkconfig del core trae CONFIG_SCCB_HARDWARE_I2C_PORT1=y, es decir,
//   el driver de la cámara se queda con el puerto I2C 1 (= objeto Wire1).
//   Por eso el BNO085 DEBE ir en el puerto I2C 0 (= objeto Wire).
//   No cambies esto a Wire1 o la cámara y el IMU se pelearán por el bus.
#define FC_IMU_WIRE       Wire        // puerto I2C 0
#define FC_IMU_SDA        21
#define FC_IMU_SCL        47
#define FC_IMU_INT        1           // -1 si no lo conectas
#define FC_IMU_RST        -1          // sin conectar
#define FC_IMU_I2C_HZ     400000
#define FC_IMU_ADDR_A     0x4B        // BNO085 con AD0/PS0 al aire (por defecto)
#define FC_IMU_ADDR_B     0x4A        // algunos módulos vienen a 0x4A

// Vector de orientación:
//   1 = Game Rotation Vector  -> sin magnetómetro. Yaw relativo (deriva lenta),
//                                roll/pitch muy estables. Recomendado para vídeo.
//   0 = Rotation Vector       -> usa magnetómetro. Yaw absoluto (norte), pero
//                                se ensucia cerca de metales, motores e imanes.
#define FC_IMU_USE_GAME_RV   1

#define FC_IMU_REPORT_MS     10       // 10 ms = 100 Hz solicitados al sensor
#define FC_IMU_TASK_HZ       200      // frecuencia de sondeo de la tarea
#define FC_IMU_DEADZONE_DEG  0.30f    // zona muerta 0.2..0.5 grados
#define FC_IMU_INVERT_ROLL   0        // 1 si el módulo está montado del revés
#define FC_IMU_INVERT_PITCH  0
#define FC_IMU_INVERT_YAW    0
// Suavizado exponencial: 1.0 = sin filtro (máxima respuesta, algo de ruido).
// 0.35..0.55 quita ruido sin que se note retraso a 100 Hz.
#define FC_IMU_SMOOTH        0.45f
#define FC_IMU_TIMEOUT_MS    1500     // sin datos -> "IMU no disponible"
#define FC_IMU_RETRY_MS      2000     // reintento de reconexión

// ---------------------------------------------------------------------
// 4. Telemetría por WebSocket
// ---------------------------------------------------------------------
#define FC_WS_MAX_CLIENTS    4
#define FC_WS_IMU_HZ         50       // paquete de orientación (50 Hz)
#define FC_WS_STATS_MS       500      // paquete de estado (FPS, heap, ...)

// ---------------------------------------------------------------------
// 5. Modos de cámara
// ---------------------------------------------------------------------
enum FcMode {
  FC_MODE_PHOTO5MP = 0,   // preview 800x600, disparo a 2592x1944
  FC_MODE_HIQ      = 1,   // preview 1600x1200
  FC_MODE_FLUID    = 2,   // preview 800x600
  FC_MODE_HLOCK    = 3,   // preview 800x600 + Horizon Lock
  FC_MODE_HLOCK_UL = 4,   // preview 640x480 + Horizon Lock, máxima prioridad a FPS
  FC_MODE_COUNT    = 5
};
#define FC_MODE_DEFAULT  FC_MODE_FLUID

// Fotogramas que se descartan tras cambiar de resolución para que el OV5640
// asiente exposición/balance y no salga el típico primer frame verde.
#define FC_SETTLE_FRAMES   3
// Tiempo máximo esperando el mutex de cámara antes de responder "ocupada".
#define FC_CAM_LOCK_MS     4000
