/**
 * @file ui_date_goto.cpp
 * @brief Implementacao de ui_date_goto.h.
 */
#include "ui_date_goto.h"
#include "file_browser.h"
#include "ui_toast.h"

#include <Arduino.h>
#include <lvgl.h>
#include <time.h>

namespace {

lv_obj_t *s_modal_bg = nullptr;
lv_obj_t *s_modal = nullptr;
lv_obj_t *s_rll_day = nullptr;
lv_obj_t *s_rll_mon = nullptr;
lv_obj_t *s_rll_year = nullptr;

char s_day_opts[256];
char s_mon_opts[64];
char s_year_opts[256];

void build_fixed_options(void) {
  /* 31 dias; dias invalidos (ex.: 31/02) caem em SD.exists e devolvem toast. */
  char *p = s_day_opts;
  for (int d = 1; d <= 31; ++d) {
    p += sprintf(p, "%02d%s", d, (d == 31) ? "" : "\n");
  }
  p = s_mon_opts;
  for (int m = 1; m <= 12; ++m) {
    p += sprintf(p, "%02d%s", m, (m == 12) ? "" : "\n");
  }
}

void build_year_options(int start, int end) {
  char *p = s_year_opts;
  for (int y = start; y <= end; ++y) {
    p += sprintf(p, "%04d%s", y, (y == end) ? "" : "\n");
  }
}

void modal_close(void) {
  if (s_modal_bg != nullptr) {
    lv_obj_del(s_modal_bg);
    s_modal_bg = nullptr;
    s_modal = nullptr;
    s_rll_day = nullptr;
    s_rll_mon = nullptr;
    s_rll_year = nullptr;
  }
}

void close_cb(lv_event_t * /*e*/) { modal_close(); }

void bg_click_cb(lv_event_t *e) {
  if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
    modal_close();
  }
}

int year_start_year(void) {
  /* Janela de 10 anos centrada no ano actual (fallback 2026 se o relogio nao tem data). */
  const time_t now = time(nullptr);
  struct tm lt {};
  int y = 2026;
  if (now > (time_t)1577836800 && localtime_r(&now, &lt) != nullptr) {
    y = lt.tm_year + 1900;
  }
  return y - 5;
}

void select_date_in_rollers(int d, int m, int y) {
  if (s_rll_day == nullptr) return;
  const int start = year_start_year();
  lv_roller_set_selected(s_rll_day, (uint16_t)(d - 1), LV_ANIM_ON);
  lv_roller_set_selected(s_rll_mon, (uint16_t)(m - 1), LV_ANIM_ON);
  if (y >= start && y <= 2050) {
    lv_roller_set_selected(s_rll_year, (uint16_t)(y - start), LV_ANIM_ON);
  }
}

void today_cb(lv_event_t * /*e*/) {
  const time_t now = time(nullptr);
  struct tm lt {};
  if (now > (time_t)1577836800 && localtime_r(&now, &lt) != nullptr) {
    select_date_in_rollers(lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900);
  }
}

void yesterday_cb(lv_event_t * /*e*/) {
  const time_t yesterday = time(nullptr) - 86400;
  struct tm lt {};
  if (yesterday > (time_t)1577836800 && localtime_r(&yesterday, &lt) != nullptr) {
    select_date_in_rollers(lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900);
  }
}

void go_cb(lv_event_t * /*e*/) {
  if (s_rll_day == nullptr || s_rll_mon == nullptr || s_rll_year == nullptr) {
    return;
  }
  const int d = static_cast<int>(lv_roller_get_selected(s_rll_day)) + 1;
  const int m = static_cast<int>(lv_roller_get_selected(s_rll_mon)) + 1;
  const int y = year_start_year() + static_cast<int>(lv_roller_get_selected(s_rll_year));
  modal_close();
  if (!file_browser_open_cycle_by_date(y, m, d)) {
    ui_toast_show(ToastKind::Warn, "Sem ciclo nesse dia");
  }
}

} // namespace

