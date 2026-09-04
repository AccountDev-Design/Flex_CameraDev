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

Compilado de verdad con `arduino-cli 1.2.0` + core **esp32 3.3.11** +
**SparkFun BNO08x 1.0.6**, con la misma configuración que usarás en el IDE:

```
Sketch uses 1068259 bytes (33%) of program storage space. Maximum is 3145728 bytes.
Global variables use 61148 bytes (18%) of dynamic memory, leaving 266532 bytes
for local variables. Maximum is 327680 bytes.
```

**Cero errores y cero warnings** con `--warnings all`.

**43 pruebas de la matemática del horizonte** (`firmware/tools/run_tests.sh`),
todas verdes. No son una reimplementación: compilan el mismo `fc_horizon.h`
que se enlaza en la placa. Incluyen los ángulos 0°, 45°, 90°, 179°, 180°,
181°, 270°, 360° y 720°, el recorrido continuo 0 → +720 → −720 sin saltos, el
signo de la compensación, el límite físico del cenit/nadir y el montaje.

**56 pruebas de la interfaz en Chromium**, todas verdes. Lo importante de
estas: no comprueban textos, **miden los píxeles del canvas**. Se sirve un
MJPEG simulado cuyos fotogramas llevan una barra blanca dibujada con la
inclinación que tendría en el sensor si la cámara estuviera girada, y después
se ajusta por mínimos cuadrados la inclinación de esa barra en el resultado:

| Giro físico | Inclinación residual medida en el canvas |
|---|---|
| 0°, 12°, 45°, 90°, 179°, 180°, 181°, 270°, 360°, 720°, −30° | **0,00°** en todos |
| Con Horizon Lock apagado (control) | 29,99° — es decir, no corrige, como debe |

También se verifica midiendo píxeles que **el JPEG estabilizado descargado**
está realmente girado (0,00° de inclinación dentro del archivo) mientras la
foto original se conserva intacta (29,99°), y que la grabación produce un
archivo real (MP4 de 34 KB en la prueba) que se descarga solo.

### Trabajo fusionado, no sobrescrito

Mientras se hacía esto, otra sesión subió cuatro commits a la misma rama.
**No se han pisado.** Este commit es una fusión que se queda con lo mejor de
cada lado:

| Parte | De dónde viene | Por qué |
|---|---|---|
| Capa de cámara con **pool latest-frame en PSRAM** (productor único, `fcCameraAcquireLatest`) | rama remota | Separa captura de red mejor que la versión anterior, y pediste conservarla |
| Métricas de firmware: FPS capturados y enviados, ms de captura y de envío, edad del frame, temperatura y límite térmico | rama remota | Se miden en el ESP32, que es donde se sabe la verdad |
| Flujo de trabajo de **GitHub Actions** | rama remota | Compila el firmware en cada push |
| Matemática del horizonte (`fc_horizon.h`) y sus 43 pruebas | esta rama | La remota usaba un suavizado fijo y no estaba verificada |
| Interfaz con **canvas de verdad**, foto estabilizada a archivo y grabación | esta rama | La remota giraba el preview con CSS y no exportaba foto estabilizada |
| Telemetría del horizonte, `/api/imucfg`, cabeceras `X-Ts`/`X-Seq`/`X-Horizon` | esta rama | Necesarias para el panel y para la foto estabilizada |

Se han eliminado nueve constantes de `fc_config.h` que quedaron sin uso tras
la fusión (`FC_IMU_SMOOTH`, `FC_HORIZON_SMOOTH`, `FC_IMU_CAMERA_*_AXIS`, …).
Dejarlas habría sido una trampa: se podrían ajustar sin que hicieran nada.

**No se ha probado sobre la placa física.** No tengo tu hardware delante.
Todo lo que depende del silicio real (FPS, ruido del sensor, alcance del
Wi-Fi, temperatura) está marcado abajo como estimación, y el firmware mide y
muestra los valores reales en la web.

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

### 2.3 No se usan `esp_camera_reconfigure`, `af_is_supported` ni `af_trigger`

Esas tres funciones **sí existen** en el core esp32 3.3.11 (están en
`esp_camera.h` y `sensor.h` del propio core, y una versión anterior de este
firmware compilaba con ellas). Aun así se han quitado: `esp_camera_reconfigure`
no es más que `esp_camera_deinit()` seguido de `esp_camera_init()`, que es lo
que hace ahora el firmware, y así compila igual en cualquier versión del core
sin depender de añadidos recientes. El autofoco se ha retirado por lo mismo.

