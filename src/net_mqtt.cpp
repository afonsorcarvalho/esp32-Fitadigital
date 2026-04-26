/**
 * @file net_mqtt.cpp
 * @brief Fase 1 stub — settings NVS lidas, sem ligacao real ao broker.
 * Implementacao de rede real na Fase 3.
 */
#include "net_mqtt.h"
#include "app_settings.h"

#include <Arduino.h>
#include <esp_log.h>

static const char *TAG = "net_mqtt";

void net_mqtt_init(void) {
    if (!app_settings_mqtt_enabled()) {
        ESP_LOGI(TAG, "MQTT desativado nas settings (mq_on=false)");
        return;
    }
    /* Fase 1: apenas log; sem task nem ligacao. */
    ESP_LOGI(TAG, "MQTT init stub: host=%s port=%u topic=%s iv=%us",
             app_settings_mqtt_host().c_str(),
             (unsigned)app_settings_mqtt_port(),
             app_settings_mqtt_base_topic().c_str(),
             (unsigned)app_settings_mqtt_telemetry_interval_s());
}

void net_mqtt_apply_settings(void) {
    /* Fase 1: no-op. */
}

MqttStatus net_mqtt_status(void) {
    return MqttStatus::Disabled;
}

const char *net_mqtt_last_error(void) {
    return "";
}

bool net_mqtt_publish(const char *topic_suffix, const char *payload, bool retain) {
    (void)topic_suffix;
    (void)payload;
    (void)retain;
    return false;
}

void net_mqtt_set_suspended(bool suspended) {
    (void)suspended;
}
