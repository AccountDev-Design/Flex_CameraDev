# FlexCam S26 — cámara web para ESP32-S3-N16R8 + OV5640 + BNO085

Firmware para Arduino IDE. El ESP32 crea su propia red Wi-Fi, sirve una
interfaz táctil y transmite MJPEG. **La nivelación de horizonte y la rotación
manual se hacen en el navegador**: el ESP32 nunca decodifica, rota ni
recomprime un fotograma.

> **Este firmware es independiente de la app Android de este repositorio.**
> Vive entero en `firmware/esp32s3_flexcam/` y no toca ni un archivo del
> proyecto Gradle. Ver la sección "Relación con el resto del repositorio".

---

## 1. Qué se ha verificado (y qué no)

La versión original se compiló con `arduino-cli 1.2.0` + core **esp32 3.3.11**.
La gestión de cámara actual usa sólo la API pública de `esp_camera.h` y
`sensor_t`: no depende de `esp_camera_reconfigure()` ni de los miembros de
autofocus que faltan en algunas versiones empaquetadas con Arduino-ESP32.

```
Sketch uses 1034123 bytes (32%) of program storage space. Maximum is 3145728 bytes.
Global variables use 60916 bytes (18%) of dynamic memory, leaving 266764 bytes
for local variables. Maximum is 327680 bytes.
```

**Cero errores y cero warnings** con `--warnings all`.

La interfaz web se ha probado en Chromium con el backend simulado: 26 pruebas
automáticas, todas verdes (rotación 0/90/180/270/360, que 360° sea idéntico a
0°, persistencia en `localStorage`, sentido y límite de la compensación,
suma de rotación manual + horizonte, que la rotación manual no toque el IMU,
cambios de modo, IMU caído, y ausencia de desbordamiento horizontal en móvil
vertical, móvil horizontal y tablet).

**No se ha probado sobre la placa física.** No tengo tu hardware delante. Todo
lo que depende del silicio real (FPS, ruido del sensor, alcance del Wi-Fi)
está marcado abajo como estimación, y el propio firmware mide y muestra los
valores reales en la web.

---

## 2. Conflictos reales encontrados durante la revisión

Estos cuatro puntos no son opiniones: salen de leer el código del core que se
enlaza. Los cuatro están ya resueltos en el firmware.

### 2.1 El BNO085 tiene que ir en `Wire`, no en `Wire1`

El `sdkconfig` del core esp32 3.3.11 trae:

```
# CONFIG_SCCB_HARDWARE_I2C_PORT0 is not set
CONFIG_SCCB_HARDWARE_I2C_PORT1=y
```

O sea: **el driver de la cámara se queda con el puerto I2C 1**, que en Arduino
es el objeto `Wire1`. Por eso el BNO085 usa `Wire` (puerto 0) en GPIO21/47.
Si lo pasas a `Wire1` la cámara y el IMU se pelean por el mismo periférico.

### 2.2 El OV5640 no da 2592×1944 con este driver

En la tabla `camera_sensor[]` de esp32-camera (comprobado desempaquetando
`libespressif__esp32-camera.a` del propio core, no sólo en el código fuente
de GitHub) el OV5640 declara:

```
{CAMERA_OV5640, "OV5640", 0x3C, 0x5640, FRAMESIZE_QSXGA, true}
                                        ^^^^^^^^^^^^^^^ = 23 = 2560x1920
```

y `esp_camera_init()` recorta **en silencio** cualquier tamaño mayor:

```c
if (frame_size > camera_sensor[camera_model].max_size) {
    ESP_LOGW(TAG, "The frame size exceeds the maximum for this sensor, ...");
    frame_size = camera_sensor[camera_model].max_size;
}
```

Pedir `FRAMESIZE_5MP` devolvería igualmente 2560×1920. Por eso el modo foto
pide **QSXGA 2560×1920 (4,92 MP)** directamente: así lo que anuncia la web es
lo que sale del sensor. El chip OV5640 tiene 2592×1944 de matriz; el recorte
lo pone el driver, no el sensor.

### 2.3 El pin INT del BNO085 no se le pasa a la librería

`SparkFun_BNO08x` tiene esto en su HAL de I2C:

```c
static bool hal_wait_for_int(void) {
  for (int i = 0; i < 500; i++) { if (!digitalRead(_int_pin)) return true; delay(1); }
  hal_hardwareReset();     // <- y además resetea el sensor
  return false;
}
```

Con el IMU desconectado, cada lectura se convertiría en medio segundo perdido
más un reset. Todas esas llamadas están protegidas por `if (_int_pin != -1)`,
así que a `begin()` se le pasa `-1` y **GPIO1 se lee a mano**, sin bloquear,
sólo como aviso de "hay dato listo". Si a los 1,5 s el INT nunca ha bajado
pero sí llegan datos, el firmware deduce que el cable no está y pasa a sondeo
por tiempo, sin avisos ni fallos.