### 2.4 El pin INT del BNO085 no se le pasa a la librería

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

### 2.5 No se incluye `partitions.csv` a propósito

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

| Modo | Preview | Captura | FPS estimado | Buffers en PSRAM |
|---|---|---|---|---|
| Foto 5 MP | 800×600 | **2560×1920** | preview 20–30 | 2×94 KB, disparo 1×960 KB |
| Alta calidad | 1600×1200 | 1600×1200 | 8–15 | 2×375 KB |
| Vista fluida | 800×600 | 800×600 | 20–30 | 2×94 KB |
| Horizon Lock | 800×600 | 800×600 | 20–30 | 2×94 KB |
| Horizon Lock Ultra | 640×480 | 640×480 | 25–40 | 2×60 KB |

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

## 6. Horizon Lock: cómo funciona de verdad

### 6.1 El ángulo NO sale del roll de Euler

El roll de Euler se vuelve inestable cuando la cámara se inclina hacia arriba
o hacia abajo, y cerca de la vertical se dispara. Por eso el ángulo se calcula
**proyectando la gravedad sobre los ejes reales de la cámara**:

```
gravedad en ejes del sensor, a partir del cuaternión del BNO085:
    gx =  2(wy − xz)
    gy = −2(yz + wx)
    gz =  2(x² + y²) − 1

horizonte = atan2(−g·derecha, g·abajo)
confianza = |(g·derecha, g·abajo)|          -> 0..1
```

El firmware usa el **Game Rotation Vector**, no el roll. Emite el cuaternión y
el vector gravedad completos, y los dos se ven en el panel.

**Signo.** El valor que sale es la inclinación de la *vertical del mundo tal y
como aparece dentro de la imagen*, no el giro de la cámara. Si la cámara rueda
+30° en sentido horario, el contenido del fotograma aparece inclinado −30°, así
que el firmware emite −30. Con eso la fórmula queda exactamente como pediste:

```
compensación   = −(horizonte_actual − horizonte_referencia)
rotación_final = rotación_manual + compensación
```

y el visor gira +30°, devolviendo el contenido a la vertical. Verificado
midiendo píxeles, no razonando sobre el signo.

### 6.2 Continuidad: 180°, 360°, 720° y más

El ángulo crudo vive en −180…+180. El firmware mantiene aparte un **ángulo
continuo**: en cada muestra acumula la diferencia angular más corta, así el
valor pasa por 180, 360, 720 y sigue subiendo sin ninguna discontinuidad. La
web usa ese continuo tal cual, sin volver a envolverlo, de modo que la
compensación tampoco salta nunca. En la prueba, dos vueltas hacia delante y
cuatro hacia atrás dan un salto máximo de 1,0001° con un paso de muestreo de
1,0000°: es decir, ninguno.

### 6.3 Filtro: suave parado, rápido en giro

Un solo filtro, en el firmware. La web **no vuelve a filtrar**, para no sumar
dos retrasos. Es un polo simple dependiente de `dt` con constante de tiempo
adaptativa:

* parado → τ = 0,110 s, no tiembla;
* giro real → τ = 0,022 s, sigue el movimiento sin arrastre;
* zona muerta de 0,25°, así un temblor pequeño no mueve la imagen (medido:
  0,000000000° de desvío ante un temblor de 0,15°);
* un giro real de 45° se sigue hasta el 90% en **60 ms**.

Cada muestra lleva marca de tiempo y número de secuencia, y `dt` está acotado
para que un hueco en el reloj no se traduzca nunca en un salto de imagen.

### 6.4 El límite físico: cenit y nadir

Cuando la cámara apunta recto arriba o recto abajo, toda la gravedad se va por
el eje óptico y **el horizonte queda matemáticamente indeterminado**. No hay
truco posible: no existe información para saber qué es «arriba» en la imagen.

Confianza medida frente a la inclinación de la óptica:

| Óptica levantada | Confianza |
|---|---|
| 0° (al horizonte) | 1,000 |
| 45° | 0,707 |
| 80° | 0,174 |
| 89° | 0,018 → sin referencia |
| 90° (al cenit) | 0,000 → sin referencia |

Por debajo del umbral (0,17 para entrar, 0,12 para salir, con histéresis para
no parpadear) el firmware:

* **congela el último ángulo válido**, exacto: en la prueba, un segundo
  apuntando al cenit con ruido en el ángulo da 0,000000000° de deriva;
