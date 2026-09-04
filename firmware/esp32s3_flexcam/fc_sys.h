// =====================================================================
//  fc_sys.h — arranque del Wi-Fi en modo AP y volcados de diagnóstico.
//  Va en un .cpp aparte a propósito: el Arduino IDE genera prototipos
//  automáticos para las funciones sueltas del .ino y con funciones
//  'static' eso produce declaraciones contradictorias. Dejando el .ino
//  con sólo setup() y loop() ese problema no puede aparecer.
// =====================================================================
#pragma once
#include <Arduino.h>

void fcSysBanner();
void fcSysMemoria(const char* cuando);
bool fcSysWifiAP();
