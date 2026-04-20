/**
 * @file net_monitor.h
 * @brief Monitorizacao de conectividade remota por ping ICMP.
 *
 * Task periodica (default 60 s) que envia um echo ICMP ao IP/hostname guardado
 * em `app_settings_monitor_ip()` e publica o resultado numa flag atomica para a
 * UI consumir. Desligado (Unknown) quando a configuracao esta vazia ou o Wi-Fi
 * nao esta ligado.
 */
#pragma once

#include <stdint.h>

enum class NetMonitorStatus : uint8_t {
  Disabled = 0, /**< IP nao configurado ou Wi-Fi em baixo: nao ha medicao. */
  Pending  = 1, /**< Configurado, a aguardar o primeiro resultado. */
  Ok       = 2, /**< Ultimo ping respondeu dentro do timeout. */
  Fail     = 3, /**< Ultimo ping falhou (timeout, DNS, sem rota). */
};

/** Arranca a task. Seguro chamar antes do Wi-Fi estar ligado. */
void net_monitor_init(void);

/** Re-le a configuracao (chamar apos `app_settings_set_monitor_ip`). */
void net_monitor_apply_settings(void);

/** Estado mais recente observado pela task. */
NetMonitorStatus net_monitor_status(void);

/** Latencia do ultimo ping bem-sucedido em ms; 0 se nunca teve sucesso. */
uint32_t net_monitor_last_latency_ms(void);

/** millis() do ultimo resultado (qualquer estado). 0 se nunca correu. */
uint32_t net_monitor_last_check_ms(void);
