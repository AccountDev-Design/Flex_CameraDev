# FlexCam S26 — ESP32-S3 N16R8 + OV5640 + BNO085

Firmware Arduino para una cámara Wi-Fi autónoma. El ESP32-S3 crea su propio
punto de acceso, transmite MJPEG, publica la orientación del BNO085 y sirve una
interfaz táctil sin depender de Internet.

La estabilización visual se realiza en el navegador. El ESP32 no decodifica ni
recomprime el JPEG: entrega el último frame y el cuaternión. Esto conserva el
máximo rendimiento que permite el ESP32-S3.

> El firmware es independiente de la app Android del repositorio. Todo vive en
> `firmware/esp32s3_flexcam/`.

## Qué cambió en esta versión

- Productor único de cámara: ningún cliente HTTP captura directamente.
- Pool de tres frames en PSRAM con política **latest-frame**.
- La red nunca conserva el mutex de cámara mientras envía datos.
- Backpressure: se descarta un frame atrasado en vez de crear segundos de cola.
- Fotografía copiada fuera del framebuffer antes de enviarse por HTTP.
- Modo de máxima resolución en vivo, con FPS deliberadamente limitado.
- Horizon Lock continuo 360° basado en gravedad proyectada desde el cuaternión.
- Cruce de ±180° y varias vueltas sin el límite anterior de ±15°.
- Tres perfiles de recorte y ajuste de sentido/montaje del IMU.
- Grabación opcional del visor estabilizado en el navegador.
- Métricas de captura, red, descartes, memoria, antigüedad y temperatura.
- Limitación térmica automática y potencia Wi-Fi reducida a 15 dBm.
- Compilación reproducible en `.github/workflows/firmware-esp32s3.yml`.

## Conexionado

### Cámara OV5640 — perfil predeterminado

El perfil `FC_CAM_PROFILE_S3_STD` coincide con el diagrama de la placa enviada:

| Señal OV5640 | GPIO | Señal OV5640 | GPIO |
|---|---:|---|---:|
| SIOD / SCCB SDA | 4 | D0 / Y2 | 11 |
| SIOC / SCCB SCL | 5 | D1 / Y3 | 9 |
| VSYNC | 6 | D2 / Y4 | 8 |
| HREF | 7 | D3 / Y5 | 10 |
| PCLK | 13 | D4 / Y6 | 12 |
| XCLK | 15 | D5 / Y7 | 18 |
| PWDN | sin conectar | D6 / Y8 | 17 |
| RESET | sin conectar | D7 / Y9 | 16 |

Al arrancar debe aparecer:

```text
[CAM] PID sensor=0x5640 (OV5640 correcto)
```

Si no aparece, el perfil físico de la placa es distinto o el cable flex no está
bien insertado. No se debe adivinar otro pinout.

### BNO085 / GY-BNO08X

| BNO085 | ESP32-S3 | Nota |
|---|---:|---|
| VCC | 3V3 | No alimentar la lógica a 5 V |
| GND | GND | Masa común obligatoria |
| SDA | GPIO21 | `Wire`, I²C 0 |
| SCL | GPIO47 | `Wire`, I²C 0 |
| INT | GPIO1 | Recomendado; opcional en el firmware |
| RST | sin conectar | Configurado como `-1` |
| AD0, PS0, PS1 | sin conectar | Dirección habitual `0x4B`, modo I²C |

La cámara usa su propio SCCB en I²C 1. El BNO085 debe permanecer en `Wire`
(I²C 0) para que ambos periféricos no compitan.

## Configuración de Arduino IDE

Probado contra estas APIs y fijado también en CI:

- Arduino CLI 1.2.0.
- Core `esp32:esp32` 3.3.11.
- `SparkFun BNO08x Cortex Based IMU` 1.0.6.

Abre `firmware/esp32s3_flexcam/esp32s3_flexcam.ino` y selecciona:

| Opción | Valor |
|---|---|
| Placa | ESP32S3 Dev Module |
| PSRAM | **OPI PSRAM** |
| Flash Size | **16MB (128Mb)** |
| Flash Mode | QIO 80MHz |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** |
| CPU Frequency | 240MHz (WiFi) |
| Arduino Runs On | Core 1 |
| Events Run On | Core 0 |
| USB CDC On Boot | Enabled |
| Core Debug Level | None |

