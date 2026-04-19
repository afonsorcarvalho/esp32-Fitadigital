/**
 * @file ui_app.h
 * @brief Arranque da UI LVGL: ecra Wi-Fi, principal (barra + explorador), definicoes.
 */
#pragma once

#include <stdbool.h>

/**
 * @param sd_mounted true se SD.begin teve sucesso antes de LVGL.
 * @param splash_active true se o splash de boot está ativo (destrói splash em vez do boot screen).
 * @note Deve ser chamado com lvgl_port_lock ativo (como no setup atual).
 */
void ui_app_run(bool sd_mounted, bool splash_active = false);

/**
 * Abre o .txt de ciclos do dia se existir (file_browser). Mutex LVGL deve estar detido.
 * Chamar apos ui_app_run no setup (apos um unlock/relock) ou apos ensure_main_content_browser.
 */
/** Abre manualmente o .txt de CICLOS do dia (se existir). O arranque nao chama isto automaticamente. */
void ui_app_open_cycles_txt_if_exists(void);
