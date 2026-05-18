/**
 * @file net_wg_probe.h
 * @brief Sondagem ICMP periodica pelo tunel WG. 3 pings a cada 30s para
 *        10.0.0.1 (server WG). Logs vao para app_log + Serial.
 *
 * Objectivo: provar que o tunel se mantem bidireccional (handshake nao basta;
 * RX/TX data tem de fluir). Cada timeout indica que o tunel "morreu silencioso".
 *
 * Activado em v1.92 apos fix wg_mask /32 -> /24.
 */
#pragma once

void net_wg_probe_init(void);
