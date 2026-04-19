/**
 * @file ui_loading.cpp
 * @brief Overlay de espera (LVGL spinner + texto).
 */
#include "ui_loading.h"

#include <string.h>

#if LV_USE_SPINNER
extern "C" {
lv_obj_t *lv_spinner_create(lv_obj_t *parent, uint32_t time, uint32_t arc_length);
}
#endif

static lv_obj_t *s_loading_root = nullptr;

void ui_loading_hide(void) {
  if (s_loading_root != nullptr) {
    lv_obj_del(s_loading_root);
    s_loading_root = nullptr;
  }
}

bool ui_loading_is_visible(void) { return s_loading_root != nullptr; }

void ui_loading_flush_display(void) {
  lv_disp_t *d = lv_disp_get_default();
  if (d != nullptr) {
    lv_refr_now(d);
  }
}

void ui_loading_show(lv_obj_t *parent, const char *message) {
  ui_loading_hide();

  lv_obj_t *p = parent;
  if (p == nullptr) {
    p = lv_layer_top();
  }

  s_loading_root = lv_obj_create(p);
  lv_obj_set_size(s_loading_root, LV_PCT(100), LV_PCT(100));
  lv_obj_align(s_loading_root, LV_ALIGN_TOP_LEFT, 0, 0);
  /** Cobre o flex sem ser empurrado: filho de s_root em LV_LAYOUT_FLEX cobria mal o ecra. */
  lv_obj_add_flag(s_loading_root, LV_OBJ_FLAG_FLOATING);
  lv_obj_move_foreground(s_loading_root);
  lv_obj_set_style_bg_color(s_loading_root, lv_color_hex(0x202020), 0);
  lv_obj_set_style_bg_opa(s_loading_root, LV_OPA_80, 0);
  lv_obj_clear_flag(s_loading_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_loading_root, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_set_layout(s_loading_root, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_loading_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_loading_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_loading_root, 16, 0);

#if LV_USE_SPINNER
  lv_obj_t *sp = lv_spinner_create(s_loading_root, 1200, 90);
  lv_obj_set_size(sp, 64, 64);
#else
  lv_obj_t *sp = lv_label_create(s_loading_root);
  lv_label_set_text(sp, "...");
#endif

  lv_obj_t *lbl = lv_label_create(s_loading_root);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, LV_PCT(85));
  lv_label_set_text(lbl, message != nullptr ? message : "Aguardando...");
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);

  (void)sp;
}
