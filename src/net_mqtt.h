/**
 * @file net_mqtt.h
 * @brief Cliente MQTT — telemetria de saude e publicacao de alertas.
 *
 * Fase 1: stub sem ligacao real. A implementacao de rede real e' adicionada
 * na Fase 3 (ver C:\Users\Afonso\.claude\plans\mqtt-telemetria-keywords.md).
 *
 * API desenhada para ser usada por:
 *  - app.cpp: chama net_mqtt_init() no boot;
 *  - net_mqtt_keywords.cpp: chama net_mqtt_publish() ao detetar match;
 *  - ui_app.cpp (Fase 2): le net_mqtt_status() para mostrar estado no ecra.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

enum class MqttStatus : uint8_t {
    Disabled,
    Connecting,
    Connected,
    Error,
};

/**
 * Inicializa o modulo MQTT a partir das settings NVS.
 * Idempotente — seguro chamar multiplas vezes.
 * Fase 1: log apenas; nao cria task nem liga ao broker.
 */
void net_mqtt_init(void);

/**
 * Aplica novas settings (chamada da UI apos o utilizador alterar config).
 * Fase 1: no-op stub.
 */
void net_mqtt_apply_settings(void);

/** Estado actual da ligacao MQTT (Fase 1: sempre Disabled). */
MqttStatus net_mqtt_status(void);

/**
 * Ultimo erro de ligacao (string estatica, valida ate proxima chamada).
 * Fase 1: retorna "".
 */
const char *net_mqtt_last_error(void);

/**
 * Publica um payload num sub-topico (ex.: "/status", "/keyword").
 * O topico completo e' construido como base_topic + topic_suffix.
 * @return true se publicado (Fase 1: false sempre).
 */
bool net_mqtt_publish(const char *topic_suffix, const char *payload, bool retain = false);

/**
 * Suspende/retoma o cliente MQTT.
 * Deve ser chamado com true durante OTA/formatacao SD, da mesma forma
 * que net_services_set_ftp_suspended().
 * Fase 1: no-op stub.
 */
void net_mqtt_set_suspended(bool suspended);
