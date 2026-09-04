// =====================================================================
//  fc_sys.cpp
// =====================================================================
#include "fc_sys.h"
#include "fc_config.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>

void fcSysBanner() {
  Serial.println();
  Serial.println(F("======================================================"));
  Serial.println(F("  FlexCam S26 - ESP32-S3 + OV5640 + BNO085"));
  Serial.println(F("======================================================"));
  Serial.printf("[SYS] Chip %s rev%u  %u nucleos  CPU %u MHz  Flash %u MB\n",
                ESP.getChipModel(), (unsigned)ESP.getChipRevision(),
                (unsigned)ESP.getChipCores(), (unsigned)getCpuFrequencyMhz(),
                (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  if (psramFound()) {
    Serial.printf("[SYS] PSRAM detectada: %u KB\n", (unsigned)(ESP.getPsramSize() / 1024));
  } else {
    Serial.println(F("[SYS] SIN PSRAM. Selecciona 'PSRAM: OPI PSRAM' en el IDE "
                     "o el OV5640 no pasara de resoluciones bajas."));
  }
}

void fcSysMemoria(const char* cuando) {
  Serial.printf("[MEM] %s  DRAM libre=%u KB (mayor bloque %u KB)  PSRAM libre=%u KB de %u KB\n",
    cuando,
    (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
    (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
    (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
    (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024));
}

bool fcSysWifiAP() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  IPAddress ip(FC_AP_IP_1, FC_AP_IP_2, FC_AP_IP_3, FC_AP_IP_4);
  IPAddress mask(255, 255, 255, 0);
  if (!WiFi.softAPConfig(ip, ip, mask)) {
    Serial.println(F("[AP] softAPConfig fallo."));
  }
  if (!WiFi.softAP(FC_AP_SSID, FC_AP_PASSWORD, FC_AP_CHANNEL, 0, FC_AP_MAX_CLIENTS)) {
    Serial.println(F("[AP] No se pudo crear el punto de acceso."));
    return false;
  }
  // Sin ahorro de energia: es lo que mas baja la latencia del MJPEG.
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_protocol(WIFI_IF_AP,
      WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);

  Serial.printf("[AP] SSID=%s  canal=%d  IP=%s\n",
                FC_AP_SSID, FC_AP_CHANNEL, WiFi.softAPIP().toString().c_str());
  return true;
}