La captura de alta resolución y el pool dependen de la PSRAM. Si el menú sigue
mostrando `PSRAM: Disabled`, no es una configuración válida para este firmware.

## Acceso a la cámara

1. Conéctate a `FlexCam-S26`.
2. Contraseña: `FlexCam2026`.
3. Si Android avisa que no hay Internet, elige mantener la conexión.
4. Abre `http://192.168.4.1/`.

La interfaz/API usa el puerto 80 y el MJPEG el 81.

## Resolución real del OV5640

El sensor físico tiene una matriz de 2592×1944, pero `esp32-camera` 2.1.3
declara para el OV5640 un máximo `FRAMESIZE_QSXGA`, es decir **2560×1920 =
4.92 MP**. Pedir 2592×1944 en ese driver se recorta internamente; etiquetarlo
como 2592×1944 sería incorrecto.

El firmware ahora solicita `FRAMESIZE_5MP` al iniciar y consulta después el
tamaño que el driver realmente aceptó:

- Con el driver incorporado en Arduino-ESP32 3.3.11: 2560×1920.
- Con una versión futura/compatible que permita `FRAMESIZE_5MP` para OV5640:
  2592×1944, sin cambiar la UI.

La foto descargada incluye cabeceras con ancho, alto y bytes reales; la web
muestra esas dimensiones después de cada disparo. No se escala una imagen
pequeña para fingir 5 MP.

## Modos de cámara

Los FPS son objetivos, no promesas: el valor real aparece en la interfaz.

| Modo | Preview efectivo con core 3.3.11 | Captura | Objetivo |
|---|---:|---:|---:|
| Foto 5 MP | 800×600 | 2560×1920 | 24 FPS en preview |
| 5 MP en vivo | 2560×1920 | 2560×1920 | 4 FPS |
| Alta calidad | 1600×1200 | 1600×1200 | 10 FPS |
| Vista fluida | 800×600 | 800×600 | 30 FPS |
| Horizon Lock | 800×600 | 800×600 | 30 FPS |
| Horizon Lock Ultra | 640×480 | 640×480 | hasta 40 FPS |

No es físicamente realista exigir 2560×1920 y 30–40 FPS MJPEG simultáneos a
este ESP32-S3. El modo 5 MP en vivo prioriza detalle; Horizon Lock Ultra
prioriza fluidez.

## Por qué ya no se congela toda la cámara por un cliente lento

Flujo actual:

1. Una tarea obtiene un único frame del driver.
2. Lo copia a un slot libre en PSRAM y devuelve inmediatamente el framebuffer.
3. Publica ese slot como el frame más reciente.
4. Cada handler MJPEG adquiere una referencia de lectura y la libera al enviar.
5. Si los tres slots están retenidos, el productor descarta el frame nuevo.

Por tanto, el socket puede bloquear como máximo a su propio handler. No mantiene
el sensor bloqueado, no captura por cliente y no acumula una cola de vídeo viejo.

La telemetría WebSocket solo envía muestras nuevas, con un techo de 50 Hz y un
latido de 500 ms. Los números de la UI se actualizan a 20 Hz, mientras la
compensación usa inmediatamente la última muestra en cada `requestAnimationFrame`.

## Horizon Lock 360°

Usar directamente el Euler `roll` falla si el eje del IMU no coincide con el
eje óptico y sufre singularidades cerca de ciertas orientaciones. Esta versión:

1. Normaliza el cuaternión del Game Rotation Vector.
2. Calcula el vector gravedad en coordenadas del BNO085.
3. Lo proyecta sobre los ejes derecha/abajo de la imagen.
4. Obtiene el horizonte con `atan2`.
5. Desenvuelve el ángulo para conservar vueltas continuas.
6. Aplica en el navegador exactamente el giro contrario.

El botón de recentrado define la posición inicial. La rotación manual de cámara
0°/90°/180°/270°/360° solamente gira la imagen; no modifica roll, pitch, yaw ni
el cuaternión.

### Montaje físico del IMU

En `fc_config.h`:

```cpp
#define FC_IMU_CAMERA_RIGHT_AXIS  (+1) // +X
#define FC_IMU_CAMERA_DOWN_AXIS   (+2) // +Y
#define FC_IMU_HORIZON_SIGN       (+1.0f)
```

