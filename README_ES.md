# A55 Super Zoom

Aplicación de cámara para Android escrita en Kotlin sobre **CameraX** y **Camera2**,
pensada y ajustada para el **Samsung Galaxy A55 5G**, pero preparada para funcionar
en otros teléfonos Android consultando siempre las capacidades reales del aparato.

* Fotografía a la resolución JPEG más alta que el dispositivo acepte.
* Zoom de cámara real y **Super Zoom digital experimental hasta 1000×**.
* Vídeo normal y de alta velocidad, solo en las combinaciones que el teléfono confirma.
* Nivelación y bloqueo de horizonte con la IMU y un paso de GPU (OpenGL ES).
* Interfaz original oscura de estilo *Liquid Glass*, con animaciones cortas.

---

## 1. Carpeta que hay que abrir en Android Studio

> **Abre la carpeta raíz del repositorio.**
>
> `Flex_CameraDev/`  ← esta es la carpeta que se selecciona en *Open*

Dentro está el proyecto Gradle completo:

```
Flex_CameraDev/
├── settings.gradle.kts        ← proyecto raíz
├── build.gradle.kts
├── gradle.properties
├── gradle/
│   ├── libs.versions.toml     ← versiones de todas las dependencias
│   └── wrapper/
│       ├── gradle-wrapper.jar
│       └── gradle-wrapper.properties
├── gradlew                    ← wrapper para Linux y macOS
├── gradlew.bat                ← wrapper para Windows
├── app/                       ← el módulo de la aplicación
└── README_ES.md
```

No hace falta ninguna carpeta externa. No abras `app/` directamente: abre la raíz.

---

## 2. Guía paso a paso para principiantes

### 1. Clonar o descargar el repositorio

Con Git:

```bash
git clone https://github.com/AccountDev-Design/Flex_CameraDev.git
cd Flex_CameraDev
git checkout claude/a55-super-zoom-d4vzzp
```

Sin Git: en GitHub pulsa **Code → Download ZIP** y descomprime la carpeta.

### 2. Abrir Android Studio

Instala **Android Studio** (versión Ladybug 2024.2 o posterior). En el primer
arranque deja que descargue el SDK que te proponga.

### 3. Pulsar «Open»

En la pantalla de bienvenida, pulsa **Open** (no *New Project*, ni *Get from VCS*
si ya lo descargaste).

### 4. Seleccionar la carpeta exacta

Elige la carpeta **`Flex_CameraDev`** (la que contiene `settings.gradle.kts`) y
acepta.

### 5. Esperar a Gradle

Android Studio descargará Gradle 8.9, el Android Gradle Plugin 8.7.3, Kotlin
2.0.21 y las bibliotecas de AndroidX/CameraX. La primera vez tarda varios
minutos y necesita conexión a Internet.

Si te pide instalar el **Android SDK Platform 35** o las **Build Tools**, acepta.
Si te avisa de que falta `local.properties`, Android Studio lo crea solo: ese
archivo **no** está en Git a propósito, porque contiene la ruta de tu SDK.

### 6. Conectar el Galaxy A55 5G

Conecta el teléfono al ordenador con un cable USB que transmita datos (no uno
que solo cargue).

### 7. Activar las opciones de desarrollador

En el teléfono: **Ajustes → Acerca del teléfono → Información de software** y
pulsa **siete veces** sobre *Número de compilación*. Aparecerá el mensaje
«Ya eres desarrollador».

### 8. Activar la depuración USB

**Ajustes → Opciones de desarrollador → Depuración USB** (activar).
Al conectar el cable el teléfono preguntará si autorizas el ordenador: acepta y
marca «Permitir siempre».

### 9. Seleccionar el teléfono

En la barra superior de Android Studio, en el desplegable de dispositivos, elige
**SM-A556B** (o el nombre que muestre tu A55).

### 10. Pulsar Run

Pulsa el botón verde ▶ (*Run 'app'*). Android Studio compila, instala y abre la
aplicación.

Alternativa por terminal:

```bash
./gradlew assembleDebug       # Linux / macOS
gradlew.bat assembleDebug     # Windows
```

### 11. Aceptar cámara y micrófono

* Al abrir la aplicación se pide el permiso de **cámara**: es obligatorio.
* El permiso de **micrófono** **no** se pide al iniciar. Solo se solicita cuando
  activas el micrófono o cuando vas a grabar con audio. Si lo rechazas, se puede
  grabar igualmente **sin audio**.

### 12. Tomar una fotografía

Con el selector inferior en **FOTO**, pulsa el botón blanco grande.
Verás un destello blanco breve y un indicador de procesamiento.

### 13. Revisar los megapíxeles

Justo después de guardar, la aplicación lee las **dimensiones reales del archivo**
y muestra un aviso como:

```
8160 × 6120 · 49.9 MP
```

o bien:

```
4080 × 3060 · 12.5 MP
```

Antes de capturar, la barra superior muestra `MAX MP` (no «50 MP»), porque
hasta que no existe el archivo no se puede afirmar la resolución obtenida.

Si el archivo resultante es claramente menor que la resolución que la cámara
anuncia, aparece:

> «Samsung ha limitado esta captura a 12.5 MP mediante las APIs disponibles.»

### 14. Grabar vídeo

Cambia el selector inferior a **VIDEO**. El botón blanco se transforma en un
círculo rojo; al grabar se convierte en un cuadrado rojo y aparece el indicador
**REC** con el contador, el tamaño aproximado y un botón **Pausar**.

### 15. Elegir resolución y FPS

Pulsa el botón de calidad (muestra el modo actual, por ejemplo `FHD · 30`).
Se abre el panel con las siete combinaciones solicitadas:

| Opción      | Tipo            |
|-------------|-----------------|
| `4K · 30`   | NORMAL          |
| `FHD · 30`  | NORMAL          |
| `FHD · 60`  | NORMAL          |
| `HD · 30`   | NORMAL          |
| `HD · 60`   | NORMAL          |
| `HD · 120`  | ALTA VELOCIDAD  |
| `HD · 240`  | ALTA VELOCIDAD  |

Cada fila indica **NORMAL**, **ALTA VELOCIDAD** o **NO DISPONIBLE**. Cuando una
opción no está disponible, debajo se explica exactamente por qué (la cámara no
publica ese tamaño, no publica esa cadencia, o ningún codificador del sistema la
acepta). Las opciones no disponibles no se pueden seleccionar.

En los modos de alta velocidad puedes elegir entre **Velocidad real** (archivo
con muchos fotogramas por segundo) y **Cámara lenta** (el archivo se reproduce a
30 fps, así que se ve ralentizado).

### 16. Usar Horizon Lock

Pulsa el botón de horizonte, junto al de calidad. Hay tres opciones:

* **DESACTIVADO** — sin corrección.
* **NIVELACIÓN** — corrige inclinaciones pequeñas con un recorte moderado.
* **HORIZON LOCK · EXPERIMENTAL** — permite correcciones mayores a cambio de más
  recorte.

Sobre la vista previa verás una línea de horizonte animada, el ángulo aproximado
y el estado: **NIVELADO**, **CORRIGIENDO** o **LÍMITE DE RECORTE**, junto con el
porcentaje aproximado de recorte. La guía se puede ocultar y **nunca** queda
grabada en el vídeo.

En modo **FOTO** el horizonte solo sirve de guía: la fotografía no se rota.
En **HD 120** y **HD 240** el horizonte se desactiva y se explica el motivo.

### 17. Encontrar las fotos y los vídeos

* Fotos: `Pictures/A55 Super Zoom` → `A55_ZOOM_20260830_124455.jpg`
* Vídeos: `Movies/A55 Super Zoom` → `A55_VIDEO_4K30_20260830_124455.mp4`
* Cámara lenta: `A55_SLOWMO_HD240_20260830_124455.mp4`

Aparecen en la Galería y en «Mis archivos». La miniatura de la esquina inferior
izquierda abre el último archivo guardado.

### 18. Qué es realmente el zoom 1000×

El deslizador es **logarítmico** y llega hasta 1000×. Al lado del número grande
se indica de dónde viene el zoom:

* **CÁMARA** — el factor está dentro del rango que la cámara declara y se envía
  a `CameraControl.setZoomRatio()`.
* **DIGITAL EXPERIMENTAL** — el factor supera ese rango. La cámara se fija en su
  máximo real y el resto se consigue **recortando el centro y ampliándolo**.

> El Galaxy A55 no posee teleobjetivo 1000×. Por encima del límite de la cámara,
> la aplicación recorta y amplía la imagen. **No puede recuperar detalles que el
> sensor no capturó.**

No se usa ninguna IA para inventar detalle. En modo foto, con zoom digital
activo, aparece la opción **«Guardar también fotografía original»**, que escribe
además la captura sin recortar (`..._ORIG.jpg`).

En vídeo, el zoom digital pasa por el mismo paso de GPU que el horizonte, así que
lo que se ve en pantalla es lo que se graba. En **alta velocidad** no hay paso de
GPU, y por eso el zoom se limita al máximo real de la cámara.

### 19. Limitaciones de los 50 MP

La aplicación pide la mayor resolución posible con `ResolutionSelector` en modo
`PREFER_HIGHER_RESOLUTION_OVER_CAPTURE_RATE`, consultando tanto
`getOutputSizes(ImageFormat.JPEG)` como `getHighResolutionOutputSizes(...)`.