* la web muestra **«Horizonte sin referencia»** sobre el visor y en el panel;
* al recuperar la referencia **desliza hasta el ángulo verdadero en vez de
  saltar** (primer paso medido: 0,058°), usando el representante del ángulo
  continuo más cercano al valor congelado.

Nunca se inventa un valor ni se produce un giro errático.

### 6.5 Dónde ocurre el giro: en un canvas, no en CSS

El ESP32 sigue mandando su MJPEG sin tocar un píxel. En el navegador:

1. El stream se lee con `fetch` y se trocea el multipart a mano. Cada parte
   trae `X-Ts` y `X-Seq`, que el ESP32 añade por fotograma: gracias a eso el
   panel puede mostrar **edad del frame y latencia reales**, no estimadas.
   Cada JPEG se decodifica con `createImageBitmap`.
2. Si ya hay una decodificación en curso, el fotograma pendiente anterior **se
   descarta**. Nunca se forma una cola: siempre gana el más reciente.
3. Cada `requestAnimationFrame` se dibuja en un `<canvas>` con
   `rotate(rotación_final)` y `scale(zoom)`. Ese canvas es el visor.
4. Si el navegador no trae streams o `createImageBitmap`, se cae solo a un
   `<img>` con MJPEG. En ese modo la edad del frame es aproximada y se nota.

Como el giro está en los píxeles del canvas y no en una transformación CSS,
**lo que se exporta a foto y lo que graba `MediaRecorder` es exactamente lo
que se ve**.

### 6.6 Perfiles de encuadre

La escala mínima para que una imagen w×h girada θ grados cubra un lienzo W×H es:

```
escala(θ) = max( (W·|cos θ| + H·|sin θ|)/w , (W·|sin θ| + H·|cos θ|)/h )
```

Y el peor caso sobre **todos** los ángulos tiene solución cerrada:

```
escala_360 = √(W² + H²) / min(w, h)
```

Para 4:3 dentro de 4:3 son exactamente **5/3 = 1,667×**. No es un número
elegido a ojo; el firmware lo calcula con esa fórmula (verificado: 1,6667).

| Perfil | Qué hace |
|---|---|
| **Dinámico** | `escala(θ)` exacta en cada fotograma. Nunca hay bordes negros, el zoom respira con el ángulo. |
| **Estable 360°** | `escala_360` fija. El encuadre no cambia nunca, aunque des vueltas completas. Es el que conviene para grabar. |
| **Amplio** | Tope de 1,25×. Recorta mucho menos, pero **avisa en pantalla** en cuanto el ángulo pide más de lo que ese zoom puede tapar. |

Con rotación manual de 90° o 270° el lienzo **se pone de canto** (800×600 pasa
a 600×800), así la imagen encaja sin tener que recortar un tercio sólo por
estar de lado.

## 7. Rotación manual de la vista

Selector **0° / 90° / 180° / 270° / 360°**, completamente independiente del IMU.

* Sólo gira el lienzo en el navegador. Ningún fotograma se rota en el ESP32.
* Funciona con Horizon Lock apagado y se suma correctamente cuando está
  encendido: `rotación_final = manual + compensación`. Verificado: manual 90° y
  compensación −12° dan **78,00°**.
* **360° se comporta exactamente igual que 0°** (`manualRot % 360`), pero se
  mantiene visible en el selector. Verificado: mismo lienzo 800×600 y misma
  inclinación resultante.
* En 90° y 270° el lienzo se pone de canto (600×800): ni barras negras ni
  imagen deformada.
* No cambia roll, pitch, yaw ni la calibración del IMU. Verificado.
* Se guarda en `localStorage` y sobrevive a recargar.
* No reinicia la cámara ni el ESP32.
* **Se bloquea durante la grabación**: cambiar el tamaño del lienzo con
  `MediaRecorder` en marcha corrompe el archivo. La web lo dice al intentarlo.

---

## 8. Foto

El obturador (pestaña **FOTO**) hace dos cosas:

1. Pide `/api/photo` al ESP32 y guarda el **JPEG original tal cual**.
2. Si Horizon Lock está activo, dibuja ese JPEG en un canvas aparte a
   resolución completa con **la misma rotación, zoom y recorte** del visor, y
   genera un **JPEG nuevo realmente estabilizado** (calidad 0,92).

Tocando la miniatura se descargan los dos archivos: `..._estabilizada.jpg` y
`..._original.jpg`. Con Horizon Lock apagado se descarga sólo el original.

