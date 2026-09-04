// =====================================================================
//  fc_server.h — dos servidores HTTP separados:
//    :80  interfaz web + API de control + WebSocket de telemetría
//    :81  sólo MJPEG, para que un cliente lento nunca frene la interfaz
// =====================================================================
#pragma once
#include <Arduino.h>

bool fcServerBegin();
void fcServerStop();
uint32_t fcServerWsClients();