### 2.4 No se incluye `partitions.csv` a propósito

`platform.txt` del core resuelve las particiones en este orden, y **gana el
último**:

```
prebuild.1: {runtime.platform.path}/tools/partitions/{build.partitions}.csv   <- menú del IDE
prebuild.2: {build.variant.path}/partitions.csv
prebuild.3: {build.source.path}/partitions.csv                                <- carpeta del sketch
```

Un `partitions.csv` dentro de la carpeta del sketch **anularía en silencio** lo
que elijas en el menú *Partition Scheme*. Como eso confunde más de lo que
ayuda, no se incluye: eliges el esquema en el menú y punto.

---

## 3. Conexionado

### Cámara OV5640 — perfil por defecto

No existe "el" pinout del ESP32-S3-N16R8 con OV5640: depende de la placa
portadora. El perfil por defecto es el estándar de Espressif para
ESP32-S3 + cámara (`CAMERA_MODEL_ESP32S3_EYE`), que es **el mismo** que usa la
Freenove ESP32-S3-WROOM CAM. Encaja exactamente con lo que describiste: usa
sólo GPIO4–GPIO18 y deja libres GPIO38–40 para la microSD.

| Señal | GPIO | | Señal | GPIO |
|---|---|---|---|---|
| SIOD (SCCB SDA) | 4 | | D0 (Y2) | 11 |
| SIOC (SCCB SCL) | 5 | | D1 (Y3) | 9 |
| VSYNC | 6 | | D2 (Y4) | 8 |
| HREF | 7 | | D3 (Y5) | 10 |
| PCLK | 13 | | D4 (Y6) | 12 |
| XCLK | 15 | | D5 (Y7) | 18 |
| PWDN | — | | D6 (Y8) | 17 |
| RESET | — | | D7 (Y9) | 16 |

**Compruébalo al arrancar.** El monitor serie imprime:

```
[CAM] PID sensor=0x5640 (OV5640 correcto)
```

Si sale `Camera detect failed` o un PID distinto, tu placa usa otro pinout:
cambia `FC_CAM_PROFILE` en `fc_config.h` (hay un perfil para XIAO ESP32S3 Sense
y uno vacío para rellenar a mano). **No he cambiado ningún pin de cámara
respecto a la referencia oficial.**

### BNO085 — tal y como lo pediste

| BNO085 | ESP32-S3 | Nota |
|---|---|---|
| VCC | 3V3 | |
| GND | GND | |
| SDA | GPIO21 | `Wire`, puerto I2C 0 |
| SCL | GPIO47 | |
| INT | GPIO1 | opcional; se detecta solo si no está |
| RST, AD0, PS0, PS1 | sin conectar | |

Todo a 3,3 V. **Sin conflictos**: GPIO21, GPIO47 y GPIO1 quedan fuera de
GPIO4–18 (cámara), GPIO35–37 (PSRAM), GPIO38–40 (microSD) y GPIO0/3/45/46.
El firmware **no toca en ningún momento** los pines de la microSD.

---

## 4. Cómo compilar y flashear (Arduino IDE)

1. **Gestor de placas**: *Archivo → Preferencias → URLs adicionales*, añade
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`.
   Luego *Herramientas → Placa → Gestor de tarjetas → esp32* (3.3.x).
2. **Librería**: *Herramientas → Administrar bibliotecas* → busca
   **`SparkFun BNO08x Cortex Based IMU`** e instálala (probado con la 1.0.6).
   Trae dentro el driver SH2 de CEVA, no hace falta nada más.
   `esp_camera.h` ya viene con el core.
3. Abre `firmware/esp32s3_flexcam/esp32s3_flexcam.ino`.
4. **Menú Herramientas** — esto es lo que se ha usado para compilar:

   | Opción | Valor |
   |---|---|
   | Board | ESP32S3 Dev Module |
   | **PSRAM** | **OPI PSRAM** ← obligatorio en la N16R8 |
   | Flash Size | 16MB (128Mb) |
   | Flash Mode | QIO 80MHz |
   | **Partition Scheme** | **16M Flash (3MB APP/9.9MB FATFS)** |
   | CPU Frequency | 240MHz (WiFi) |
   | Arduino Runs On | Core 1 |
   | Events Run On | Core 0 |
   | Core Debug Level | None |
   | USB CDC On Boot | Enabled |

   Sin `OPI PSRAM` el OV5640 no pasa de resoluciones bajas; el firmware lo
   avisa por serie en vez de fallar sin explicación.
5. Sube el sketch y abre el **Monitor Serie a 115200**.

### Cómo entrar a la web

1. En el móvil o la tablet, Wi-Fi → conéctate a **`FlexCam-S26`**, clave
   **`FlexCam2026`**.
2. Android avisará de "red sin Internet": dile **mantener la conexión**.
3. Abre el navegador en **`http://192.168.4.1/`**.

