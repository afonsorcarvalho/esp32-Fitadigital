/**
 * @file ui_pin_entry.cpp
 * @brief Modal de introducao de senha alfanumerica (4-16 chars).
 *
 * Substitui o anterior modal de 4 rollers numericos.
 * Usa lv_textarea + lv_keyboard para aceitar qualquer senha configurada
 * pelo utilizador (4 a 16 caracteres alfanumericos).
 */
#include "ui_pin_entry.h"
#include "app_settings.h"

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>
#include "ui_theme.h"

namespace {

enum class Mode : uint8_t { Validate, Capture };

lv_obj_t           *s_bg        = nullptr;
lv_obj_t           *s_ta        = nullptr;
lv_obj_t           *s_kb        = nullptr;
/** Spinner visivel durante o delay de validacao (so no modo Validate). */
lv_obj_t           *s_spinner   = nullptr;
/** Timer de 300 ms que conclui a validacao apos o spinner ter sido apresentado. */
lv_timer_t         *s_val_timer = nullptr;
ui_pin_result_cb_t  s_cb_validate  = nullptr;
ui_pin_capture_cb_t s_cb_capture   = nullptr;
Mode                s_mode = Mode::Validate;

static constexpr size_t kPassMax = 16U;
/** Duracao do spinner de validacao (ms). Suficiente para o user ver o feedback. */
static constexpr uint32_t kValidateDelayMs = 300U;

/* Buffer temporario: copiado do textarea antes de o modal ser fechado. */
static char s_pending_text[kPassMax + 1] = {};

void modal_close(void) {
  if (s_val_timer != nullptr) {
    lv_timer_del(s_val_timer);
    s_val_timer = nullptr;
  }
  if (s_bg != nullptr) {
    lv_obj_del(s_bg);
    s_bg      = nullptr;
    s_ta      = nullptr;
    s_kb      = nullptr;
    s_spinner = nullptr;
  }
}

void dispatch_cancel(void) {
  modal_close();
  if (s_mode == Mode::Validate) {
    if (s_cb_validate) { s_cb_validate(false); s_cb_validate = nullptr; }
  } else {
    if (s_cb_capture)  { s_cb_capture(false, nullptr); s_cb_capture = nullptr; }
  }
}

/* Timer callback: modal ja esta' fechado; invoca o callback de resultado. */
static void validate_timer_cb(lv_timer_t *t) {
  (void)t;
  lv_timer_del(s_val_timer);
  s_val_timer = nullptr;

  const String saved = app_settings_settings_pin();
  const bool ok = (strcmp(s_pending_text, saved.c_str()) == 0);
  modal_close();
  if (s_cb_validate) { s_cb_validate(ok); s_cb_validate = nullptr; }
}

void dispatch_confirm(void) {
  const char *text = (s_ta != nullptr) ? lv_textarea_get_text(s_ta) : "";
  if (text == nullptr) text = "";

  if (s_mode == Mode::Validate) {
    /* Guardar texto antes de qualquer alteracao de estado. */
    strncpy(s_pending_text, text, kPassMax);
    s_pending_text[kPassMax] = '\0';

    /* Desabilitar teclado e o fundo para evitar cliques duplos. */
    if (s_kb != nullptr)  { lv_obj_add_state(s_kb, LV_STATE_DISABLED); }
    if (s_bg != nullptr)  { lv_obj_clear_flag(s_bg, LV_OBJ_FLAG_CLICKABLE); }

    /* Mostrar spinner centrado sobre o modal. */
    if (s_bg != nullptr && s_spinner == nullptr) {
      s_spinner = lv_spinner_create(s_bg, 1000, 60);
      lv_obj_set_size(s_spinner, 64, 64);
      lv_obj_align(s_spinner, LV_ALIGN_CENTER, 0, -60);
      lv_obj_set_style_arc_color(s_spinner, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
      lv_obj_set_style_arc_width(s_spinner, 6, LV_PART_INDICATOR);
      lv_obj_set_style_arc_color(s_spinner, UI_COLOR_PRIMARY_LIGHT, LV_PART_MAIN);
      lv_obj_set_style_arc_width(s_spinner, 6, LV_PART_MAIN);
    }

    /* Timer de um disparo: conclui a validacao apos kValidateDelayMs. */
    s_val_timer = lv_timer_create(validate_timer_cb, kValidateDelayMs, nullptr);
    lv_timer_set_repeat_count(s_val_timer, 1);

  } else {
    /* Modo Capture: sem spinner, resposta imediata. */
    char buf[kPassMax + 1];
    strncpy(buf, text, kPassMax);
    buf[kPassMax] = '\0';
    ui_pin_capture_cb_t cb = s_cb_capture;
    s_cb_capture = nullptr;
    modal_close();
    if (cb) { cb(true, buf); }
  }
}

void cancel_cb(lv_event_t * /*e*/) {
  dispatch_cancel();
}

void confirm_cb(lv_event_t * /*e*/) {
  dispatch_confirm();
}

void bg_click_cb(lv_event_t *e) {
  if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
    dispatch_cancel();
  }
}

void kb_ready_cb(lv_event_t * /*e*/) {
  /* Tecla OK (checkmark) do teclado confirma a entrada. */
  dispatch_confirm();
}

void kb_cancel_cb(lv_event_t * /*e*/) {
  dispatch_cancel();
}

void show_modal(const char *title_text) {
  if (s_bg != nullptr) return;

  const lv_coord_t scr_w = lv_disp_get_hor_res(nullptr);
  const lv_coord_t scr_h = lv_disp_get_ver_res(nullptr);
  const lv_coord_t kb_h  = (lv_coord_t)(scr_h * 40 / 100);

  s_bg = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_bg, scr_w, scr_h);
  lv_obj_align(s_bg, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(s_bg, UI_COLOR_BLACK, 0);
  lv_obj_set_style_bg_opa(s_bg, LV_OPA_60, 0);
  lv_obj_set_style_border_width(s_bg, 0, 0);
  lv_obj_set_style_pad_all(s_bg, 0, 0);
  lv_obj_set_style_radius(s_bg, 0, 0);
  lv_obj_clear_flag(s_bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_bg, bg_click_cb, LV_EVENT_CLICKED, nullptr);

  /* Painel central (acima do teclado). */
  lv_obj_t *modal = lv_obj_create(s_bg);
  lv_obj_set_width(modal, scr_w - 40);
  lv_obj_set_height(modal, LV_SIZE_CONTENT);
  lv_obj_align(modal, LV_ALIGN_TOP_MID, 0, 16);
  lv_obj_set_style_bg_color(modal, ui_color_surface(app_settings_dark_mode()), 0);
  lv_obj_set_style_radius(modal, 12, 0);
  lv_obj_set_style_pad_all(modal, 16, 0);
  lv_obj_set_style_pad_row(modal, 12, 0);
  lv_obj_set_layout(modal, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

  /* Titulo. */
  lv_obj_t *title = lv_label_create(modal);
  lv_label_set_text(title, title_text);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(title, UI_COLOR_PRIMARY, 0);

  /* Campo de texto (senha oculta). */
  s_ta = lv_textarea_create(modal);
  lv_obj_set_width(s_ta, LV_PCT(100));
  lv_textarea_set_one_line(s_ta, true);
  lv_textarea_set_max_length(s_ta, kPassMax);
  lv_textarea_set_password_mode(s_ta, true);
  lv_textarea_set_placeholder_text(s_ta, "Senha (4-16 caracteres)");
  lv_obj_set_style_text_font(s_ta, &lv_font_montserrat_20, LV_PART_MAIN);

  /* Fila de botoes. */
  lv_obj_t *btn_row = lv_obj_create(modal);
  lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(btn_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(btn_row, 0, 0);
  lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(btn_row, 0, 0);
  lv_obj_set_style_pad_top(btn_row, 4, 0);
  lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *btn_cancel = lv_btn_create(btn_row);
  lv_obj_set_size(btn_cancel, 160, 48);
  lv_obj_set_style_bg_color(btn_cancel, UI_COLOR_TEXT_MUTED, 0);
  lv_obj_t *lbl_c = lv_label_create(btn_cancel);
  lv_label_set_text(lbl_c, LV_SYMBOL_CLOSE " Cancelar");
  lv_obj_set_style_text_font(lbl_c, &lv_font_montserrat_16, 0);
  lv_obj_center(lbl_c);
  lv_obj_add_event_cb(btn_cancel, cancel_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *btn_ok = lv_btn_create(btn_row);
  lv_obj_set_size(btn_ok, 160, 48);
  lv_obj_set_style_bg_color(btn_ok, UI_COLOR_PRIMARY, 0);
  lv_obj_t *lbl_ok = lv_label_create(btn_ok);
  lv_label_set_text(lbl_ok, LV_SYMBOL_OK " OK");
  lv_obj_set_style_text_font(lbl_ok, &lv_font_montserrat_16, 0);
  lv_obj_center(lbl_ok);
  lv_obj_add_event_cb(btn_ok, confirm_cb, LV_EVENT_CLICKED, nullptr);

  /* Teclado fixo no fundo do ecra, associado ao textarea.
   * Usar callbacks separados para READY/CANCEL em vez de LV_EVENT_ALL
   * para nao interferir com o processamento interno do teclado LVGL. */
  s_kb = lv_keyboard_create(s_bg);
  lv_obj_set_size(s_kb, scr_w, kb_h);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(s_kb, s_ta);
  lv_obj_add_event_cb(s_kb, kb_ready_cb,  LV_EVENT_READY,  nullptr);
  lv_obj_add_event_cb(s_kb, kb_cancel_cb, LV_EVENT_CANCEL, nullptr);
}

} // namespace

void ui_pin_entry_show(ui_pin_result_cb_t cb) {
  if (s_bg != nullptr) return;
  s_mode = Mode::Validate;
  s_cb_validate = cb;
  s_cb_capture = nullptr;
  show_modal(LV_SYMBOL_SETTINGS " Codigo de acesso");
}

void ui_pin_entry_capture_show(const char *title, ui_pin_capture_cb_t cb) {
  if (s_bg != nullptr) return;
  s_mode = Mode::Capture;
  s_cb_capture = cb;
  s_cb_validate = nullptr;
  show_modal(title ? title : "Introduzir senha");
}
