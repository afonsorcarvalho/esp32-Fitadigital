/**
 * @file ui_app.h
 * @brief Arranque da UI LVGL: ecra Wi-Fi, principal (barra + explorador), definicoes.
 */
#pragma once

#include <stdbool.h>

/**
 * @param sd_mounted true se SD.begin teve sucesso antes de LVGL.
 * @note Deve ser chamado com lvgl_port_lock ativo (como no setup atual).
 */
void ui_app_run(bool sd_mounted);
