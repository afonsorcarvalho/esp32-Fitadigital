/**
 * @file net_mqtt_keywords.h
 * @brief Detecao de palavras-chave RS485 e publicacao MQTT.
 *
 * Fase 1: stubs sem task real. A implementacao completa e' adicionada
 * na Fase 4 (ver C:\Users\Afonso\.claude\plans\mqtt-telemetria-keywords.md).
 *
 * AVISO (implementacao Fase 4): por iteracao sao processadas no maximo as
 * ultimas 10 linhas do ficheiro do dia, independentemente de quantas chegaram.
 * Se chegarem 50 linhas num intervalo de 5 s, as primeiras 40 sao descartadas.
 * Este limite e' intencional para evitar picos de heap e latencia na task.
 */
#pragma once

/**
 * Inicia a task de detecao de palavras-chave (Fase 1: stub, nao cria task).
 * Chamado em app.cpp apos net_mqtt_init().
 */
void net_mqtt_keywords_start(void);

/**
 * Aplica novas settings (lista de palavras-chave alterada via UI).
 * Fase 1: no-op stub.
 */
void net_mqtt_keywords_apply_settings(void);