Los ejes firmados posibles son `±1 = X`, `±2 = Y`, `±3 = Z`. Los dos ejes deben
ser distintos. El valor predeterminado supone que el BNO085 está paralelo al
plano de la cámara. La web permite corregir la rotación plana 0/90/180/270 y el
sentido sin recompilar.

Si al girar físicamente a la derecha la imagen también gira a la derecha,
selecciona `Sentido IMU: Invertido`. Eso corrige el signo de la compensación;
no altera los datos crudos mostrados.

### Caso sin horizonte definido

Cuando el eje óptico apunta casi exactamente en dirección de la gravedad, su
proyección sobre la imagen tiende a cero. En esa posición no existe una dirección
de horizonte observable. El firmware marca baja confianza y la web conserva la
última corrección; no salta a cero.

## Recorte durante giros extremos

Girar un rectángulo crea esquinas vacías. No se pueden eliminar sin recortar o
mostrar bordes. Para una imagen 4:3, cubrir todos los ángulos requiere hasta
aproximadamente **1.667×**.

| Perfil | Comportamiento | Coste |
|---|---|---|
| Dinámico | Usa el zoom mínimo necesario en cada ángulo | El encuadre respira |
| Fijo 360° | Mantiene el recorte seguro máximo | Pierde más campo visual |
| Amplio | Limita el zoom a 1.20× | Puede mostrar bordes en giros grandes |

Ningún perfil vuelve a limitar el ángulo de estabilización.

## Grabación estabilizada

El botón rojo graba el resultado visual compuesto en un `canvas` del navegador:

- Incluye Horizon Lock, rotación manual y recorte.
- Usa hasta 1280×960 y hasta 30 FPS para no saturar el móvil.
- Descarga MP4 si el navegador lo admite; en caso contrario, WebM.
- No incluye audio.
- Se detiene a los cinco minutos para no agotar la memoria del navegador.
- La grabación ocurre en el móvil/tablet, no se guarda en la flash del ESP32.

La fotografía del obturador permanece como JPEG directo del sensor y conserva
la máxima resolución disponible.

## Temperatura y seguridad

Las medidas aplicadas por software son:

- Potencia Wi-Fi reducida de 20 dBm a 15 dBm.
- Productor dormido cuando no hay clientes MJPEG.
- Límites de FPS por modo.
- IMU solicitado a 100 Hz y sondeado a 120 Hz, no 200 Hz inútiles.
- A 75 °C internos, objetivo máximo de 15 FPS.
- A 85 °C internos, objetivo máximo de 5 FPS.
- Temperatura y estado térmico visibles en la web.

El sensor térmico interno indica la tendencia del SoC; no mide el regulador,
la PSRAM ni la superficie. Si un componente quema al tocarlo después de tres
minutos, desconecta la alimentación. Un corto, regulador dañado, 5 V entrando a
3V3 o una placa defectuosa no se corrigen con firmware.

## Diagnóstico visible

El panel muestra:

- Roll, pitch, yaw y horizonte continuo.
- Compensación, rotación total, zoom y confianza.
- Cuaternión W/X/Y/Z y gravedad X/Y/Z.
- Frecuencia real del IMU.
- FPS capturados y entregados.
- Tiempo medio de captura/copia y envío.
- Frames fallidos y descartados por atraso.
- Tamaño y antigüedad del último frame.
- DRAM, PSRAM y temperatura.

El monitor serie emite un resumen cada diez segundos con las mismas señales
principales.

## Límites de verificación

Se realizan estas comprobaciones sin hardware:

- Sintaxis C++ contra las cabeceras públicas de esp32-camera 2.1.3,
  ESP-IDF 5.5 y SparkFun BNO08x 1.0.6.
- JavaScript con `node --check`.
- Geometría de cobertura en 0°, 15°, 45°, 90°, 180°, 360° y 720°.
- Integridad de IDs y ausencia de desbordamiento estático en la UI.
- Compilación Arduino completa mediante el workflow del repositorio.

Lo que exige la placa física y no debe fingirse como comprobado:

- FPS reales en cada escena.
- Temperatura del componente exacto que se calienta.
- Signo/ejes correctos para la forma concreta en que montes el BNO085.
- Prueba continua de 30 minutos y varias vueltas manuales.

Después de flashear, esas cuatro pruebas son obligatorias antes de considerar
estable el montaje final.
