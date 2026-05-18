/**
 * @file net_wireguard.h
 * @brief WireGuard via biblioteca ciniml/WireGuard-ESP32 (README do repositorio).
 */
#pragma once

#include <cstddef>

void net_wireguard_init(void);

/**
 * Para o tunel e, se Wi-Fi ligado e opcoes NVS validas, inicia de novo.
 * Chamar apos gravar definicoes WireGuard na UI.
 */
void net_wireguard_apply(void);

/** Suspende o watchdog (probe ping + 1h re-apply) para libertar ~10K internal
 *  heap. Chamar antes de operacoes com pressao de heap (ex: wg_provision_start
 *  precisa 6K stack para mbedTLS). */
void net_wireguard_pause_watchdog(void);

/** Retoma watchdog (recria probe session se WG enabled + Wi-Fi up). */
void net_wireguard_resume_watchdog(void);

/**
 * Snapshot estado WG p/ telemetria HTTP (/api/wg/status).
 * Escreve JSON em `out` (max `out_sz` bytes). Devolve bytes escritos
 * (sem terminator). 0 = erro buffer.
 *
 * Campos: active, peer_up, ever_up, ever_rx, last_up_ms_ago, last_rx_ms_ago,
 * last_apply_ms_ago, reapply_count, wifi_soft_count, wifi_hard_count, uptime_ms.
 */
size_t net_wireguard_status_json(char *out, size_t out_sz);