El ángulo que se aplica **no se adivina**: la respuesta de `/api/photo` trae
las cabeceras `X-Horizon`, `X-Hvalid` y `X-Hepoch` con el ángulo del instante
exacto del disparo, medido en el ESP32. Entre que se pide la foto y llega el
JPEG pasan cientos de milisegundos (a 2560×1920 hay que reconfigurar el
sensor) y la cámara puede haberse movido en ese rato.

El panel informa siempre de lo real, nunca de lo nominal:

```
Original 800×600 (18 KB) · Estabilizada 800×600 (15 KB), recorte 1.533×, giro 30.0°
```

Las dimensiones salen de decodificar el archivo recibido, así que si el driver
entrega 2560×1920 es eso lo que se muestra, nunca 2592×1944.

---

## 9. Vídeo

Pestaña **VÍDEO**. El obturador pasa a rojo y se convierte en botón de
grabar/detener.

* Se graba **el canvas ya corregido** con `canvas.captureStream(fps)`. Con
  Horizon Lock activo **nunca** se graba el MJPEG bruto.
* `MediaRecorder` prueba **MP4 (avc1) primero** y cae a WebM (VP9 → VP8) si el
  navegador no lo soporta. El aviso en pantalla dice cuál se está usando.
* FPS elegible: 15 / 24 / 30. El bitrate se calcula a partir de los píxeles y
  los fps, acotado entre 0,8 y 8 Mb/s, para no ahogar el móvil.
* Durante la grabación se ve un indicador con **tiempo y tamaño acumulado**.
* Al detener, el archivo se **descarga solo** a la carpeta de Descargas.
* Al detener se paran los tracks, se limpia el temporizador y se revocan los
  object URLs anteriores. Verificado en las pruebas: sin temporizadores vivos,
  `MediaRecorder` y stream a `null`.

Los cambios de modo y de resolución quedan bloqueados mientras se graba, por
la misma razón que la rotación manual.

---

## 10. Panel de detalle

Se abre con la flecha junto al botón de Horizon Lock, y **no tapa el visor**:
en móvil ocupa la parte de abajo con scroll propio; en tablet horizontal pasa
a una columna lateral. Muestra en tiempo real:

**Estado** — Activo / Apagado / Sin referencia · compensación congelada / IMU
no disponible · horizonte bruto · horizonte continuo · ángulo de referencia ·
compensación aplicada · dirección (horaria ↻ / antihoraria ↺ / centrado) ·
rotación total · zoom y recorte aplicados.

**IMU** — roll · pitch · yaw · frecuencia real en Hz · confianza de gravedad
con barra · calibración · **cuaternión completo** (i, j, k, real) · **vector
gravedad** (x, y, z).

**Rendimiento** — FPS de captura (fotogramas decodificados en el navegador) ·
FPS enviados (los que cuenta el ESP32) · FPS de render · edad del último frame
· latencia relativa · frames perdidos · temperatura del chip · reinicios del
IMU · memoria libre interna y PSRAM.

> La latencia es **relativa**: se descuenta el mejor trayecto observado, lo que
> absorbe el desfase entre el reloj del ESP32 y el del móvil. Sirve para ver
> jitter y degradación, no como valor absoluto. Prefiero decirlo a presentar
> un número absoluto que no puedo medir con dos relojes sin sincronizar.

**Controles del panel** — activar/desactivar Lock · recentrar horizonte ·
cuadrícula · rotación visual manual · perfil de encuadre · montaje del IMU
(0/90/180/270) · eje óptico del IMU (Z/Y/X) · sentido normal/invertido ·
calidad de vídeo · voltear sensor.

El montaje, el sentido y el plano se cambian **en caliente**, sin reprogramar:
van a `/api/imucfg` y el firmware sube un contador de época. La web ve ese
cambio y **retoma la referencia sola**, para que cambiar el montaje no provoque
un salto en la imagen.

Horizon Lock **nunca se activa solo**. Sólo se enciende cuando lo pulsas.

## 11. Qué pasa cuando algo falla