Aun así, **no se puede garantizar** que un fabricante exponga el modo de sensor
completo a aplicaciones de terceros. Si la vinculación falla, la aplicación baja
a la siguiente resolución compatible, sigue funcionando y avisa de la
degradación. La cifra que se muestra después de guardar sale del archivo, no de
una promesa.

### 20. Limitaciones de 120 y 240 FPS

CameraX no expone sesiones de alta velocidad, así que estos modos usan
**Camera2** directamente: `SessionConfiguration.SESSION_HIGH_SPEED` +
`CameraConstrainedHighSpeedCaptureSession` + `MediaRecorder`.

Solo se habilitan si el teléfono publica el tamaño en
`StreamConfigurationMap.getHighSpeedVideoSizes()`, publica esa cadencia en
`getHighSpeedVideoFpsRangesFor(...)` y existe un codificador que la acepte.

* Una sesión de alta velocidad **no graba audio**. La interfaz lo dice:
  «El audio no está disponible en este modo de alta velocidad.»
* Si 240 FPS solo está disponible para la aplicación propietaria de Samsung, la
  opción aparece desactivada con el texto:
  «240 FPS no está disponible para aplicaciones externas en este dispositivo.»
* No se interpolan fotogramas para simular cámara lenta.

El A55 anuncia oficialmente **4K a 30 FPS como máximo**; por eso no existe
ninguna opción de 4K 60 ni 4K 120 en la aplicación.

### 21. Encontrar el APK

Después de compilar:

```
app/build/outputs/apk/debug/app-debug.apk
```

Para una versión firmada de lanzamiento tendrás que configurar tu propia clave
(este repositorio no contiene claves ni contraseñas):

```
app/build/outputs/apk/release/app-release.apk
```

---

## 3. Pantalla de diagnóstico

**Información** (icono ⓘ del panel superior) → **Diagnóstico**.

Muestra únicamente datos leídos del dispositivo:

* ID lógico de la cámara elegida y nivel de hardware.
* Resolución JPEG máxima y lista de resoluciones de alta resolución.
* Resoluciones de vídeo y rangos de FPS del control de exposición.
* Tamaños y rangos de alta velocidad publicados.
* Zoom mínimo y máximo.
* Flash, estabilización óptica y estabilización de vídeo.
* Sensores físicos expuestos por la cámara lógica.
* Fuente usada para el horizonte (vector de rotación, giroscopio…).
* Resultado de la evaluación de cada modo de vídeo.

El botón **Copiar informe** deja el texto en el portapapeles para poder pegarlo
en un mensaje o en una incidencia.

---

## 4. Qué falta verificar en un teléfono real

Nada de lo siguiente se puede comprobar sin ejecutar la aplicación en un
Galaxy A55 5G físico. La pantalla de diagnóstico está pensada para reportarlo.

- [ ] Acceso real a 50 MP y resolución JPEG máxima realmente obtenida.
- [ ] Grabación 4K 30 sostenida sin cortes.
- [ ] FHD 60 estable.
- [ ] HD 120 y HD 240 mediante sesión de alta velocidad.
- [ ] Comportamiento del audio en modos de alta velocidad.
- [ ] Cámaras físicas expuestas por la cámara lógica.
- [ ] Efecto real del OIS.
- [ ] **Signo y suavidad de la corrección de horizonte** (que gire hacia el lado
      correcto y que el recorte no muestre bordes negros).
- [ ] Temperatura y consumo en grabaciones largas.
- [ ] Calidad nocturna.
- [ ] Estabilidad de MediaStore con archivos grandes.

---

## 5. Arquitectura