void ui_date_goto_show(void) {
  build_fixed_options();
  const int start = year_start_year();
  build_year_options(start, 2050);

  modal_close();

  const lv_coord_t scr_w = lv_disp_get_hor_res(nullptr);
  const lv_coord_t scr_h = lv_disp_get_ver_res(nullptr);

  s_modal_bg = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_modal_bg, scr_w, scr_h);
  lv_obj_align(s_modal_bg, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(s_modal_bg, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_modal_bg, LV_OPA_60, 0);
  lv_obj_set_style_border_width(s_modal_bg, 0, 0);
  lv_obj_set_style_pad_all(s_modal_bg, 0, 0);
  lv_obj_set_style_radius(s_modal_bg, 0, 0);
  lv_obj_clear_flag(s_modal_bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_modal_bg, bg_click_cb, LV_EVENT_CLICKED, nullptr);

  s_modal = lv_obj_create(s_modal_bg);
  lv_obj_set_width(s_modal, 620);
  lv_obj_set_height(s_modal, LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(s_modal, scr_h - 20, 0); /* nunca ultrapassa o ecra */
  lv_obj_center(s_modal);
  lv_obj_set_style_bg_color(s_modal, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(s_modal, 12, 0);
  lv_obj_set_style_pad_all(s_modal, 16, 0);
  lv_obj_set_style_pad_row(s_modal, 12, 0);
  lv_obj_set_layout(s_modal, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_modal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(s_modal);
  lv_label_set_text(title, LV_SYMBOL_LIST " Ir para ciclo do dia");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x449D48), 0);

  /* Linha com os 3 rollers: dia, mes, ano. 50 px de margem do titulo. */
  lv_obj_t *rollers_row = lv_obj_create(s_modal);
  lv_obj_set_size(rollers_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(rollers_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(rollers_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(rollers_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(rollers_row, 0, 0);
  lv_obj_set_style_bg_opa(rollers_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(rollers_row, 0, 0);
  lv_obj_clear_flag(rollers_row, LV_OBJ_FLAG_SCROLLABLE);

  /* Fonte Montserrat 28 + altura explicita para aumentar ~50 px face ao calculo automatico. */
  auto style_roller = [](lv_obj_t *r) {
    lv_obj_set_style_text_font(r, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_28, LV_PART_SELECTED);
    lv_obj_set_height(r, 140);
  };
  s_rll_day = lv_roller_create(rollers_row);
  lv_roller_set_options(s_rll_day, s_day_opts, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(s_rll_day, 3);
  style_roller(s_rll_day);

  s_rll_mon = lv_roller_create(rollers_row);
  lv_roller_set_options(s_rll_mon, s_mon_opts, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(s_rll_mon, 3);
  style_roller(s_rll_mon);

  s_rll_year = lv_roller_create(rollers_row);
  lv_roller_set_options(s_rll_year, s_year_opts, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(s_rll_year, 3);
  style_roller(s_rll_year);

  /* Pre-selecciona o dia corrente. */
  const time_t now = time(nullptr);
  struct tm lt {};
  if (now > (time_t)1577836800 && localtime_r(&now, &lt) != nullptr) {
    lv_roller_set_selected(s_rll_day, (uint16_t)(lt.tm_mday - 1), LV_ANIM_OFF);
    lv_roller_set_selected(s_rll_mon, (uint16_t)lt.tm_mon, LV_ANIM_OFF);
    const int y_now = lt.tm_year + 1900;
    if (y_now >= start) {
      lv_roller_set_selected(s_rll_year, (uint16_t)(y_now - start), LV_ANIM_OFF);
    }
  }

  /* Atalhos rapidos: Hoje / Ontem — preenchem rollers, nao navegam. */
  lv_obj_t *shortcut_row = lv_obj_create(s_modal);
  lv_obj_set_size(shortcut_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(shortcut_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(shortcut_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(shortcut_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(shortcut_row, 0, 0);
  lv_obj_set_style_bg_opa(shortcut_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(shortcut_row, 0, 0);
  lv_obj_set_style_pad_column(shortcut_row, 16, 0);
  lv_obj_clear_flag(shortcut_row, LV_OBJ_FLAG_SCROLLABLE);

  auto make_shortcut = [&](const char *label, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(shortcut_row);
    lv_obj_set_size(btn, 140, 44);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xE8F1E9), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xC5DEC7), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 22, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x2A6B2E), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
  };
  make_shortcut(LV_SYMBOL_HOME " Hoje", today_cb);
  make_shortcut(LV_SYMBOL_LEFT " Ontem", yesterday_cb);

  /* Linha de botoes principais. */
  lv_obj_t *btn_row = lv_obj_create(s_modal);
  lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(btn_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(btn_row, 0, 0);
  lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(btn_row, 0, 0);
  lv_obj_set_style_pad_top(btn_row, 16, 0);
  lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *btn_cancel = lv_btn_create(btn_row);
  lv_obj_set_size(btn_cancel, 180, 57); /* +30% na altura (44 -> 57) */
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x888888), 0);
  lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
  lv_label_set_text(lbl_cancel, LV_SYMBOL_CLOSE " Cancelar");
  lv_obj_set_style_text_font(lbl_cancel, &lv_font_montserrat_18, 0); /* +4 pt do default */
  lv_obj_center(lbl_cancel);
  lv_obj_add_event_cb(btn_cancel, close_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *btn_go = lv_btn_create(btn_row);
  lv_obj_set_size(btn_go, 180, 57);
  lv_obj_set_style_bg_color(btn_go, lv_color_hex(0x449D48), 0);
  lv_obj_t *lbl_go = lv_label_create(btn_go);
  lv_label_set_text(lbl_go, LV_SYMBOL_OK " Ir");
  lv_obj_set_style_text_font(lbl_go, &lv_font_montserrat_18, 0);
  lv_obj_center(lbl_go);
  lv_obj_add_event_cb(btn_go, go_cb, LV_EVENT_CLICKED, nullptr);
}