| Situación | Comportamiento |
|---|---|
| BNO085 sin conectar | La cámara arranca igual. La web muestra "IMU no disponible". Se reintenta cada 2 s sin tocar la cámara. |
| BNO085 se suelta en caliente | Pasa a "Sin datos" a los 1,5 s, luego a reconectar. La cámara ni se entera. |
| El BNO085 se reinicia solo | Se detecta con `wasReset()` y se vuelven a pedir los informes. |
| Cámara no detectada al arrancar | Se avisa por serie con las causas típicas y se reintenta cada 30 s. **No se reinicia la placa en bucle.** |
| Cliente web que se va | `close_fn` del servidor limpia el descriptor; `lru_purge_enable` recicla sockets muertos. La tarea de telemetría se duerme si no hay nadie mirando. |
| Cambio de modo con el stream vivo | Se sube un contador de generación: los handlers MJPEG salen limpios, se toma el mutex, se reconfigura y la web reengancha. Nunca se queda colgado. |
| Cambio de modo mientras otro reconfigura | El segundo espera hasta 4 s y, si no, responde "cámara ocupada" en vez de romper nada. |
| Fallo al reconfigurar | Se intenta volver al modo anterior; si tampoco, se marca la cámara como caída y se dice en la web. |
| Sin PSRAM | Aviso explícito por serie y `fb_count` baja a 1 en vez de fallar sin explicación. |
| Cámara apuntando al cenit o al nadir | Congela la última compensación válida, avisa «Horizonte sin referencia» y al volver desliza hasta el ángulo verdadero. Nunca salta ni inventa. |
| El navegador no puede trocear el MJPEG | Cae solo a `<img>` con MJPEG. Sigue estabilizando; la edad de frame pasa a ser aproximada. |
| El navegador no soporta MP4 | Graba en WebM y lo dice en pantalla. |
| El navegador no soporta `MediaRecorder` | Lo dice y deja la foto funcionando. |
| Se cambia el montaje del IMU con el Lock activo | La web detecta el cambio de época y retoma la referencia sola, sin salto. |
| Pestaña en segundo plano | Al volver se relanza el stream si murió. `Stream.start()` siempre cierra el anterior: nunca hay dos. |

Ningún `delay()` en las rutas de vídeo, IMU o telemetría; sólo uno de 50 ms en
`loop()`, que no hace trabajo real. Ninguna operación cara dentro de un
callback: la telemetría se arma en su propia tarea.

---

## 12. Reparto de tareas

| Núcleo | Qué corre |
|---|---|
| 0 | Wi-Fi/LWIP, servidor web `:80`, servidor MJPEG `:81` |
| 1 | Tarea del BNO085, tarea de telemetría WebSocket, `loop()` |

Presupuesto de sockets: `CONFIG_LWIP_MAX_SOCKETS=16`. El servidor web usa
5 (+3 internos) y el de vídeo 3 (+3) = 14. Los dos `ctrl_port` son distintos
(32768 y 32769) porque, si se repiten, el segundo servidor no arranca.

---

## 13. Ajustes rápidos (`fc_config.h`)

| Constante | Para qué |
|---|---|
| `FC_AP_SSID` / `FC_AP_PASSWORD` / `FC_AP_CHANNEL` | Red del ESP32 |
| `FC_CAM_PROFILE` | Perfil de pines de cámara |
| `FC_XCLK_HZ` | Reloj del sensor (20 MHz por defecto; 24 da algo más de FPS pero saca artefactos en varias placas) |
| `FC_IMU_INVERT_ROLL` | Sentido inicial (también se cambia desde la web) |
| `FC_IMU_MOUNT_DEG` / `FC_IMU_PLANE` | Montaje inicial (también desde la web) |
| `FC_HORIZON_DEADZONE` | Zona muerta, 0,25° por defecto (rango pedido 0,2–0,5) |
| `FC_HORIZON_TAU_SLOW` / `FC_HORIZON_TAU_FAST` | Filtro adaptativo: quieto / en giro |
| `FC_HORIZON_CONF_HI` / `FC_HORIZON_CONF_LO` | Umbrales de «sin referencia», con histéresis |
| `FC_IMU_USE_GAME_RV` | 1 = Game Rotation Vector (sin brújula, más estable). 0 = Rotation Vector (yaw absoluto pero sensible a metales) |
| `FC_IMU_REPORT_MS` | 10 ms = 100 Hz pedidos al sensor |
| `FC_WS_IMU_HZ` | Ritmo de telemetría hacia el navegador (50 Hz) |

---

## 14. Relación con el resto del repositorio

Este repositorio contenía **sólo** la app Android *A55 Super Zoom*
(Kotlin + Gradle + CameraX). No había ni un archivo de ESP32, Arduino, OV5640
o BNO085: ningún pinout previo que conservar, ningún servidor web, ninguna
tabla de particiones, ningún método de compilación anterior.

Por eso el firmware entra **entero en `firmware/`**, sin tocar `app/`,
`build.gradle.kts`, `settings.gradle.kts`, `gradle/` ni `README_ES.md`.
Gradle ignora esa carpeta y el Arduino IDE ignora el resto. Las dos cosas
conviven sin interferir.
