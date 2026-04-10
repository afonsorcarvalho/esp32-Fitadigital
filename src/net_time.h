/**
 * @file net_time.h
 * @brief NTP (configTime) e hora local com offset NVS; hora manual UTC (settimeofday).
 * @note Documentacao Arduino ESP32: configTime / getLocalTime (extensao).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

void net_time_init(void);

/**
 * Tenta carregar a hora do RTC externo (UTC) para o relogio do sistema.
 * Chamar cedo no arranque (antes de montar SD) para que o FAT grave
 * timestamps de criacao/modificacao com hora valida.
 */
void net_time_bootstrap_from_rtc_early(void);

/** Aplicar servidor NTP e offset (chamar apos gravar NVS ou no arranque). */
void net_time_apply_settings(void);

/**
 * Chamado quando Wi-Fi fica ligado: inicia SNTP se NTP ativo.
 * Idempotente com estado interno.
 */
void net_time_on_wifi_connected(void);

/** Periodico (ex.: net_services_loop): re-sincronizacao opcional. */
void net_time_loop(void);

/** @return true quando o relogio do sistema esta plausivel (>= 2020-01-01 UTC). */
bool net_time_is_valid(void);

/**
 * Le epoch UTC diretamente do RTC externo.
 * @return true quando o RTC respondeu e devolveu data plausivel.
 */
bool net_time_get_rtc_epoch_utc(time_t *out_ts);

/**
 * Sonda o RTC por I2C.
 * @return true se o RTC respondeu no barramento.
 * @note rtc_has_valid_time indica se o RTC tambem tinha hora plausivel.
 */
bool net_time_probe_rtc(bool *rtc_has_valid_time);

/**
 * Sincronizacao imediata (bloqueia ate ~3 s). Mensagem curta para UI.
 * @return true se getLocalTime teve sucesso apos configTime.
 */
bool net_time_sync_now_blocking(char *msg, size_t msg_len);

/**
 * Tenta escrever a hora atual do sistema no RTC.
 */
void net_time_push_system_time_to_rtc(const char *reason);

/**
 * Define relogio como UTC (campos validos 2000-2099).
 * @return false se data invalida; err_msg preenchido.
 */
bool net_time_set_manual_utc(int year, int mon, int day, int hour, int min, char *err_msg, size_t err_len);

/**
 * Formatar data/hora para a barra, priorizando RTC (com offset configurado).
 * Em fallback, usa relogio local do sistema.
 */
void net_time_format_status_line(char *buf, size_t buf_len);
