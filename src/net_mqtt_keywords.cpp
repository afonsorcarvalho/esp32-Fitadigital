/**
 * @file net_mqtt_keywords.cpp
 * @brief Fase 1 stub — sem task real. Implementacao completa na Fase 4.
 */
#include "net_mqtt_keywords.h"
#include "app_settings.h"

#include <esp_log.h>

static const char *TAG = "mqtt_kw";

void net_mqtt_keywords_start(void) {
    /* Fase 1: apenas log; sem task. */
    const String kw = app_settings_mqtt_keywords();
    ESP_LOGI(TAG, "keywords stub: palavras='%s' (task inativa — Fase 4)", kw.c_str());
}

void net_mqtt_keywords_apply_settings(void) {
    /* Fase 1: no-op. */
}
