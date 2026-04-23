/**
 * @file boot_screen.cpp
 * @brief Ecrã de arranque estilo terminal: cada linha é flex row — rótulo | pontos (clip) | [OK]/[ERROR].
 *
 * Evita `LV_LABEL_LONG_WRAP` numa única etiqueta longa (a etiqueta de estado passava para a linha de baixo).
 * Usa `lv_font_unscii_16` quando disponível (doc LVGL: fonte monoespaçada).
 */

#include "boot_screen.h"
#include "ui_theme.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_port_v8.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

lv_obj_t *s_scr = nullptr;
lv_obj_t *s_subtitle = nullptr;
/** Etiqueta só com o estado à direita ([--], [OK], ...). */
lv_obj_t *s_step_tag_lbl[BOOT_STEP_COUNT] = {};
lv_obj_t *s_footer = nullptr;

static const char *k_step_left[BOOT_STEP_COUNT] = {
    "1 SD Card",
    "2 Real Time Clock",
    "3 WiFi",
    "4 NTP",
    "5 FTP Server",
    "6 WireGuard",
    "7 HTTP Server",
};

/** Pontos para preencher o espaço central; o clip corta o excesso à direita. */
static const char k_dots_fill[] =
    "................................................................"
    "................................................................";

static void boot_screen_clear_internal(void) {
  if (s_scr != nullptr) {
    lv_obj_del(s_scr);
    s_scr = nullptr;
    s_subtitle = nullptr;
    s_footer = nullptr;
    for (int i = 0; i < (int)BOOT_STEP_COUNT; ++i) {
      s_step_tag_lbl[i] = nullptr;
    }
  }
}

/**
 * Não chamar `lv_refr_now` com `lvgl_port_lock` detido: em painéis RGB pode bloquear em VSYNC
 * e nunca libertar o mutex; as chamadas seguintes (subtítulo, passos [OK]) ficam à espera para sempre.
 * A tarefa LVGL corre `lv_timer_handler` noutro contexto; um pequeno delay cede CPU ao desenho.
 */
static void boot_yield_lvgl_task(void) {
  vTaskDelay(pdMS_TO_TICKS(3));
}

static const lv_font_t *boot_font(void) {
#if LV_FONT_UNSCII_16
  return &lv_font_unscii_16;
#else
  return LV_FONT_DEFAULT;
#endif
}

static const char *level_to_tag(const char *level) {
  if (level != nullptr && strcmp(level, "ERROR") == 0) {
    return "[ERROR]";
  }
  if (level != nullptr && strcmp(level, "WARN") == 0) {
    return "[WARN]";
  }
  return "[OK]";
}

static void apply_tag_color(lv_obj_t *tag_lbl, const char *level) {
  if (tag_lbl == nullptr) {
    return;
  }
  lv_color_t c = UI_COLOR_BOOT_STEP_OK;
  if (level != nullptr && strcmp(level, "ERROR") == 0) {
    c = UI_COLOR_BOOT_STEP_ERROR;
  } else if (level != nullptr && strcmp(level, "WARN") == 0) {
    c = UI_COLOR_BOOT_STEP_WARN;
  } else if (level == nullptr) {
    c = UI_COLOR_BOOT_STEP_IDLE;
  }
  lv_obj_set_style_text_color(tag_lbl, c, LV_PART_MAIN);
}

}  // namespace

void boot_screen_show(void) {
  if (!lvgl_port_lock(-1)) {
    return;
  }
  boot_screen_clear_internal();

  s_scr = lv_obj_create(nullptr);
  lv_obj_set_size(s_scr, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_scr, UI_COLOR_BOOT_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_scr, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_scr, 8, LV_PART_MAIN);
  lv_obj_set_flex_flow(s_scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(s_scr, 4, LV_PART_MAIN);
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(s_scr);
  lv_label_set_text(title, "FitaDigital - arranque");
  lv_obj_set_style_text_color(title, UI_COLOR_BOOT_TITLE, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, boot_font(), LV_PART_MAIN);

  s_subtitle = lv_label_create(s_scr);
  lv_obj_set_width(s_subtitle, LV_PCT(100));
  lv_label_set_long_mode(s_subtitle, LV_LABEL_LONG_WRAP);
  lv_label_set_text(s_subtitle, "A inicializar painel...");
  lv_obj_set_style_text_color(s_subtitle, UI_COLOR_BOOT_SUBTITLE, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_subtitle, boot_font(), LV_PART_MAIN);

  for (int i = 0; i < (int)BOOT_STEP_COUNT; ++i) {
    lv_obj_t *row = lv_obj_create(s_scr);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = lv_label_create(row);
    lv_label_set_text(left, k_step_left[i]);
    lv_obj_set_style_text_color(left, UI_COLOR_BOOT_STEP_OK, LV_PART_MAIN);
    lv_obj_set_style_text_font(left, boot_font(), LV_PART_MAIN);

    lv_obj_t *dots = lv_label_create(row);
    lv_label_set_text(dots, k_dots_fill);
    lv_label_set_long_mode(dots, LV_LABEL_LONG_CLIP);
    lv_obj_set_flex_grow(dots, 1);
    lv_obj_set_style_text_color(dots, UI_COLOR_BOOT_DOTS, LV_PART_MAIN);
    lv_obj_set_style_text_font(dots, boot_font(), LV_PART_MAIN);

    lv_obj_t *tag = lv_label_create(row);
    lv_label_set_text(tag, "[--]");
    apply_tag_color(tag, nullptr);
    lv_obj_set_style_text_font(tag, boot_font(), LV_PART_MAIN);
    s_step_tag_lbl[i] = tag;
  }

  s_footer = lv_label_create(s_scr);
  lv_obj_set_width(s_footer, LV_PCT(100));
  lv_label_set_long_mode(s_footer, LV_LABEL_LONG_WRAP);
  lv_label_set_text(s_footer, "");
  lv_obj_set_style_text_color(s_footer, UI_COLOR_BOOT_FOOTER, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_footer, boot_font(), LV_PART_MAIN);

  lv_scr_load(s_scr);
  (void)lvgl_port_unlock();
  boot_yield_lvgl_task();
}

void boot_screen_set_step(boot_step_t step, const char *level) {
  if (!lvgl_port_lock(-1)) {
    return;
  }
  if ((unsigned)step >= BOOT_STEP_COUNT || s_step_tag_lbl[step] == nullptr) {
    (void)lvgl_port_unlock();
    return;
  }
  const char *tag = level_to_tag(level);
  lv_label_set_text(s_step_tag_lbl[step], tag);
  apply_tag_color(s_step_tag_lbl[step], level);
  (void)lvgl_port_unlock();
  boot_yield_lvgl_task();
}

void boot_screen_set_subtitle(const char *msg) {
  if (!lvgl_port_lock(-1)) {
    return;
  }
  if (s_subtitle != nullptr) {
    lv_label_set_text(s_subtitle, (msg != nullptr && msg[0] != '\0') ? msg : " ");
  }
  (void)lvgl_port_unlock();
  boot_yield_lvgl_task();
}

void boot_screen_set_footer(const char *msg) {
  if (!lvgl_port_lock(-1)) {
    return;
  }
  if (s_footer != nullptr) {
    lv_label_set_text(s_footer, (msg != nullptr) ? msg : "");
  }
  (void)lvgl_port_unlock();
  boot_yield_lvgl_task();
}

void boot_screen_destroy(void) {
  if (!lvgl_port_lock(-1)) {
    return;
  }
  boot_screen_clear_internal();
  (void)lvgl_port_unlock();
}
