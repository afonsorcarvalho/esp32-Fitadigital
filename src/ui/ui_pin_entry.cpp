#include "ui_pin_entry.h"
#include "app_settings.h"

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>

namespace {

lv_obj_t          *s_bg  = nullptr;
lv_obj_t          *s_r[4] = {};
ui_pin_result_cb_t s_cb  = nullptr;

static const char *kDigits = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9";

void modal_close(void) {
  if (s_bg != nullptr) {
    lv_obj_del(s_bg);
    s_bg = nullptr;
    s_r[0] = s_r[1] = s_r[2] = s_r[3] = nullptr;
  }
}

void cancel_cb(lv_event_t * /*e*/) {
  modal_close();
  if (s_cb) { s_cb(false); s_cb = nullptr; }
}

void bg_click_cb(lv_event_t *e) {
  if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
    modal_close();
    if (s_cb) { s_cb(false); s_cb = nullptr; }
  }
}

void confirm_cb(lv_event_t * /*e*/) {
  char entered[5] = {};
  for (int i = 0; i < 4; ++i) {
    entered[i] = (char)('0' + lv_roller_get_selected(s_r[i]));
  }
  const String pin = app_settings_settings_pin();
  const bool ok = (strncmp(entered, pin.c_str(), 4) == 0);
  modal_close();
  if (s_cb) { s_cb(ok); s_cb = nullptr; }
}

} // namespace

void ui_pin_entry_show(ui_pin_result_cb_t cb) {
  if (s_bg != nullptr) return;
  s_cb = cb;

  const lv_coord_t scr_w = lv_disp_get_hor_res(nullptr);
  const lv_coord_t scr_h = lv_disp_get_ver_res(nullptr);

  s_bg = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_bg, scr_w, scr_h);
  lv_obj_align(s_bg, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(s_bg, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_bg, LV_OPA_60, 0);
  lv_obj_set_style_border_width(s_bg, 0, 0);
  lv_obj_set_style_pad_all(s_bg, 0, 0);
  lv_obj_set_style_radius(s_bg, 0, 0);
  lv_obj_clear_flag(s_bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_bg, bg_click_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *modal = lv_obj_create(s_bg);
  lv_obj_set_width(modal, 460);
  lv_obj_set_height(modal, LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(modal, scr_h - 20, 0);
  lv_obj_center(modal);
  lv_obj_set_style_bg_color(modal, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(modal, 12, 0);
  lv_obj_set_style_pad_all(modal, 16, 0);
  lv_obj_set_style_pad_row(modal, 14, 0);
  lv_obj_set_layout(modal, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(modal);
  lv_label_set_text(title, LV_SYMBOL_SETTINGS " Codigo de acesso");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x449D48), 0);

  /* Linha com 4 rollers de digitos */
  lv_obj_t *row = lv_obj_create(modal);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < 4; ++i) {
    s_r[i] = lv_roller_create(row);
    lv_roller_set_options(s_r[i], kDigits, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_r[i], 3);
    lv_obj_set_style_text_font(s_r[i], &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_r[i], &lv_font_montserrat_28, LV_PART_SELECTED);
    lv_obj_set_height(s_r[i], 140);
    lv_obj_set_width(s_r[i], 80);
  }

  /* Botoes */
  lv_obj_t *btn_row = lv_obj_create(modal);
  lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(btn_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(btn_row, 0, 0);
  lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(btn_row, 0, 0);
  lv_obj_set_style_pad_top(btn_row, 8, 0);
  lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *btn_cancel = lv_btn_create(btn_row);
  lv_obj_set_size(btn_cancel, 160, 54);
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x888888), 0);
  lv_obj_t *lbl_c = lv_label_create(btn_cancel);
  lv_label_set_text(lbl_c, LV_SYMBOL_CLOSE " Cancelar");
  lv_obj_set_style_text_font(lbl_c, &lv_font_montserrat_16, 0);
  lv_obj_center(lbl_c);
  lv_obj_add_event_cb(btn_cancel, cancel_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *btn_ok = lv_btn_create(btn_row);
  lv_obj_set_size(btn_ok, 160, 54);
  lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x449D48), 0);
  lv_obj_t *lbl_ok = lv_label_create(btn_ok);
  lv_label_set_text(lbl_ok, LV_SYMBOL_OK " Entrar");
  lv_obj_set_style_text_font(lbl_ok, &lv_font_montserrat_16, 0);
  lv_obj_center(lbl_ok);
  lv_obj_add_event_cb(btn_ok, confirm_cb, LV_EVENT_CLICKED, nullptr);
}
