/**
 * @file net_wireguard.h
 * @brief WireGuard via biblioteca ciniml/WireGuard-ESP32 (README do repositorio).
 */
#pragma once

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
