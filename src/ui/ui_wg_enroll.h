/**
 * @file ui_wg_enroll.h
 * @brief Modal LVGL para provisionamento WireGuard via QR code.
 */
#pragma once

/**
 * Abre o modal de enrollment e inicia a state machine de provisionamento.
 * server_url: URL base do backend, ex. "http://192.168.1.10:5000".
 * Ignorado se ja houver um modal aberto.
 */
void ui_wg_enroll_open(const char *server_url);

/** Fecha o modal e cancela o provisionamento em curso (se houver). */
void ui_wg_enroll_close(void);
