/**
 * @file ui_share_qr.cpp
 * @brief Implementacao de ui_share_qr.h — modal LVGL + QR code + URL em texto.
 */
#include "ui_share_qr.h"
#include "ui_toast.h"

#include "app_settings.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include "ui_theme.h"

namespace {

lv_obj_t *s_modal_bg = nullptr; /**< Cobre o ecra inteiro; clicavel para fechar. */
lv_obj_t *s_modal = nullptr;    /**< Cartao branco central com o QR. */

bool url_char_is_safe(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == '.' || c == '~';
}

/** Preenche `out` com versao URL-encoded de `in` (RFC 3986 unreserved). */
size_t url_encode(const char *in, char *out, size_t out_sz) {
  if (in == nullptr || out == nullptr || out_sz == 0U) {
    return 0;
  }
  static const char hex[] = "0123456789ABCDEF";
  size_t i = 0;
  for (const char *p = in; *p != '\0' && i + 4U < out_sz; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (url_char_is_safe(static_cast<char>(c))) {
      out[i++] = static_cast<char>(c);
    } else {
      out[i++] = '%';
      out[i++] = hex[(c >> 4) & 0xFu];
      out[i++] = hex[c & 0xFu];
    }
  }
  out[i] = '\0';
  return i;
}

void modal_close(void) {
  if (s_modal_bg != nullptr) {
    lv_obj_del(s_modal_bg);
    s_modal_bg = nullptr;
    s_modal = nullptr;
  }
}

void close_btn_cb(lv_event_t * /*e*/) { modal_close(); }
void bg_click_cb(lv_event_t *e) {
  /* Fecha so se o click foi no fundo, nao num filho (modal/botao). */
  if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
    modal_close();
  }
}

} // namespace

void ui_share_qr_show(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    ui_toast_show(ToastKind::Warn, "Caminho invalido");
    return;
  }
  if (!WiFi.isConnected()) {
    ui_toast_show(ToastKind::Warn, "Sem Wi-Fi para gerar URL");
    return;
  }

  /* Monta a URL completa. */
  String base = app_settings_download_url();
  if (base.length() == 0) {
    base = String("http://") + WiFi.localIP().toString() + "/api/fs/file";
  }
  char enc_path[512];
  url_encode(path, enc_path, sizeof(enc_path));
  char full_url[640];
  (void)snprintf(full_url, sizeof(full_url), "%s?path=%s", base.c_str(), enc_path);

  /* Destroi qualquer modal anterior (caso tenha ficado orfao). */
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
  lv_obj_set_size(s_modal, 360, 440);
  lv_obj_center(s_modal);
  lv_obj_set_style_bg_color(s_modal, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_modal, 12, 0);
  lv_obj_set_style_pad_all(s_modal, 16, 0);
  lv_obj_set_style_pad_row(s_modal, 10, 0);
  lv_obj_set_layout(s_modal, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_modal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(s_modal);
  lv_label_set_text(title, LV_SYMBOL_UPLOAD " Partilhar ciclo");
  lv_obj_set_style_text_color(title, UI_COLOR_PRIMARY, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

  lv_obj_t *qr = lv_qrcode_create(s_modal, 240, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
  const lv_res_t r = lv_qrcode_update(qr, full_url, strlen(full_url));
  if (r != LV_RES_OK) {
    ui_toast_show(ToastKind::Error, "URL demasiado longa para QR");
  }

  lv_obj_t *url_lbl = lv_label_create(s_modal);
  lv_label_set_long_mode(url_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(url_lbl, LV_PCT(100));
  lv_label_set_text(url_lbl, full_url);
  lv_obj_set_style_text_color(url_lbl, lv_color_hex(0x606060), 0);
  lv_obj_set_style_text_align(url_lbl, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *btn_close = lv_btn_create(s_modal);
  lv_obj_set_size(btn_close, 160, 44);
  lv_obj_set_style_bg_color(btn_close, UI_COLOR_PRIMARY, 0);
  lv_obj_t *lbl_close = lv_label_create(btn_close);
  lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE " Fechar");
  lv_obj_center(lbl_close);
  lv_obj_add_event_cb(btn_close, close_btn_cb, LV_EVENT_CLICKED, nullptr);
}
