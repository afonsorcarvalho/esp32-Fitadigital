/**
 * @file net_mqtt_keywords.h
 * @brief Detecao de palavras-chave RS485 e publicacao MQTT.
 *
 * Task FreeRTOS `mqtt_kw` (core 1, prio 1, stack 3072 B) corre a cada 5 s.
 * Em cada tick le as linhas novas do ficheiro do dia via sd_access_sync e
 * faz match case-insensitive contra a lista configurada em NVS (mq_kw, sep. ';').
 *
 * AVISO: por iteracao sao processadas no maximo as ultimas 10 linhas,
 * independentemente de quantas chegaram. As mais antigas sao descartadas.
 * Este limite e' intencional para evitar picos de heap e latencia na task.
 */
#pragma once

/**
 * Inicia a task de detecao de palavras-chave.
 * Idempotente — seguro chamar multiplas vezes.
 * Chamado em app.cpp apos net_mqtt_init().
 */
void net_mqtt_keywords_start(void);

/**
 * Aplica novas settings (lista de palavras-chave alterada via UI).
 * No-op: a task le app_settings_mqtt_keywords() em cada tick.
 */
void net_mqtt_keywords_apply_settings(void);