El vídeo va por el puerto 81 (`http://192.168.4.1:81/stream`), la web y el
WebSocket por el 80. Están separados para que un cliente lento tragando vídeo
no congele los botones.

---

## 5. Modos y rendimiento

| Modo | Preview | Captura | FPS estimado | Buffers reservados |
|---|---|---|---|---|
| Foto 5 MP | 800×600 | **2560×1920** | preview 20–30 | 2 buffers QSXGA en PSRAM |
| Alta calidad | 1600×1200 | 1600×1200 | 8–15 | 2 buffers QSXGA en PSRAM |
| Vista fluida | 800×600 | 800×600 | 20–30 | 2 buffers QSXGA en PSRAM |
| Horizon Lock | 800×600 | 800×600 | 20–30 | 2 buffers QSXGA en PSRAM |
| Horizon Lock Ultra | 640×480 | 640×480 | 25–40 | 2 buffers QSXGA en PSRAM |

**De dónde salen esos números y qué valen.** Los tamaños de buffer **sí son
exactos**: el driver reserva `ancho × alto / 5` bytes por buffer en modo JPEG
(`cam_hal.c`, `cam_obj->recv_size = cam_obj->width * cam_obj->height / 5`),
más el medio buffer de DMA. Los **FPS son estimaciones, no medidas**: dependen
del PLL interno del OV5640, del tamaño real de cada JPEG (que cambia con la
escena), y del Wi-Fi. No tengo tu placa, así que **no voy a dar cifras
inventadas como si las hubiera medido**. El firmware cuenta los fotogramas
realmente enviados y la web muestra el FPS real arriba y en el panel de IMU:
ese número es el bueno.

Los dos cuellos de botella reales, para que sepas dónde mirar:

* **El sensor**: a 20 MHz de XCLK el OV5640 baja de fotogramas conforme sube
  la resolución. A 2560×1920 no hay vídeo fluido y no se promete: por eso el
  modo foto hace el preview a 800×600 y sólo sube a máxima resolución en el
  instante del disparo.
* **El Wi-Fi**: un SoftAP del ESP32-S3 en HT20 mueve del orden de 1–2 MB/s
  útiles con un cliente. A 800×600 y calidad 12 un JPEG ronda 25–45 KB, así
  que ahí el techo de red anda por los 25–40 fps. A 1600×1200 los JPEG rondan
  100–160 KB y el techo baja a 8–15 fps. Con dos móviles mirando a la vez, la
  mitad para cada uno.

Si quieres más FPS: baja la resolución, sube el número de `jpeg_quality` (más
alto = más comprimido = menos bytes) y quédate con un solo cliente.

---

## 6. Horizon Lock: cómo funciona y por qué el límite no siempre es ±15°

Todo ocurre en el navegador, con `transform: rotate() scale()` sobre el
elemento del stream. Nada de esto toca al ESP32.

* Al activarlo se guarda el roll actual como referencia (`refRoll`).
* Compensación = **negativo** del roll relativo, normalizado a −180…+180 para
  que no pegue el salto de +180° a −180°.
* Rotación final = **rotación manual + compensación**. Con rotación manual 90°
  y horizonte −12°, el visor usa 78°. (Probado automáticamente.)
* Sin transiciones CSS: el giro se aplica en el `requestAnimationFrame`, con
  el último dato recibido. La respuesta es inmediata.

**El límite útil.** La escala mínima para que una imagen girada θ grados siga
cubriendo un visor de W×H es:

```
escala(θ) = max( (W·|cos θ| + H·|sin θ|) / w , (W·|sin θ| + H·|cos θ|) / h )
```

Con una imagen 4:3 dentro de un visor 4:3, tapar las esquinas a 15° exige
**1,31×** de zoom. Como pediste mantener el recorte entre **1,08× y 1,20×**,
el firmware hace lo que dijiste para el caso de pasarse: en vez de enseñar
esquinas negras, **recorta el ángulo** al máximo que ese zoom sí puede tapar,
y saca el aviso **"Límite de Horizon Lock"**. Ese máximo se recalcula solo
según la forma real del visor (por eso en móvil vertical sale ±15,0° y en
tablet horizontal ±11,9°) y se muestra en el panel como **"Límite útil"**.

¿Quieres los ±15° completos siempre? Sube `ZOOM_MAX` a `1.35` en el bloque de
constantes al principio del `<script>` de `fc_web_ui.h`. A cambio recortas más
encuadre. Las tres constantes están juntas y comentadas:

```js
const HORIZON_LIMIT_DEG = 15.0;
const ZOOM_MIN          = 1.08;
const ZOOM_MAX          = 1.20;
```

---

## 7. Rotación manual de la vista

