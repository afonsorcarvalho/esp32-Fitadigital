/**
 * @file ui_toast.cpp
 * @brief Implementacao de ui_toast.h — single instance, fade in/out + auto-hide.
 */
#include "ui_toast.h"

#include <lvgl.h>
#include "ui_theme.h"

namespace {

lv_obj_t *s_toast = nullptr;
lv_obj_t *s_toast_lbl = nullptr;
lv_timer_t *s_hide_timer = nullptr;

void toast_destroy(void) {
  if (s_hide_timer != nullptr) {
    lv_timer_del(s_hide_timer);
    s_hide_timer = nullptr;
  }
  if (s_toast != nullptr) {
    lv_anim_del(s_toast, nullptr);
    lv_obj_del(s_toast);
    s_toast = nullptr;
    s_toast_lbl = nullptr;
  }
}

void toast_opa_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_opa(static_cast<lv_obj_t *>(var), static_cast<lv_opa_t>(v), 0);
}

void toast_fade_out_ready_cb(lv_anim_t *a) {
  (void)a;
  toast_destroy();
}

void toast_hide_timer_cb(lv_timer_t *t) {
  (void)t;
  if (s_hide_timer != nullptr) {
    lv_timer_del(s_hide_timer);
    s_hide_timer = nullptr;
  }
  if (s_toast == nullptr) {
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_toast);
  lv_anim_set_exec_cb(&a, toast_opa_anim_cb);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_time(&a, 220);
  lv_anim_set_ready_cb(&a, toast_fade_out_ready_cb);
  lv_anim_start(&a);
}

} // namespace

void ui_toast_show(ToastKind kind, const char *message, uint32_t duration_ms) {
  if (message == nullptr) {
    return;
  }
  if (s_toast != nullptr) {
    /* Substitui toast existente: cancela timer + anim para reaproveitar o objeto. */
    if (s_hide_timer != nullptr) {
      lv_timer_del(s_hide_timer);
      s_hide_timer = nullptr;
    }
    lv_anim_del(s_toast, nullptr);
  } else {
    s_toast = lv_obj_create(lv_layer_top());
    if (s_toast == nullptr) {
      return;
    }
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(s_toast, 10, 0);
    lv_obj_set_style_pad_hor(s_toast, 18, 0);
    lv_obj_set_style_pad_ver(s_toast, 10, 0);
    lv_obj_set_style_border_width(s_toast, 0, 0);
    lv_obj_set_style_shadow_width(s_toast, 14, 0);
    lv_obj_set_style_shadow_color(s_toast, UI_COLOR_BLACK, 0);
    lv_obj_set_style_shadow_opa(s_toast, LV_OPA_30, 0);
    s_toast_lbl = lv_label_create(s_toast);
    lv_obj_center(s_toast_lbl);
  }

  uint32_t bg = 0x606060u; /* Info */
  switch (kind) {
    case ToastKind::Success:
      bg = 0x449D48u; /* UI_COLOR_PRIMARY */
      break;
    case ToastKind::Error:
      bg = 0xC62828u; /* UI_COLOR_ERROR_BG */
      break;
    case ToastKind::Warn:
      bg = 0xF5B841u; /* UI_COLOR_WARN_AMBER */
      break;
    case ToastKind::Info:
    default:
      break;
  }
  lv_obj_set_style_bg_color(s_toast, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
  if (s_toast_lbl != nullptr) {
    lv_obj_set_style_text_color(s_toast_lbl, UI_COLOR_WHITE, 0);
    lv_label_set_text(s_toast_lbl, message);
  }
  lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(s_toast, LV_ALIGN_BOTTOM_RIGHT, -16, -16);

  /* Fade-in 180 ms. */
  lv_obj_set_style_opa(s_toast, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_toast);
  lv_anim_set_exec_cb(&a, toast_opa_anim_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_time(&a, 180);
  lv_anim_start(&a);

  s_hide_timer = lv_timer_create(toast_hide_timer_cb, duration_ms, nullptr);
  lv_timer_set_repeat_count(s_hide_timer, 1);
}
