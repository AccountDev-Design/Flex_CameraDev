# Pruebas

## `test_horizon.cpp` — matemática del Horizon Lock

Compila e incluye **el mismo `fc_horizon.h`** que se enlaza en el firmware,
así que no es una reimplementación: es el código real.

```bash
cd firmware/tools
./run_tests.sh
```

Sólo necesita `g++`. Comprueba, entre otras cosas:

* que con la cámara nivelada el horizonte vale 0 y la gravedad cae por «abajo»;
* los ángulos exigidos 0°, 45°, 90°, 179°, 180°, 181°, 270°, 360° y 720°;
* que el contenido acaba **vertical en pantalla** en todos ellos
  (`horizonte + compensación = 0`), que es lo que de verdad importa;
* seguimiento continuo de 0° → +720° → −720° sin ningún salto mayor que el
  paso real de muestreo;
* el cruce de ±180° en los dos sentidos;
* que la compensación deshace exactamente la inclinación del contenido;
* que apuntando al cenit o al nadir la confianza cae a 0 y el ángulo se
  congela sin derivar ni un microgrado;
* que al recuperar la referencia se desliza en vez de saltar;
* montaje 0/90/180/270, inversión de sentido y planos de montaje alternativos;
* zona muerta y tiempo de respuesta ante un giro real.

## Pruebas de la interfaz web

Están en el historial de desarrollo y necesitan Node más `playwright-core` y
un Chromium. Sirven un backend simulado que devuelve fotogramas JPEG con una
barra blanca dibujada con la inclinación que tendría en el sensor, y después
**miden en los píxeles del canvas** que la barra queda vertical. También
comprueban el JPEG estabilizado exportado y el archivo de vídeo grabado.