```
app/src/main/java/com/flex/cameradev/
├── core/            Lógica pura sin dependencias de Android (con pruebas)
│   ├── ZoomMath.kt              conversión logarítmica, límites, reparto cámara/digital
│   ├── MegapixelMath.kt         megapíxeles y detección de límite del fabricante
│   ├── HorizonMath.kt           ángulos, ±180°, suavizado, filtro complementario
│   ├── CropMath.kt              escala de cobertura, recorte y límite suave
│   ├── FileNaming.kt            nombres y carpetas de MediaStore
│   ├── MediaModels.kt           SizeSpec, VideoMode, catálogo y disponibilidad
│   ├── ResolutionSelection.kt   orden de resoluciones y elección de FPS
│   ├── CameraScoring.kt         elección de la cámara principal sin IDs fijos
│   └── HorizonCompatibility.kt  qué modo de horizonte admite cada modo de vídeo
├── camera/
│   ├── CameraCapabilities.kt    sondeo real de Camera2 + codificadores
│   ├── CameraSelectorManager.kt CameraSelector por ID real
│   ├── CameraCoordinator.kt     único punto que vincula casos de uso
│   ├── PhotoController.kt       ImageCapture y guardado
│   ├── VideoController.kt       Recorder / VideoCapture
│   ├── HighSpeedVideoController.kt  Camera2 alta velocidad + MediaRecorder
│   ├── ZoomController.kt        aplica el reparto de zoom
│   ├── FocusController.kt       enfoque al tocar con corrección de zoom digital
│   └── DiagnosticsReport.kt     texto del informe
├── horizon/
│   ├── SensorFusionManager.kt   IMU en hilo propio
│   ├── HorizonController.kt     convierte el roll en corrección
│   ├── VideoRenderPipeline.kt   CameraEffect (Preview + VideoCapture)
│   └── gl/                      EGL, programa GLSL y SurfaceProcessor
├── media/
│   ├── MediaStoreManager.kt     filas pendientes, publicación y espacio libre
│   ├── ExifUtils.kt             etiquetas EXIF
│   └── DigitalZoomProcessor.kt  recorte por región, sin decodificar 50 MP enteros
└── ui/
    ├── MainActivity.kt          única pantalla de cámara
    ├── CameraViewModel.kt       única fuente de verdad del estado
    ├── CameraUiState.kt         estado de la interfaz
    ├── DiagnosticsActivity.kt / InfoActivity.kt
    └── views/                   ShutterButton, ZoomSlider, LiquidGlassPanel,
                                 HorizonOverlayView, FocusRingView, GridOverlayView
```

Regla central: **una sola fuente de verdad**. Los controladores nunca dibujan;
escriben en `CameraUiState` y la actividad lo representa. Así los controles,
CameraX y las etiquetas no se contradicen.

---

## 6. Estado de verificación

Hay que ser claro sobre lo que se ha comprobado y lo que no.

**Comprobado de verdad**

* Las 97 pruebas unitarias de la lógica pura se ejecutan y pasan.
* Todo el código Kotlin de la aplicación compila: se verificó contra el
  **framework real de Android 15 (API 35)** más declaraciones escritas a mano de
  la superficie de AndroidX/CameraX que usa el proyecto.
* Todas las referencias a recursos (`R.string`, `R.drawable`, `R.id`, `R.color`,
  `R.dimen`, `R.layout`) existen; se generó la clase `R` a partir de los recursos
  reales y el proyecto compila contra ella.
* Todos los XML están bien formados y todas las referencias `@string/`,
  `@color/`, `@drawable/`, `@dimen/`, `@style/`, `@id/` y las clases de vistas
  personalizadas de los layouts resuelven.
* No quedan recursos ni funciones públicas sin usar, ni `TODO`.

**No comprobado**

* **`./gradlew assembleDebug` no se pudo ejecutar** en el entorno donde se
  escribió este proyecto: la política de red bloquea `dl.google.com`, que es el
  único origen del Android Gradle Plugin, de AndroidX/CameraX y del propio
  Android SDK. En un ordenador con acceso normal a Internet, Android Studio
  descargará todo eso y la compilación es el paso 5 de la guía anterior.
* Por la misma razón, **no se compiló contra los artefactos reales de CameraX
  1.4.1**. Las firmas usadas son las documentadas de esa versión, pero la
  primera compilación en tu máquina es la que lo confirma.
* Nada del comportamiento en el teléfono: eso es la lista de la sección 4.

## 7. Pruebas

```bash
./gradlew testDebugUnitTest
```

97 pruebas JVM cubren la lógica pura: conversión logarítmica del zoom, límites,
megapíxeles, selección de resolución y FPS, continuidad angular en ±180°, el
filtro del horizonte, el cálculo de recorte, los nombres de archivo y el
comportamiento ante valores inválidos (NaN, infinito, listas vacías, tamaños
degenerados).

---

## 8. Configuración técnica

| Elemento              | Valor                    |
|-----------------------|--------------------------|
| Lenguaje              | Kotlin 2.0.21            |
| Gradle                | 8.9 (wrapper incluido)   |
| Android Gradle Plugin | 8.7.3                    |
| compileSdk / targetSdk| 35                       |
| minSdk                | 29 (Android 10)          |
| JDK                   | 17                       |
| CameraX               | 1.4.1                    |
| Package               | `com.flex.cameradev`     |
| Orientación           | Vertical                 |
| Tema                  | Oscuro                   |

Sin servicios externos, sin claves de API, sin APIs privadas de Samsung y sin
descargas de imágenes protegidas.

---

## 9. Accesibilidad

* Áreas táctiles de al menos 48 dp.
* `contentDescription` en todos los controles.
* Textos centralizados en `res/values/strings.xml`.
* La información nunca depende solo del color: los estados llevan texto.
* Los avisos se anuncian a TalkBack una vez, no en cada cambio de zoom o del IMU.
* Se respeta la escala de animaciones del sistema; con animaciones desactivadas,
  las transiciones se aplican de inmediato.
