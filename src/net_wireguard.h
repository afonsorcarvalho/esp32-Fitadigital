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
