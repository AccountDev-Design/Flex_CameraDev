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
// 60 unidades de 0,25 dBm = 15 dBm. Es potencia suficiente a corta/media
// distancia y disipa bastante menos que los 20 dBm máximos por defecto.
#define FC_WIFI_TX_POWER_QDBM 60
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
//   1 = Game Rotation Vector  -> sin magnetómetro. Muy estable, sin tirones
//                                por metales o motores. Recomendado.
//   0 = Rotation Vector       -> usa magnetómetro. Yaw absoluto (norte), pero
//                                se ensucia cerca de metales e imanes.
// El Horizon Lock sólo necesita gravedad, así que el Game RV basta y sobra.
#define FC_IMU_USE_GAME_RV   1

#define FC_IMU_REPORT_MS     10       // 10 ms = 100 Hz solicitados al sensor
#define FC_IMU_TASK_HZ       200      // frecuencia de sondeo de la tarea
#define FC_IMU_TIMEOUT_MS    1500     // sin datos -> "IMU no disponible"
#define FC_IMU_RETRY_MS      2000     // reintento de reconexión

// --- Montaje del IMU (valores iniciales; se cambian desde la web) --------
// Plano de la imagen en ejes del sensor. 0 = XY (eje óptico ~ Z, que es el
// caso normal con el módulo pegado en paralelo a la placa de la cámara),
// 1 = XZ (eje óptico ~ Y), 2 = YZ (eje óptico ~ X).
#define FC_IMU_PLANE         0
// Giro del módulo dentro de ese plano: 0, 90, 180 o 270 grados.
#define FC_IMU_MOUNT_DEG     0
// Sentido de la corrección. Ponlo a 1 si la imagen gira hacia el mismo lado
// que la cámara en vez de al contrario.
#define FC_IMU_INVERT_ROLL   0

// --- Horizonte por gravedad ---------------------------------------------
// Confianza = módulo de la gravedad proyectada en el plano de la imagen.
// Vale 1 con la cámara apuntando al horizonte y 0 apuntando al cenit o al
// nadir, donde el horizonte es matemáticamente indeterminado. Con histéresis
// para no parpadear justo en el umbral. 0.17 ~ 10 grados de la vertical.
#define FC_HORIZON_CONF_HI   0.17f    // hay que superarlo para volver a ser válido
#define FC_HORIZON_CONF_LO   0.12f    // por debajo se declara "sin referencia"

// Filtro de un polo dependiente de dt. tau pequeño = más rápido.
#define FC_HORIZON_TAU_SLOW  0.110f   // quieto: suave, sin temblor
#define FC_HORIZON_TAU_FAST  0.022f   // giro real: respuesta inmediata
#define FC_HORIZON_TAU_REACQ 0.260f   // al recuperar referencia: deslizar, no saltar
#define FC_HORIZON_FAST_DEG  6.0f     // error a partir del cual se usa tau rápido
#define FC_HORIZON_DEADZONE  0.25f    // zona muerta en grados (0,2 a 0,5)
#define FC_HORIZON_REACQ_MS  900      // duración del deslizamiento de recuperación

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
  FC_MODE_PHOTO5MP = 0,   // preview 800x600, disparo a la máxima resolución real
  FC_MODE_LIVE5MP  = 1,   // máxima resolución en vivo, deliberadamente pocos FPS
  FC_MODE_HIQ      = 2,   // preview 1600x1200
  FC_MODE_FLUID    = 3,   // preview 800x600
  FC_MODE_HLOCK    = 4,   // preview 800x600 + Horizon Lock
  FC_MODE_HLOCK_UL = 5,   // preview 640x480 + Horizon Lock, máxima prioridad a FPS
  FC_MODE_COUNT    = 6
};
#define FC_MODE_DEFAULT  FC_MODE_FLUID

// El productor de cámara conserva sólo los fotogramas más nuevos en PSRAM.
// Si todos están siendo enviados a clientes lentos, descarta el recién
// capturado: nunca acumula latencia ni deja el sensor preso del Wi-Fi.
#define FC_STREAM_POOL_SLOTS       3
#define FC_STREAM_SLOT_HEADROOM    (32 * 1024)
#define FC_CAMERA_TASK_STACK       6144
#define FC_CAMERA_IDLE_WAIT_MS     120

// Protección térmica basada en el sensor interno del ESP32-S3. Es una medida
// aproximada del chip, no de la superficie del regulador ni de la cámara.
#define FC_THERMAL_SAMPLE_MS       2000
#define FC_THERMAL_THROTTLE_C      75.0f
#define FC_THERMAL_CRITICAL_C      85.0f
#define FC_THERMAL_THROTTLE_FPS    15
#define FC_THERMAL_CRITICAL_FPS    5

// Fotogramas que se descartan tras cambiar de resolución para que el OV5640
// asiente exposición/balance y no salga el típico primer frame verde.
#define FC_SETTLE_FRAMES   3
// Tiempo máximo esperando el mutex de cámara antes de responder "ocupada".
#define FC_CAM_LOCK_MS     4000
