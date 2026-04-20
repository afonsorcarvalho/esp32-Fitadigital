/**
 * @file ui_toast.h
 * @brief Notificacao transitoria (toast) reutilizavel para feedback de accoes.
 *
 * Canto inferior-direito, sobre todas as telas (lv_layer_top). Apenas um toast
 * visivel por vez: chamadas sucessivas substituem o anterior.
 */
#pragma once

#include <stdint.h>

enum class ToastKind : uint8_t {
  Info    = 0,
  Success = 1,
  Warn    = 2,
  Error   = 3,
};

/**
 * Mostra um toast com a mensagem dada pelo tempo indicado (default 2500 ms).
 * Chamar com mutex LVGL detido (contexto habitual dos callbacks LVGL).
 */
void ui_toast_show(ToastKind kind, const char *message, uint32_t duration_ms = 2500);
