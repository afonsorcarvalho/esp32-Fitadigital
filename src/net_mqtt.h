/**
 * @file net_mqtt.h
 * @brief Cliente MQTT assincrono — telemetria de saude e publicacao de alertas.
 *
 * API usada por:
 *  - app.cpp: net_mqtt_init() no boot;
 *  - net_mqtt_keywords.cpp: net_mqtt_publish() ao detetar match;
 *  - ui_app.cpp: net_mqtt_status() / net_mqtt_last_error() para label de estado.
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
 * Inicializa o modulo MQTT e cria a task mqtt_svc.
 * Idempotente — seguro chamar multiplas vezes.
 */
void net_mqtt_init(void);

/**
 * Aplica novas settings (chamada da UI apos o utilizador alterar config).
 * Desliga a ligacao actual e retoma com os novos parametros imediatamente.
 */
void net_mqtt_apply_settings(void);

/** Estado actual da ligacao MQTT. */
MqttStatus net_mqtt_status(void);

/**
 * Ultimo erro de ligacao (string estatica, valida ate proxima chamada).
 * Retorna "" se sem erro.
 */
const char *net_mqtt_last_error(void);

/**
 * Publica um payload num sub-topico (ex.: "/keyword").
 * O topico completo e' construido como base_topic + topic_suffix.
 * @return true se publicado (false se desligado ou suspenso).
 */
bool net_mqtt_publish(const char *topic_suffix, const char *payload, bool retain = false);

/**
 * Suspende/retoma o cliente MQTT.
 * Deve ser chamado com true durante OTA/formatacao SD, da mesma forma
 * que net_services_set_ftp_suspended().
 * Mante a ligacao TCP activa; apenas pausa publishes.
 */
void net_mqtt_set_suspended(bool suspended);