Selector **0° / 90° / 180° / 270° / 360°**, independiente del IMU.

* Sólo gira el elemento visual en el navegador. Ningún fotograma se rota en
  el ESP32.
* Funciona con Horizon Lock apagado y se suma correctamente cuando está
  encendido.
* **360° se comporta exactamente igual que 0°** (`manualRot % 360`), pero se
  mantiene como opción visible en el selector, como pediste.
* En 90° y 270° el contenedor se reescala con la fórmula de cobertura de
  arriba: ni barras negras ni imagen deformada.
* La selección se guarda en `localStorage` y sobrevive a recargar.
* No reinicia la cámara ni el ESP32: es puro CSS.
* **No altera roll, pitch, yaw ni la calibración del IMU.**

---

## 8. Qué pasa cuando algo falla

| Situación | Comportamiento |
|---|---|
| BNO085 sin conectar | La cámara arranca igual. La web muestra "IMU no disponible". Se reintenta cada 2 s sin tocar la cámara. |
| BNO085 se suelta en caliente | Pasa a "Sin datos" a los 1,5 s, luego a reconectar. La cámara ni se entera. |
| El BNO085 se reinicia solo | Se detecta con `wasReset()` y se vuelven a pedir los informes. |
| Cámara no detectada al arrancar | Se avisa por serie con las causas típicas y se reintenta cada 30 s. **No se reinicia la placa en bucle.** |
| Cliente web que se va | `close_fn` del servidor limpia el descriptor; `lru_purge_enable` recicla sockets muertos. La tarea de telemetría se duerme si no hay nadie mirando. |
| Cambio de modo con el stream vivo | Se sube un contador de generación: los handlers MJPEG salen limpios, se toma el mutex, cambia el sensor y la web reengancha. |
| Cambio de modo mientras otro cambia el sensor | El segundo espera hasta 4 s y, si no, responde "cámara ocupada" en vez de romper nada. |
| Fallo al cambiar resolución/calidad | Se restaura el último modo válido y se informa a la web. |
| Sin PSRAM | Aviso explícito por serie y un solo framebuffer; los modos altos pueden no estar disponibles. |

Ningún `delay()` en las rutas de vídeo, IMU o telemetría; sólo uno de 50 ms en
`loop()`, que no hace trabajo real. Ninguna operación cara dentro de un
callback: la telemetría se arma en su propia tarea.

---

## 9. Reparto de tareas

| Núcleo | Qué corre |
|---|---|
| 0 | Wi-Fi/LWIP, servidor web `:80`, servidor MJPEG `:81` |
| 1 | Tarea del BNO085, tarea de telemetría WebSocket, `loop()` |

Presupuesto de sockets: `CONFIG_LWIP_MAX_SOCKETS=16`. El servidor web usa
5 (+3 internos) y el de vídeo 3 (+3) = 14. Los dos `ctrl_port` son distintos
(32768 y 32769) porque, si se repiten, el segundo servidor no arranca.

---

## 10. Ajustes rápidos (`fc_config.h`)

| Constante | Para qué |
|---|---|
| `FC_AP_SSID` / `FC_AP_PASSWORD` / `FC_AP_CHANNEL` | Red del ESP32 |
| `FC_CAM_PROFILE` | Perfil de pines de cámara |
| `FC_XCLK_HZ` | Reloj del sensor (20 MHz por defecto; 24 da algo más de FPS pero saca artefactos en varias placas) |
| `FC_IMU_INVERT_ROLL` | **Ponlo a 1 si el módulo está montado del revés** |
| `FC_IMU_DEADZONE_DEG` | Zona muerta, 0,30° por defecto (rango pedido 0,2–0,5) |
| `FC_IMU_SMOOTH` | Filtro: 1.0 = sin filtro; 0,45 por defecto |
| `FC_IMU_USE_GAME_RV` | 1 = Game Rotation Vector (sin brújula, más estable). 0 = Rotation Vector (yaw absoluto pero sensible a metales) |
| `FC_IMU_REPORT_MS` | 10 ms = 100 Hz pedidos al sensor |
| `FC_WS_IMU_HZ` | Ritmo de telemetría hacia el navegador (50 Hz) |

---

## 11. Relación con el resto del repositorio

Este repositorio contenía **sólo** la app Android *A55 Super Zoom*
(Kotlin + Gradle + CameraX). No había ni un archivo de ESP32, Arduino, OV5640
o BNO085: ningún pinout previo que conservar, ningún servidor web, ninguna
tabla de particiones, ningún método de compilación anterior.

Por eso el firmware entra **entero en `firmware/`**, sin tocar `app/`,
`build.gradle.kts`, `settings.gradle.kts`, `gradle/` ni `README_ES.md`.
Gradle ignora esa carpeta y el Arduino IDE ignora el resto. Las dos cosas
conviven sin interferir.
