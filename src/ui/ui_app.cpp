/**
 * @file ui_app.cpp
 * @brief Ecra Wi-Fi (primeiro), principal com barra (Wi-Fi + forca aproximada, data/hora), definicoes, explorador SD.
 */
#include "ui_app.h"
#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_settings.h"
#include "app_settings_sd.h"
#include "app_log.h"
#include "boot_screen.h"
#include "file_browser.h"
#include "lvgl_port_v8.h"
#include "net_services.h"
#include "net_time.h"
#include "net_wireguard.h"
#include "ui/ui_loading.h"
#include "web_remote/web_remote.h"

static constexpr int kStatusBarH = 46;

static lv_obj_t *s_scr_main = nullptr;
static lv_obj_t *s_scr_settings = nullptr;
static lv_obj_t *s_scr_wifi = nullptr;
/** Barra principal: icone Wi-Fi + forca do sinal (%). */
static lv_obj_t *s_bar_wifi_lbl = nullptr;
/** Barra principal: data e hora (DD/MM/AAAA HH:MM). */
static lv_obj_t *s_bar_time_lbl = nullptr;
/** Barra definicoes: Wi-Fi + %. */
static lv_obj_t *s_bar_settings_wifi_lbl = nullptr;
/** Barra definicoes: data e hora. */
static lv_obj_t *s_bar_settings_time_lbl = nullptr;
static lv_obj_t *s_main_content = nullptr;

static lv_obj_t *s_ta_wifi_ssid = nullptr;
static lv_obj_t *s_ta_wifi_pass = nullptr;
static lv_obj_t *s_wifi_status_lbl = nullptr;
static lv_obj_t *s_wifi_kb = nullptr;
static lv_obj_t *s_wifi_back_btn = nullptr;

/** Raiz do conteudo de definicoes (filho do ecra s_scr_settings, abaixo da barra). */
static lv_obj_t *s_settings_root = nullptr;
static lv_obj_t *s_sett_wifi_lbl = nullptr;
static lv_obj_t *s_sett_ftp_info_lbl = nullptr;
static lv_obj_t *s_ta_ftp_user = nullptr;
static lv_obj_t *s_ta_ftp_pass = nullptr;
static lv_obj_t *s_sett_ftp_kb = nullptr;
static lv_obj_t *s_ftp_feedback_lbl = nullptr;
static lv_obj_t *s_font_slider = nullptr;

static lv_obj_t *s_sw_ntp = nullptr;
static lv_obj_t *s_ta_ntp_srv = nullptr;
static lv_obj_t *s_roller_tz = nullptr;
static lv_obj_t *s_ta_utc_manual = nullptr;
static lv_obj_t *s_time_feedback_lbl = nullptr;
static lv_obj_t *s_sett_time_kb = nullptr;

static lv_obj_t *s_sw_wg = nullptr;
static lv_obj_t *s_ta_wg_ip = nullptr;
static lv_obj_t *s_ta_wg_priv = nullptr;
static lv_obj_t *s_ta_wg_pub = nullptr;
static lv_obj_t *s_ta_wg_ep = nullptr;
static lv_obj_t *s_ta_wg_port = nullptr;
static lv_obj_t *s_wg_feedback_lbl = nullptr;
static lv_obj_t *s_sett_wg_kb = nullptr;
static lv_obj_t *s_vnc_scale_slider = nullptr;
static lv_obj_t *s_vnc_quality_slider = nullptr;
static lv_obj_t *s_vnc_interval_slider = nullptr;
static lv_obj_t *s_vnc_scale_val_lbl = nullptr;
static lv_obj_t *s_vnc_quality_val_lbl = nullptr;
static lv_obj_t *s_vnc_interval_val_lbl = nullptr;
static lv_obj_t *s_vnc_feedback_lbl = nullptr;
static lv_obj_t *s_log_textarea = nullptr;
static lv_obj_t *s_log_info_lbl = nullptr;
static lv_obj_t *s_sd_format_status_lbl = nullptr;
static lv_obj_t *s_sd_format_confirm_btn = nullptr;
static bool s_sd_format_armed = false;

static lv_timer_t *s_status_timer = nullptr;

static bool s_sd_at_boot = false;

static void refresh_settings_wifi_label(void);
static void refresh_settings_ftp_label(void);
static void settings_hide_ftp_keyboard(void);
static void settings_hide_time_keyboard(void);
static void settings_hide_wg_keyboard(void);
static void settings_sd_format_status_refresh(const char *msg_override);

typedef struct {
  int tries;
} wifi_try_ud_t;

static const lv_font_t *font_for_index(uint8_t idx) {
  switch (idx) {
    case 1:
      return &lv_font_montserrat_16;
    case 2:
      return &lv_font_montserrat_18;
    case 3:
      return &lv_font_montserrat_20;
    default:
      return &lv_font_montserrat_14;
  }
}

static void apply_font_tree(lv_obj_t *obj, const lv_font_t *font) {
  if (obj == nullptr || font == nullptr) {
    return;
  }
  lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
  const uint32_t n = lv_obj_get_child_cnt(obj);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *ch = lv_obj_get_child(obj, (int32_t)i);
    if (ch != nullptr) {
      apply_font_tree(ch, font);
    }
  }
}

static void ui_apply_font_everywhere(void) {
  const lv_font_t *f = font_for_index(app_settings_font_index());
  if (s_scr_main != nullptr) {
    apply_font_tree(s_scr_main, f);
  }
  if (s_scr_wifi != nullptr) {
    apply_font_tree(s_scr_wifi, f);
  }
  if (s_scr_settings != nullptr) {
    apply_font_tree(s_scr_settings, f);
  }
  file_browser_apply_font(f);
}

/**
 * Aproxima RSSI (dBm) para percentagem: -100..-50 mapeado para 0..100 (uso comum em UI).
 */
static int wifi_rssi_to_percent(int rssi_dbm) {
  if (rssi_dbm <= -100) {
    return 0;
  }
  if (rssi_dbm >= -50) {
    return 100;
  }
  return 2 * (rssi_dbm + 100);
}

static void bar_label_set_signal_only(lv_obj_t *lbl) {
  if (lbl == nullptr) {
    return;
  }
  char line[40];
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    const int pct = wifi_rssi_to_percent(WiFi.RSSI());
    snprintf(line, sizeof line, LV_SYMBOL_WIFI " %d%%", pct);
  } else if (st == WL_IDLE_STATUS || st == WL_DISCONNECTED) {
    snprintf(line, sizeof line, LV_SYMBOL_WIFI " ...");
  } else {
    snprintf(line, sizeof line, LV_SYMBOL_WIFI " --");
  }
  lv_label_set_text(lbl, line);
}

static void bar_label_set_date_time(lv_obj_t *lbl) {
  if (lbl == nullptr) {
    return;
  }
  char tline[40];
  net_time_format_status_line(tline, sizeof tline);
  lv_label_set_text(lbl, tline);
}

static void update_bar_wifi_text(void) {
  bar_label_set_signal_only(s_bar_wifi_lbl);
  bar_label_set_date_time(s_bar_time_lbl);
  bar_label_set_signal_only(s_bar_settings_wifi_lbl);
  bar_label_set_date_time(s_bar_settings_time_lbl);
}

static void status_timer_cb(lv_timer_t *t) {
  (void)t;
  update_bar_wifi_text();
  if (s_scr_settings != nullptr && lv_disp_get_scr_act(nullptr) == s_scr_settings) {
    if (s_sett_wifi_lbl != nullptr) {
      refresh_settings_wifi_label();
    }
    if (s_sett_ftp_info_lbl != nullptr) {
      refresh_settings_ftp_label();
    }
  }
}

/** Se true, o ecra Wi-Fi volta para definicoes em vez do principal. */
static bool s_wifi_return_to_settings = false;

static void create_settings_screen(void);
static void settings_screen_enter(void);
static void settings_back_cb(lv_event_t *e);

static void ensure_main_content_browser(void) {
  if (s_main_content == nullptr) {
    return;
  }
  lv_obj_clean(s_main_content);
  if (s_sd_at_boot && file_browser_init(s_main_content)) {
    /* explorador criado */
  } else {
    lv_obj_t *msg = lv_label_create(s_main_content);
    lv_label_set_text(msg, s_sd_at_boot ? "Nao foi possivel listar o SD." : "SD nao montado.\nUse cartao FAT32 MBR.");
    lv_obj_center(msg);
  }
  ui_apply_font_everywhere();
}

static void enter_main_after_wifi_connected(void) {
  if (s_wifi_back_btn != nullptr) {
    lv_obj_add_flag(s_wifi_back_btn, LV_OBJ_FLAG_HIDDEN);
  }
  lv_scr_load(s_scr_main);
  ensure_main_content_browser();
}

static lv_timer_t *s_wifi_wait_timer = nullptr;
static wifi_try_ud_t *s_wifi_wait_ud = nullptr;

static void wifi_try_timer_cb(lv_timer_t *tm) {
  auto *ud = static_cast<wifi_try_ud_t *>(tm->user_data);
  if (ud == nullptr) {
    lv_timer_del(tm);
    s_wifi_wait_timer = nullptr;
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    lv_label_set_text(s_wifi_status_lbl, "Wi-Fi ligado.");
    lv_timer_del(tm);
    s_wifi_wait_timer = nullptr;
    free(ud);
    s_wifi_wait_ud = nullptr;
    enter_main_after_wifi_connected();
    return;
  }
  ud->tries++;
  char m[48];
  snprintf(m, sizeof m, "A ligar... (%d)", ud->tries);
  lv_label_set_text(s_wifi_status_lbl, m);
  if (ud->tries >= 60) {
    lv_label_set_text(s_wifi_status_lbl, "Timeout. Verifique SSID/senha.");
    lv_timer_del(tm);
    s_wifi_wait_timer = nullptr;
    free(ud);
    s_wifi_wait_ud = nullptr;
  }
}

static void wifi_save_connect_cb(lv_event_t *e) {
  (void)e;
  if (s_wifi_wait_timer != nullptr) {
    lv_timer_del(s_wifi_wait_timer);
    s_wifi_wait_timer = nullptr;
    free(s_wifi_wait_ud);
    s_wifi_wait_ud = nullptr;
  }
  const char *ssid = lv_textarea_get_text(s_ta_wifi_ssid);
  const char *pass = lv_textarea_get_text(s_ta_wifi_pass);
  if (ssid == nullptr || strlen(ssid) == 0) {
    lv_label_set_text(s_wifi_status_lbl, "Preencha o nome da rede (SSID).");
    return;
  }
  app_settings_set_wifi(ssid, pass);
  net_wifi_begin(ssid, pass);
  lv_label_set_text(s_wifi_status_lbl, "A ligar...");
  auto *ud = static_cast<wifi_try_ud_t *>(malloc(sizeof(wifi_try_ud_t)));
  if (ud == nullptr) {
    return;
  }
  ud->tries = 0;
  s_wifi_wait_ud = ud;
  s_wifi_wait_timer = lv_timer_create(wifi_try_timer_cb, 500, ud);
}

/** Teclado do ecra Wi-Fi: visivel apenas com SSID ou senha focados. */
static void wifi_ta_kb_event_cb(lv_event_t *e) {
  if (s_wifi_kb == nullptr) {
    return;
  }
  const lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(s_wifi_kb, ta);
    lv_obj_clear_flag(s_wifi_kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED) {
    lv_obj_add_flag(s_wifi_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_wifi_kb, nullptr);
  }
}

static void wifi_back_cb(lv_event_t *e) {
  (void)e;
  lv_obj_add_flag(s_wifi_back_btn, LV_OBJ_FLAG_HIDDEN);
  if (s_wifi_return_to_settings) {
    s_wifi_return_to_settings = false;
    settings_screen_enter();
    lv_scr_load(s_scr_settings);
    return;
  }
  lv_scr_load(s_scr_main);
}

static void gear_btn_cb(lv_event_t *e) {
  (void)e;
  create_settings_screen();
  if (s_scr_settings == nullptr) {
    app_log_write("ERROR", "Falha ao criar ecra de definicoes (memoria LVGL insuficiente).");
    return;
  }
  settings_screen_enter();
  lv_scr_load(s_scr_settings);
}

static void settings_font_slider_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  lv_obj_t *sl = lv_event_get_target(e);
  int v = (int)lv_slider_get_value(sl);
  if (v < 0) {
    v = 0;
  }
  if (v > 3) {
    v = 3;
  }
  app_settings_set_font_index((uint8_t)v);
  ui_apply_font_everywhere();
}

static void settings_vnc_refresh_value_labels(void) {
  if (s_vnc_scale_val_lbl != nullptr && s_vnc_scale_slider != nullptr) {
    char b[24];
    snprintf(b, sizeof b, "%dx", (int)lv_slider_get_value(s_vnc_scale_slider));
    lv_label_set_text(s_vnc_scale_val_lbl, b);
  }
  if (s_vnc_quality_val_lbl != nullptr && s_vnc_quality_slider != nullptr) {
    char b[24];
    snprintf(b, sizeof b, "%d", (int)lv_slider_get_value(s_vnc_quality_slider));
    lv_label_set_text(s_vnc_quality_val_lbl, b);
  }
  if (s_vnc_interval_val_lbl != nullptr && s_vnc_interval_slider != nullptr) {
    char b[24];
    snprintf(b, sizeof b, "%d ms", (int)lv_slider_get_value(s_vnc_interval_slider));
    lv_label_set_text(s_vnc_interval_val_lbl, b);
  }
}

static void settings_vnc_slider_changed_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  settings_vnc_refresh_value_labels();
}

static void settings_save_vnc_cb(lv_event_t *e) {
  (void)e;
  if (s_vnc_scale_slider == nullptr || s_vnc_quality_slider == nullptr || s_vnc_interval_slider == nullptr) {
    return;
  }
  const uint8_t scale = (uint8_t)lv_slider_get_value(s_vnc_scale_slider);
  const uint8_t quality = (uint8_t)lv_slider_get_value(s_vnc_quality_slider);
  const uint16_t interval_ms = (uint16_t)lv_slider_get_value(s_vnc_interval_slider);

  app_settings_set_vnc_scale(scale);
  app_settings_set_vnc_jpeg_quality(quality);
  app_settings_set_vnc_interval_ms(interval_ms);

  web_remote_stream_cfg_t cfg;
  cfg.scale = scale;
  cfg.jpeg_quality = quality;
  cfg.interval_ms = interval_ms;
  web_remote_set_stream_config(&cfg);

  if (s_vnc_feedback_lbl != nullptr) {
    lv_label_set_text(s_vnc_feedback_lbl, "Guardado e aplicado no stream remoto.");
  }
  app_log_writef("INFO", "VNC atualizado: scale=%u quality=%u interval=%u", (unsigned)scale, (unsigned)quality,
                 (unsigned)interval_ms);
}

static void settings_log_refresh_view(void) {
  if (s_log_textarea == nullptr || s_log_info_lbl == nullptr) {
    return;
  }
  static char tail[8 * 1024];
  size_t total_sz = 0U;
  bool truncated = false;
  const bool ok = app_log_read_tail(tail, sizeof(tail), &total_sz, &truncated);
  if (!ok) {
    lv_textarea_set_text(s_log_textarea, "(Log indisponivel. Verifique cartao SD.)");
    lv_label_set_text(s_log_info_lbl, "Sem log.");
    return;
  }
  if (tail[0] == '\0') {
    lv_textarea_set_text(s_log_textarea, "(Sem entradas de log)");
  } else {
    lv_textarea_set_text(s_log_textarea, tail);
  }
  if (truncated) {
    lv_label_set_text_fmt(s_log_info_lbl, "Mostrando final do arquivo (%u bytes).", (unsigned)total_sz);
  } else {
    lv_label_set_text_fmt(s_log_info_lbl, "Arquivo completo (%u bytes).", (unsigned)total_sz);
  }
}

static void settings_log_refresh_cb(lv_event_t *e) {
  (void)e;
  settings_log_refresh_view();
}

static void settings_log_clear_cb(lv_event_t *e) {
  (void)e;
  if (app_log_clear()) {
    app_log_write("INFO", "Log limpo manualmente pela UI.");
    settings_log_refresh_view();
    if (s_log_info_lbl != nullptr) {
      lv_label_set_text(s_log_info_lbl, "Log limpo.");
    }
  } else if (s_log_info_lbl != nullptr) {
    lv_label_set_text(s_log_info_lbl, "Falha ao limpar log.");
  }
}

static void settings_sd_format_status_refresh(const char *msg_override) {
  if (s_sd_format_status_lbl == nullptr) {
    return;
  }
  if (msg_override != nullptr) {
    lv_label_set_text(s_sd_format_status_lbl, msg_override);
    return;
  }
  if (SD.cardType() == CARD_NONE) {
    lv_label_set_text(s_sd_format_status_lbl, "Sem cartao SD montado. Insira um cartao FAT32/MBR.");
    return;
  }
  lv_label_set_text(s_sd_format_status_lbl,
                    "Pronto para formatar.\nA operacao recria o sistema FAT e apaga todos os ficheiros.");
}

static void settings_sd_format_arm_cb(lv_event_t *e) {
  (void)e;
  s_sd_format_armed = !s_sd_format_armed;
  if (s_sd_format_confirm_btn != nullptr) {
    lv_obj_t *lbl = lv_obj_get_child(s_sd_format_confirm_btn, 0);
    if (lbl != nullptr) {
      lv_label_set_text(lbl, s_sd_format_armed ? LV_SYMBOL_WARNING " Confirmar formatacao"
                                               : LV_SYMBOL_WARNING " Armar formatacao");
    }
  }
  settings_sd_format_status_refresh(
      s_sd_format_armed ? "Confirmacao armada. Toque em \"Executar\" para formatar o cartao."
                        : "Confirmacao desarmada.");
}

static void settings_sd_format_exec_cb(lv_event_t *e) {
  (void)e;
  if (!s_sd_format_armed) {
    settings_sd_format_status_refresh("Arme a confirmacao antes de executar.");
    return;
  }
  if (SD.cardType() == CARD_NONE) {
    settings_sd_format_status_refresh("Sem cartao SD montado.");
    return;
  }

  ui_loading_show(s_scr_settings, "A formatar cartao SD...");
  ui_loading_flush_display();
  net_services_set_ftp_suspended(true);

  const bool ok = SD.formatFAT();

  if (ok) {
    app_settings_sync_config_file_to_sd();
    ensure_main_content_browser();
    settings_log_refresh_view();
    settings_sd_format_status_refresh("Formatacao concluida com sucesso.");
    app_log_write("INFO", "Formatacao de SD concluida via UI.");
  } else {
    settings_sd_format_status_refresh("Falha na formatacao. Verifique cartao e logs.");
  }

  net_services_set_ftp_suspended(false);
  s_sd_format_armed = false;
  if (s_sd_format_confirm_btn != nullptr) {
    lv_obj_t *lbl = lv_obj_get_child(s_sd_format_confirm_btn, 0);
    if (lbl != nullptr) {
      lv_label_set_text(lbl, LV_SYMBOL_WARNING " Armar formatacao");
    }
  }
  ui_loading_hide();
}

static void settings_change_wifi_cb(lv_event_t *e) {
  (void)e;
  s_wifi_return_to_settings = true;
  lv_obj_clear_flag(s_wifi_back_btn, LV_OBJ_FLAG_HIDDEN);
  lv_textarea_set_text(s_ta_wifi_ssid, app_settings_wifi_ssid().c_str());
  lv_textarea_set_text(s_ta_wifi_pass, app_settings_wifi_pass().c_str());
  lv_scr_load(s_scr_wifi);
}

static void settings_back_cb(lv_event_t *e) {
  (void)e;
  lv_scr_load(s_scr_main);
}

/**
 * Atualiza campos ao voltar ao ecra de definicoes (valores NVS / estado atual).
 */
static void settings_screen_enter(void) {
  settings_hide_ftp_keyboard();
  settings_hide_time_keyboard();
  settings_hide_wg_keyboard();
  if (s_ta_ftp_user != nullptr) {
    lv_textarea_set_text(s_ta_ftp_user, app_settings_ftp_user().c_str());
  }
  if (s_ta_ftp_pass != nullptr) {
    lv_textarea_set_text(s_ta_ftp_pass, app_settings_ftp_pass().c_str());
  }
  if (s_font_slider != nullptr) {
    lv_slider_set_value(s_font_slider, app_settings_font_index(), LV_ANIM_OFF);
  }
  if (s_vnc_scale_slider != nullptr) {
    lv_slider_set_value(s_vnc_scale_slider, app_settings_vnc_scale(), LV_ANIM_OFF);
  }
  if (s_vnc_quality_slider != nullptr) {
    lv_slider_set_value(s_vnc_quality_slider, app_settings_vnc_jpeg_quality(), LV_ANIM_OFF);
  }
  if (s_vnc_interval_slider != nullptr) {
    lv_slider_set_value(s_vnc_interval_slider, app_settings_vnc_interval_ms(), LV_ANIM_OFF);
  }
  settings_vnc_refresh_value_labels();
  if (s_vnc_feedback_lbl != nullptr) {
    lv_label_set_text(s_vnc_feedback_lbl, "");
  }
  if (s_sw_ntp != nullptr) {
    if (app_settings_ntp_enabled()) {
      lv_obj_add_state(s_sw_ntp, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(s_sw_ntp, LV_STATE_CHECKED);
    }
  }
  if (s_ta_ntp_srv != nullptr) {
    lv_textarea_set_text(s_ta_ntp_srv, app_settings_ntp_server().c_str());
  }
  if (s_roller_tz != nullptr) {
    int32_t sec = app_settings_tz_offset_sec();
    int h = (int)(sec / 3600);
    if (h < -12) {
      h = -12;
    }
    if (h > 14) {
      h = 14;
    }
    lv_roller_set_selected(s_roller_tz, (uint16_t)(h + 12), LV_ANIM_OFF);
  }
  if (s_ta_utc_manual != nullptr) {
    lv_textarea_set_text(s_ta_utc_manual, "");
  }
  if (s_sw_wg != nullptr) {
    if (app_settings_wireguard_enabled()) {
      lv_obj_add_state(s_sw_wg, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(s_sw_wg, LV_STATE_CHECKED);
    }
  }
  if (s_ta_wg_ip != nullptr) {
    lv_textarea_set_text(s_ta_wg_ip, app_settings_wg_local_ip().c_str());
  }
  if (s_ta_wg_priv != nullptr) {
    lv_textarea_set_text(s_ta_wg_priv, app_settings_wg_private_key().c_str());
  }
  if (s_ta_wg_pub != nullptr) {
    lv_textarea_set_text(s_ta_wg_pub, app_settings_wg_peer_public_key().c_str());
  }
  if (s_ta_wg_ep != nullptr) {
    lv_textarea_set_text(s_ta_wg_ep, app_settings_wg_endpoint().c_str());
  }
  if (s_ta_wg_port != nullptr) {
    char pb[8];
    snprintf(pb, sizeof pb, "%u", (unsigned)app_settings_wg_port());
    lv_textarea_set_text(s_ta_wg_port, pb);
  }
  s_sd_format_armed = false;
  if (s_sd_format_confirm_btn != nullptr) {
    lv_obj_t *lbl = lv_obj_get_child(s_sd_format_confirm_btn, 0);
    if (lbl != nullptr) {
      lv_label_set_text(lbl, LV_SYMBOL_WARNING " Armar formatacao");
    }
  }
  settings_sd_format_status_refresh(nullptr);
  refresh_settings_wifi_label();
  refresh_settings_ftp_label();
  settings_log_refresh_view();
  update_bar_wifi_text();
}

static void refresh_settings_wifi_label(void) {
  if (s_sett_wifi_lbl == nullptr) {
    return;
  }
  char info[320];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(info, sizeof info,
             "Estado: ligado\n"
             "SSID: %s\n"
             "IP: %s\n"
             "RSSI: %d dBm",
             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    snprintf(info, sizeof info, "Estado: sem ligacao.\nUse o botao abaixo para configurar.");
  }
  lv_label_set_text(s_sett_wifi_lbl, info);
}

static void refresh_settings_ftp_label(void) {
  if (s_sett_ftp_info_lbl == nullptr) {
    return;
  }
  char info[384];
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(info, sizeof info,
             "Ligue o Wi-Fi (aba Wi-Fi) para usar o FTP.\n\n"
             "Predefinicao: %s / %s (max. 15 caracteres por campo).",
             kAppSettingsFtpDefaultUser, kAppSettingsFtpDefaultPass);
    lv_label_set_text(s_sett_ftp_info_lbl, info);
    return;
  }
  if (SD.cardType() == CARD_NONE) {
    snprintf(info, sizeof info,
             "E necessario cartao SD montado (FAT32).\n\n"
             "Predefinicao: %s / %s",
             kAppSettingsFtpDefaultUser, kAppSettingsFtpDefaultPass);
    lv_label_set_text(s_sett_ftp_info_lbl, info);
    return;
  }
  snprintf(info, sizeof info,
           "Host: %s  porta 21 (passivo)\n"
           "Predefinicao: %s / %s\n\n"
           "ftp://%s/\n"
           "Grave abaixo para aplicar utilizador e senha.",
           WiFi.localIP().toString().c_str(), kAppSettingsFtpDefaultUser, kAppSettingsFtpDefaultPass,
           WiFi.localIP().toString().c_str());
  lv_label_set_text(s_sett_ftp_info_lbl, info);
}

static void settings_hide_ftp_keyboard(void) {
  if (s_sett_ftp_kb == nullptr) {
    return;
  }
  lv_obj_add_flag(s_sett_ftp_kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(s_sett_ftp_kb, nullptr);
}

static void settings_hide_time_keyboard(void) {
  if (s_sett_time_kb == nullptr) {
    return;
  }
  lv_obj_add_flag(s_sett_time_kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(s_sett_time_kb, nullptr);
}

static void settings_hide_wg_keyboard(void) {
  if (s_sett_wg_kb == nullptr) {
    return;
  }
  lv_obj_add_flag(s_sett_wg_kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(s_sett_wg_kb, nullptr);
}

/** Indices usados para ocultar teclados ao trocar de aba. */
static constexpr uint32_t kSettingsTabFtp = 1;
static constexpr uint32_t kSettingsTabTime = 2;
static constexpr uint32_t kSettingsTabWg = 3;

/** Ao mudar de aba, esconde teclados dos formularios que deixam de estar visiveis. */
static void settings_tab_btns_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  lv_obj_t *btns = lv_event_get_target(e);
  const uint32_t sel = lv_btnmatrix_get_selected_btn(btns);
  if (sel != kSettingsTabFtp) {
    settings_hide_ftp_keyboard();
  }
  if (sel != kSettingsTabTime) {
    settings_hide_time_keyboard();
  }
  if (sel != kSettingsTabWg) {
    settings_hide_wg_keyboard();
  }
}

/** Permite navegar lateralmente entre abas com gesto de swipe. */
static void settings_tabview_swipe_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
    return;
  }
  lv_obj_t *tv = lv_event_get_target(e);
  lv_indev_t *indev = lv_indev_get_act();
  if (tv == nullptr || indev == nullptr) {
    return;
  }
  const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  const uint32_t cur = lv_tabview_get_tab_act(tv);
  lv_obj_t *content = lv_tabview_get_content(tv);
  const uint32_t total = content != nullptr ? lv_obj_get_child_cnt(content) : 0U;
  if (total == 0U) {
    return;
  }
  if (dir == LV_DIR_LEFT && (cur + 1U) < total) {
    lv_tabview_set_act(tv, cur + 1U, LV_ANIM_ON);
    lv_indev_wait_release(indev);
  } else if (dir == LV_DIR_RIGHT && cur > 0U) {
    lv_tabview_set_act(tv, cur - 1U, LV_ANIM_ON);
    lv_indev_wait_release(indev);
  }
}

/** Teclado FTP: visivel apenas com utilizador ou senha focados. */
static void settings_ftp_ta_kb_event_cb(lv_event_t *e) {
  if (s_sett_ftp_kb == nullptr) {
    return;
  }
  const lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(s_sett_ftp_kb, ta);
    lv_obj_clear_flag(s_sett_ftp_kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED) {
    settings_hide_ftp_keyboard();
  }
}

static void settings_save_ftp_cb(lv_event_t *e) {
  (void)e;
  if (s_ta_ftp_user == nullptr || s_ta_ftp_pass == nullptr) {
    return;
  }
  const char *u = lv_textarea_get_text(s_ta_ftp_user);
  const char *p = lv_textarea_get_text(s_ta_ftp_pass);
  if (u == nullptr || strlen(u) == 0) {
    if (s_ftp_feedback_lbl != nullptr) {
      lv_label_set_text(s_ftp_feedback_lbl, "Preencha o utilizador.");
    }
    return;
  }
  if (p == nullptr) {
    p = "";
  }
  app_settings_set_ftp(u, p);
  net_services_ftp_restart();
  if (s_ftp_feedback_lbl != nullptr) {
    lv_label_set_text(s_ftp_feedback_lbl, "Guardado. FTP a reiniciar.");
  }
}

static char s_tz_roller_buf[512];

static void build_tz_roller_options(void) {
  char *p = s_tz_roller_buf;
  const char *const end = s_tz_roller_buf + sizeof(s_tz_roller_buf);
  for (int h = -12; h <= 14; h++) {
    if (h > -12) {
      if (p >= end - 1) {
        break;
      }
      *p++ = '\n';
    }
    const int n = snprintf(p, (size_t)(end - p), "UTC%+d", h);
    if (n < 0 || (size_t)n >= (size_t)(end - p)) {
      break;
    }
    p += n;
  }
}

static void settings_time_ta_kb_event_cb(lv_event_t *e) {
  if (s_sett_time_kb == nullptr) {
    return;
  }
  const lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(s_sett_time_kb, ta);
    lv_obj_clear_flag(s_sett_time_kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED) {
    settings_hide_time_keyboard();
  }
}

static void settings_wg_ta_kb_event_cb(lv_event_t *e) {
  if (s_sett_wg_kb == nullptr) {
    return;
  }
  const lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(s_sett_wg_kb, ta);
    lv_obj_clear_flag(s_sett_wg_kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED) {
    settings_hide_wg_keyboard();
  }
}

static void settings_save_time_cb(lv_event_t *e) {
  (void)e;
  if (s_sw_ntp == nullptr || s_ta_ntp_srv == nullptr || s_roller_tz == nullptr || s_time_feedback_lbl == nullptr) {
    return;
  }
  const bool ntp_on = lv_obj_has_state(s_sw_ntp, LV_STATE_CHECKED);
  app_settings_set_ntp_enabled(ntp_on);
  const char *srv = lv_textarea_get_text(s_ta_ntp_srv);
  app_settings_set_ntp_server(srv ? srv : "");
  const uint16_t sel = lv_roller_get_selected(s_roller_tz);
  const int h = (int)sel - 12;
  app_settings_set_tz_offset_sec((int32_t)h * 3600);
  net_time_apply_settings();
  char msg[80];
  if (ntp_on && WiFi.status() == WL_CONNECTED) {
    ui_loading_show(s_scr_settings, "A sincronizar com o servidor NTP...");
    ui_loading_flush_display();
    (void)net_time_sync_now_blocking(msg, sizeof msg);
    ui_loading_hide();
  } else {
    snprintf(msg, sizeof msg, "Guardado (NTP %s).", ntp_on ? "ativo" : "inativo");
  }
  lv_label_set_text(s_time_feedback_lbl, msg);
  update_bar_wifi_text();
}

static void settings_apply_utc_cb(lv_event_t *e) {
  (void)e;
  if (s_ta_utc_manual == nullptr || s_time_feedback_lbl == nullptr) {
    return;
  }
  const char *t = lv_textarea_get_text(s_ta_utc_manual);
  if (t == nullptr) {
    return;
  }
  int y = 0;
  int M = 0;
  int d = 0;
  int h = 0;
  int m = 0;
  char err[64];
  if (sscanf(t, "%d-%d-%d %d:%d", &y, &M, &d, &h, &m) != 5 &&
      sscanf(t, "%d-%d-%dT%d:%d", &y, &M, &d, &h, &m) != 5) {
    lv_label_set_text(s_time_feedback_lbl, "Use AAAA-MM-DD HH:MM (UTC).");
    return;
  }
  if (!net_time_set_manual_utc(y, M, d, h, m, err, sizeof err)) {
    lv_label_set_text(s_time_feedback_lbl, err);
    return;
  }
  lv_label_set_text(s_time_feedback_lbl, "Hora UTC aplicada.");
  update_bar_wifi_text();
}

static void settings_save_wg_cb(lv_event_t *e) {
  (void)e;
  if (s_sw_wg == nullptr || s_wg_feedback_lbl == nullptr) {
    return;
  }
  app_settings_set_wireguard_enabled(lv_obj_has_state(s_sw_wg, LV_STATE_CHECKED));
  if (s_ta_wg_ip != nullptr) {
    app_settings_set_wg_local_ip(lv_textarea_get_text(s_ta_wg_ip));
  }
  if (s_ta_wg_priv != nullptr) {
    app_settings_set_wg_private_key(lv_textarea_get_text(s_ta_wg_priv));
  }
  if (s_ta_wg_pub != nullptr) {
    app_settings_set_wg_peer_public_key(lv_textarea_get_text(s_ta_wg_pub));
  }
  if (s_ta_wg_ep != nullptr) {
    app_settings_set_wg_endpoint(lv_textarea_get_text(s_ta_wg_ep));
  }
  if (s_ta_wg_port != nullptr) {
    const char *ps = lv_textarea_get_text(s_ta_wg_port);
    unsigned long pt = strtoul(ps ? ps : "51820", nullptr, 10);
    if (pt == 0 || pt > 65535UL) {
      pt = 51820;
    }
    app_settings_set_wg_port((uint16_t)pt);
  }
  net_wireguard_apply();
  lv_label_set_text(s_wg_feedback_lbl, "Guardado.");
}

/**
 * Ecra completo de definicoes: mesma altura de barra superior que o principal + abas.
 * So e criado uma vez; usar settings_screen_enter() ao lv_scr_load(s_scr_settings).
 */
static void create_settings_screen(void) {
  if (s_scr_settings != nullptr) {
    return;
  }

  auto fail_settings_create = []() {
    app_log_write("ERROR", "Sem memoria para construir ecra de definicoes.");
    if (s_scr_settings != nullptr) {
      lv_obj_del(s_scr_settings);
      s_scr_settings = nullptr;
    }
    s_settings_root = nullptr;
  };

  const lv_coord_t w = lv_disp_get_hor_res(nullptr);
  const lv_coord_t vh = lv_disp_get_ver_res(nullptr);

  s_scr_settings = lv_obj_create(nullptr);
  if (s_scr_settings == nullptr) {
    fail_settings_create();
    return;
  }
  lv_obj_set_size(s_scr_settings, w, vh);
  lv_obj_set_layout(s_scr_settings, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_scr_settings, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_scr_settings, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(s_scr_settings, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *bar = lv_obj_create(s_scr_settings);
  if (bar == nullptr) {
    fail_settings_create();
    return;
  }
  lv_obj_set_size(bar, w, kStatusBarH);
  lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(bar, 8, 0);
  lv_obj_set_style_pad_all(bar, 4, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  s_bar_settings_wifi_lbl = lv_label_create(bar);
  lv_label_set_long_mode(s_bar_settings_wifi_lbl, LV_LABEL_LONG_CLIP);
  lv_label_set_text(s_bar_settings_wifi_lbl, LV_SYMBOL_WIFI " ...");

  s_bar_settings_time_lbl = lv_label_create(bar);
  lv_obj_set_flex_grow(s_bar_settings_time_lbl, 1);
  lv_obj_set_style_text_align(s_bar_settings_time_lbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(s_bar_settings_time_lbl, LV_LABEL_LONG_CLIP);
  lv_label_set_text(s_bar_settings_time_lbl, "--/--/---- --:--");

  lv_obj_t *bt_back = lv_btn_create(bar);
  lv_obj_set_size(bt_back, 88, 36);
  lv_obj_t *lbl_back = lv_label_create(bt_back);
  lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Voltar");
  lv_obj_center(lbl_back);
  lv_obj_add_event_cb(bt_back, settings_back_cb, LV_EVENT_CLICKED, nullptr);

  s_settings_root = lv_obj_create(s_scr_settings);
  if (s_settings_root == nullptr) {
    fail_settings_create();
    return;
  }
  lv_obj_set_width(s_settings_root, w);
  lv_obj_set_flex_grow(s_settings_root, 1);
  lv_obj_set_layout(s_settings_root, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_settings_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_settings_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(s_settings_root, 10, 0);
  lv_obj_set_style_pad_row(s_settings_root, 8, 0);
  lv_obj_clear_flag(s_settings_root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(s_settings_root);
  lv_label_set_text(title, LV_SYMBOL_SETTINGS " Definicoes");

  lv_obj_t *tv = lv_tabview_create(s_settings_root, LV_DIR_TOP, 36);
  if (tv == nullptr) {
    fail_settings_create();
    return;
  }
  lv_obj_set_width(tv, LV_PCT(100));
  lv_obj_set_flex_grow(tv, 1);

  /* --- Aba Wi-Fi --- */
  lv_obj_t *tab_wifi = lv_tabview_add_tab(tv, LV_SYMBOL_WIFI " Wi-Fi");
  lv_obj_set_layout(tab_wifi, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab_wifi, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_wifi, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(tab_wifi, 6, 0);
  lv_obj_set_style_pad_row(tab_wifi, 8, 0);

  s_sett_wifi_lbl = lv_label_create(tab_wifi);
  lv_label_set_long_mode(s_sett_wifi_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_sett_wifi_lbl, LV_PCT(100));
  refresh_settings_wifi_label();

  lv_obj_t *bt_wifi = lv_btn_create(tab_wifi);
  lv_obj_t *lb1 = lv_label_create(bt_wifi);
  lv_label_set_text(lb1, LV_SYMBOL_WIFI " Alterar Wi-Fi");
  lv_obj_center(lb1);
  lv_obj_add_event_cb(bt_wifi, settings_change_wifi_cb, LV_EVENT_CLICKED, nullptr);

  /* --- Aba FTP --- */
  lv_obj_t *tab_ftp = lv_tabview_add_tab(tv, LV_SYMBOL_DRIVE " FTP");
  if (tab_ftp == nullptr) {
    fail_settings_create();
    return;
  }
  lv_obj_set_layout(tab_ftp, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab_ftp, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_ftp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(tab_ftp, 4, 0);
  lv_obj_set_style_pad_row(tab_ftp, 6, 0);

  lv_obj_t *ftp_scroll = lv_obj_create(tab_ftp);
  if (ftp_scroll == nullptr) {
    fail_settings_create();
    return;
  }
  lv_obj_set_width(ftp_scroll, LV_PCT(100));
  lv_obj_set_flex_grow(ftp_scroll, 1);
  lv_obj_set_layout(ftp_scroll, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(ftp_scroll, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(ftp_scroll, 4, 0);
  lv_obj_set_style_pad_row(ftp_scroll, 6, 0);
  lv_obj_set_scrollbar_mode(ftp_scroll, LV_SCROLLBAR_MODE_AUTO);

  s_sett_ftp_info_lbl = lv_label_create(ftp_scroll);
  lv_label_set_long_mode(s_sett_ftp_info_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_sett_ftp_info_lbl, LV_PCT(100));
  refresh_settings_ftp_label();

  lv_obj_t *lu = lv_label_create(ftp_scroll);
  lv_label_set_text(lu, "Utilizador FTP:");

  s_ta_ftp_user = lv_textarea_create(ftp_scroll);
  lv_textarea_set_one_line(s_ta_ftp_user, true);
  lv_textarea_set_max_length(s_ta_ftp_user, 15);
  lv_textarea_set_placeholder_text(s_ta_ftp_user, kAppSettingsFtpDefaultUser);
  lv_textarea_set_text(s_ta_ftp_user, app_settings_ftp_user().c_str());
  lv_obj_set_width(s_ta_ftp_user, LV_PCT(100));
  lv_obj_add_event_cb(s_ta_ftp_user, settings_ftp_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_ta_ftp_user, settings_ftp_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *lpf = lv_label_create(ftp_scroll);
  lv_label_set_text(lpf, "Senha FTP:");

  s_ta_ftp_pass = lv_textarea_create(ftp_scroll);
  lv_textarea_set_one_line(s_ta_ftp_pass, true);
  lv_textarea_set_password_mode(s_ta_ftp_pass, true);
  lv_textarea_set_max_length(s_ta_ftp_pass, 15);
  lv_textarea_set_placeholder_text(s_ta_ftp_pass, kAppSettingsFtpDefaultPass);
  lv_textarea_set_text(s_ta_ftp_pass, app_settings_ftp_pass().c_str());
  lv_obj_set_width(s_ta_ftp_pass, LV_PCT(100));
  lv_obj_add_event_cb(s_ta_ftp_pass, settings_ftp_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_ta_ftp_pass, settings_ftp_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *bt_save_ftp = lv_btn_create(ftp_scroll);
  lv_obj_t *lbs = lv_label_create(bt_save_ftp);
  lv_label_set_text(lbs, LV_SYMBOL_SAVE " Guardar FTP");
  lv_obj_center(lbs);
  lv_obj_add_event_cb(bt_save_ftp, settings_save_ftp_cb, LV_EVENT_CLICKED, nullptr);

  s_ftp_feedback_lbl = lv_label_create(ftp_scroll);
  lv_label_set_long_mode(s_ftp_feedback_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_ftp_feedback_lbl, LV_PCT(100));
  lv_label_set_text(s_ftp_feedback_lbl, "");

  s_sett_ftp_kb = lv_keyboard_create(tab_ftp);
  lv_obj_set_size(s_sett_ftp_kb, LV_PCT(100), (lv_coord_t)(vh * 30 / 100));
  lv_keyboard_set_mode(s_sett_ftp_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_add_flag(s_sett_ftp_kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(s_sett_ftp_kb, nullptr);

  /* --- Aba Data/Hora --- */
  lv_obj_t *tab_time = lv_tabview_add_tab(tv, LV_SYMBOL_LIST " Hora");
  lv_obj_set_layout(tab_time, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab_time, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_time, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(tab_time, 4, 0);
  lv_obj_set_style_pad_row(tab_time, 6, 0);

  lv_obj_t *time_scroll = lv_obj_create(tab_time);
  lv_obj_set_width(time_scroll, LV_PCT(100));
  lv_obj_set_flex_grow(time_scroll, 1);
  lv_obj_set_layout(time_scroll, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(time_scroll, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(time_scroll, 4, 0);
  lv_obj_set_style_pad_row(time_scroll, 6, 0);
  lv_obj_set_scrollbar_mode(time_scroll, LV_SCROLLBAR_MODE_AUTO);

  lv_obj_t *time_help = lv_label_create(time_scroll);
  lv_label_set_long_mode(time_help, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(time_help, LV_PCT(100));
  lv_label_set_text(time_help,
                     "NTP: servidor SNTP (Arduino configTime). Offset: horas face a UTC.\n"
                     "Hora manual: tempo universal (UTC), nao hora local.");

  lv_obj_t *ln = lv_label_create(time_scroll);
  lv_label_set_text(ln, "Ativar NTP:");
  s_sw_ntp = lv_switch_create(time_scroll);
  if (app_settings_ntp_enabled()) {
    lv_obj_add_state(s_sw_ntp, LV_STATE_CHECKED);
  }

  lv_obj_t *lsrv = lv_label_create(time_scroll);
  lv_label_set_text(lsrv, "Servidor NTP:");
  s_ta_ntp_srv = lv_textarea_create(time_scroll);
  lv_textarea_set_one_line(s_ta_ntp_srv, true);
  lv_textarea_set_max_length(s_ta_ntp_srv, 63);
  lv_textarea_set_text(s_ta_ntp_srv, app_settings_ntp_server().c_str());
  lv_obj_set_width(s_ta_ntp_srv, LV_PCT(100));
  lv_obj_add_event_cb(s_ta_ntp_srv, settings_time_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_ta_ntp_srv, settings_time_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *ltz = lv_label_create(time_scroll);
  lv_label_set_text(ltz, "Fuso (UTC):");
  build_tz_roller_options();
  s_roller_tz = lv_roller_create(time_scroll);
  lv_roller_set_options(s_roller_tz, s_tz_roller_buf, LV_ROLLER_MODE_NORMAL);
  lv_obj_set_height(s_roller_tz, 100);
  {
    int32_t sec = app_settings_tz_offset_sec();
    int h = (int)(sec / 3600);
    if (h < -12) {
      h = -12;
    }
    if (h > 14) {
      h = 14;
    }
    lv_roller_set_selected(s_roller_tz, (uint16_t)(h + 12), LV_ANIM_OFF);
  }

  lv_obj_t *bt_save_time = lv_btn_create(time_scroll);
  lv_obj_t *lbst = lv_label_create(bt_save_time);
  lv_label_set_text(lbst, LV_SYMBOL_SAVE " Guardar e sincronizar");
  lv_obj_center(lbst);
  lv_obj_add_event_cb(bt_save_time, settings_save_time_cb, LV_EVENT_CLICKED, nullptr);

  s_time_feedback_lbl = lv_label_create(time_scroll);
  lv_label_set_long_mode(s_time_feedback_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_time_feedback_lbl, LV_PCT(100));
  lv_label_set_text(s_time_feedback_lbl, "");

  lv_obj_t *lum = lv_label_create(time_scroll);
  lv_label_set_text(lum, "Hora manual (UTC, AAAA-MM-DD HH:MM):");
  s_ta_utc_manual = lv_textarea_create(time_scroll);
  lv_textarea_set_one_line(s_ta_utc_manual, true);
  lv_textarea_set_placeholder_text(s_ta_utc_manual, "2026-04-06 15:30");
  lv_obj_set_width(s_ta_utc_manual, LV_PCT(100));
  lv_obj_add_event_cb(s_ta_utc_manual, settings_time_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_ta_utc_manual, settings_time_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *bt_utc = lv_btn_create(time_scroll);
  lv_obj_t *lbu = lv_label_create(bt_utc);
  lv_label_set_text(lbu, LV_SYMBOL_OK " Aplicar hora UTC");
  lv_obj_center(lbu);
  lv_obj_add_event_cb(bt_utc, settings_apply_utc_cb, LV_EVENT_CLICKED, nullptr);

  s_sett_time_kb = lv_keyboard_create(tab_time);
  lv_obj_set_size(s_sett_time_kb, LV_PCT(100), (lv_coord_t)(vh * 28 / 100));
  lv_keyboard_set_mode(s_sett_time_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_add_flag(s_sett_time_kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(s_sett_time_kb, nullptr);

  /* --- Aba WireGuard (biblioteca ciniml/WireGuard-ESP32) --- */
  lv_obj_t *tab_wg = lv_tabview_add_tab(tv, LV_SYMBOL_LOOP " WG");
  lv_obj_set_layout(tab_wg, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab_wg, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_wg, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(tab_wg, 4, 0);
  lv_obj_set_style_pad_row(tab_wg, 6, 0);

  lv_obj_t *wg_scroll = lv_obj_create(tab_wg);
  lv_obj_set_width(wg_scroll, LV_PCT(100));
  lv_obj_set_flex_grow(wg_scroll, 1);
  lv_obj_set_layout(wg_scroll, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(wg_scroll, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(wg_scroll, 4, 0);
  lv_obj_set_style_pad_row(wg_scroll, 6, 0);
  lv_obj_set_scrollbar_mode(wg_scroll, LV_SCROLLBAR_MODE_AUTO);

  lv_obj_t *wg_help = lv_label_create(wg_scroll);
  lv_label_set_long_mode(wg_help, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(wg_help, LV_PCT(100));
  lv_label_set_text(wg_help,
                    "VPN WireGuard (lib ciniml/WireGuard-ESP32). Exige Wi-Fi e hora valida (NTP). "
                    "Chaves em Base64 (formato wg).");

  lv_obj_t *lwg = lv_label_create(wg_scroll);
  lv_label_set_text(lwg, "Ativar tunel:");
  s_sw_wg = lv_switch_create(wg_scroll);
  if (app_settings_wireguard_enabled()) {
    lv_obj_add_state(s_sw_wg, LV_STATE_CHECKED);
  }

  lv_obj_t *lwip = lv_label_create(wg_scroll);
  lv_label_set_text(lwip, "IP local (tunel):");
  s_ta_wg_ip = lv_textarea_create(wg_scroll);
  lv_textarea_set_one_line(s_ta_wg_ip, true);
  lv_textarea_set_max_length(s_ta_wg_ip, 19);
  lv_textarea_set_text(s_ta_wg_ip, app_settings_wg_local_ip().c_str());
  lv_obj_set_width(s_ta_wg_ip, LV_PCT(100));
  lv_obj_add_event_cb(s_ta_wg_ip, settings_wg_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_ta_wg_ip, settings_wg_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *lpk = lv_label_create(wg_scroll);
  lv_label_set_text(lpk, "Chave privada (cliente):");
  s_ta_wg_priv = lv_textarea_create(wg_scroll);
  lv_textarea_set_one_line(s_ta_wg_priv, true);
  lv_textarea_set_max_length(s_ta_wg_priv, 127);
  lv_textarea_set_text(s_ta_wg_priv, app_settings_wg_private_key().c_str());
  lv_obj_set_width(s_ta_wg_priv, LV_PCT(100));
  lv_obj_add_event_cb(s_ta_wg_priv, settings_wg_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_ta_wg_priv, settings_wg_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *lpub = lv_label_create(wg_scroll);
  lv_label_set_text(lpub, "Chave publica do servidor:");
  s_ta_wg_pub = lv_textarea_create(wg_scroll);
  lv_textarea_set_one_line(s_ta_wg_pub, true);
  lv_textarea_set_max_length(s_ta_wg_pub, 127);
  lv_textarea_set_text(s_ta_wg_pub, app_settings_wg_peer_public_key().c_str());
  lv_obj_set_width(s_ta_wg_pub, LV_PCT(100));
  lv_obj_add_event_cb(s_ta_wg_pub, settings_wg_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_ta_wg_pub, settings_wg_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *lep = lv_label_create(wg_scroll);
  lv_label_set_text(lep, "Endpoint (hostname ou IP):");
  s_ta_wg_ep = lv_textarea_create(wg_scroll);
  lv_textarea_set_one_line(s_ta_wg_ep, true);
  lv_textarea_set_max_length(s_ta_wg_ep, 127);
  lv_textarea_set_text(s_ta_wg_ep, app_settings_wg_endpoint().c_str());
  lv_obj_set_width(s_ta_wg_ep, LV_PCT(100));
  lv_obj_add_event_cb(s_ta_wg_ep, settings_wg_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_ta_wg_ep, settings_wg_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *lpt = lv_label_create(wg_scroll);
  lv_label_set_text(lpt, "Porta UDP:");
  s_ta_wg_port = lv_textarea_create(wg_scroll);
  lv_textarea_set_one_line(s_ta_wg_port, true);
  lv_textarea_set_max_length(s_ta_wg_port, 5);
  {
    char pb[8];
    snprintf(pb, sizeof pb, "%u", (unsigned)app_settings_wg_port());
    lv_textarea_set_text(s_ta_wg_port, pb);
  }
  lv_obj_set_width(s_ta_wg_port, LV_PCT(100));
  lv_obj_add_event_cb(s_ta_wg_port, settings_wg_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_ta_wg_port, settings_wg_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *bt_wg = lv_btn_create(wg_scroll);
  lv_obj_t *lbw = lv_label_create(bt_wg);
  lv_label_set_text(lbw, LV_SYMBOL_SAVE " Guardar WireGuard");
  lv_obj_center(lbw);
  lv_obj_add_event_cb(bt_wg, settings_save_wg_cb, LV_EVENT_CLICKED, nullptr);

  s_wg_feedback_lbl = lv_label_create(wg_scroll);
  lv_label_set_long_mode(s_wg_feedback_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_wg_feedback_lbl, LV_PCT(100));
  lv_label_set_text(s_wg_feedback_lbl, "");

  s_sett_wg_kb = lv_keyboard_create(tab_wg);
  lv_obj_set_size(s_sett_wg_kb, LV_PCT(100), (lv_coord_t)(vh * 28 / 100));
  lv_keyboard_set_mode(s_sett_wg_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_add_flag(s_sett_wg_kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(s_sett_wg_kb, nullptr);

  /* --- Aba VNC (Web Remote) --- */
  lv_obj_t *tab_vnc = lv_tabview_add_tab(tv, LV_SYMBOL_VIDEO " VNC");
  lv_obj_set_layout(tab_vnc, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab_vnc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_vnc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(tab_vnc, 6, 0);
  lv_obj_set_style_pad_row(tab_vnc, 8, 0);

  lv_obj_t *vnc_help = lv_label_create(tab_vnc);
  lv_label_set_long_mode(vnc_help, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(vnc_help, LV_PCT(100));
  lv_label_set_text(vnc_help,
                    "Ajusta o stream remoto via browser.\n"
                    "Scale maior reduz resolucao/custo. Qualidade menor reduz tamanho.\n"
                    "Intervalo menor aumenta FPS e uso de CPU/rede.");

  lv_obj_t *ls = lv_label_create(tab_vnc);
  lv_label_set_text(ls, "Scale (downscale):");
  s_vnc_scale_slider = lv_slider_create(tab_vnc);
  lv_obj_set_width(s_vnc_scale_slider, LV_PCT(100));
  lv_slider_set_range(s_vnc_scale_slider, 1, 8);
  lv_slider_set_value(s_vnc_scale_slider, app_settings_vnc_scale(), LV_ANIM_OFF);
  lv_obj_add_event_cb(s_vnc_scale_slider, settings_vnc_slider_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  s_vnc_scale_val_lbl = lv_label_create(tab_vnc);
  lv_label_set_text(s_vnc_scale_val_lbl, "");

  lv_obj_t *lq = lv_label_create(tab_vnc);
  lv_label_set_text(lq, "Qualidade JPEG:");
  s_vnc_quality_slider = lv_slider_create(tab_vnc);
  lv_obj_set_width(s_vnc_quality_slider, LV_PCT(100));
  lv_slider_set_range(s_vnc_quality_slider, 1, 100);
  lv_slider_set_value(s_vnc_quality_slider, app_settings_vnc_jpeg_quality(), LV_ANIM_OFF);
  lv_obj_add_event_cb(s_vnc_quality_slider, settings_vnc_slider_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  s_vnc_quality_val_lbl = lv_label_create(tab_vnc);
  lv_label_set_text(s_vnc_quality_val_lbl, "");

  lv_obj_t *li = lv_label_create(tab_vnc);
  lv_label_set_text(li, "Intervalo minimo entre frames (ms):");
  s_vnc_interval_slider = lv_slider_create(tab_vnc);
  lv_obj_set_width(s_vnc_interval_slider, LV_PCT(100));
  lv_slider_set_range(s_vnc_interval_slider, 80, 2000);
  lv_slider_set_value(s_vnc_interval_slider, app_settings_vnc_interval_ms(), LV_ANIM_OFF);
  lv_obj_add_event_cb(s_vnc_interval_slider, settings_vnc_slider_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  s_vnc_interval_val_lbl = lv_label_create(tab_vnc);
  lv_label_set_text(s_vnc_interval_val_lbl, "");

  lv_obj_t *bt_vnc = lv_btn_create(tab_vnc);
  lv_obj_t *lbv = lv_label_create(bt_vnc);
  lv_label_set_text(lbv, LV_SYMBOL_SAVE " Guardar VNC");
  lv_obj_center(lbv);
  lv_obj_add_event_cb(bt_vnc, settings_save_vnc_cb, LV_EVENT_CLICKED, nullptr);

  s_vnc_feedback_lbl = lv_label_create(tab_vnc);
  lv_label_set_long_mode(s_vnc_feedback_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_vnc_feedback_lbl, LV_PCT(100));
  lv_label_set_text(s_vnc_feedback_lbl, "");

  settings_vnc_refresh_value_labels();

  /* --- Aba Logs --- */
  lv_obj_t *tab_logs = lv_tabview_add_tab(tv, LV_SYMBOL_FILE " Logs");
  lv_obj_set_layout(tab_logs, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab_logs, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_logs, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(tab_logs, 6, 0);
  lv_obj_set_style_pad_row(tab_logs, 8, 0);

  lv_obj_t *logs_help = lv_label_create(tab_logs);
  lv_label_set_long_mode(logs_help, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(logs_help, LV_PCT(100));
  lv_label_set_text_fmt(logs_help, "Arquivo: /sd%s (rotativo, max %u MB)", app_log_path(),
                        (unsigned)(app_log_max_size_bytes() / (1024U * 1024U)));

  lv_obj_t *logs_btn_row = lv_obj_create(tab_logs);
  lv_obj_set_width(logs_btn_row, LV_PCT(100));
  lv_obj_set_height(logs_btn_row, LV_SIZE_CONTENT);
  lv_obj_set_layout(logs_btn_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(logs_btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(logs_btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(logs_btn_row, 4, 0);
  lv_obj_set_style_pad_column(logs_btn_row, 8, 0);
  lv_obj_clear_flag(logs_btn_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *bt_log_refresh = lv_btn_create(logs_btn_row);
  lv_obj_t *lbl_log_refresh = lv_label_create(bt_log_refresh);
  lv_label_set_text(lbl_log_refresh, LV_SYMBOL_REFRESH " Atualizar");
  lv_obj_center(lbl_log_refresh);
  lv_obj_add_event_cb(bt_log_refresh, settings_log_refresh_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *bt_log_clear = lv_btn_create(logs_btn_row);
  lv_obj_t *lbl_log_clear = lv_label_create(bt_log_clear);
  lv_label_set_text(lbl_log_clear, LV_SYMBOL_TRASH " Limpar");
  lv_obj_center(lbl_log_clear);
  lv_obj_add_event_cb(bt_log_clear, settings_log_clear_cb, LV_EVENT_CLICKED, nullptr);

  s_log_info_lbl = lv_label_create(tab_logs);
  lv_label_set_long_mode(s_log_info_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_log_info_lbl, LV_PCT(100));
  lv_label_set_text(s_log_info_lbl, "");

  s_log_textarea = lv_textarea_create(tab_logs);
  lv_obj_set_width(s_log_textarea, LV_PCT(100));
  lv_obj_set_flex_grow(s_log_textarea, 1);
  lv_textarea_set_one_line(s_log_textarea, false);
  lv_textarea_set_text(s_log_textarea, "(A carregar...)");
  lv_obj_set_scrollbar_mode(s_log_textarea, LV_SCROLLBAR_MODE_AUTO);

  settings_log_refresh_view();

  /* --- Aba SD (formatacao) --- */
  lv_obj_t *tab_sd = lv_tabview_add_tab(tv, LV_SYMBOL_DRIVE " SD");
  lv_obj_set_layout(tab_sd, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab_sd, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_sd, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(tab_sd, 6, 0);
  lv_obj_set_style_pad_row(tab_sd, 8, 0);

  lv_obj_t *sd_help = lv_label_create(tab_sd);
  lv_label_set_long_mode(sd_help, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(sd_help, LV_PCT(100));
  lv_label_set_text(sd_help,
                    "Formato: FAT (FatFs).\n"
                    "A operacao apaga todos os ficheiros do cartao.\n"
                    "Recomendado: cartao em tabela MBR.");

  s_sd_format_confirm_btn = lv_btn_create(tab_sd);
  lv_obj_t *sd_arm_lbl = lv_label_create(s_sd_format_confirm_btn);
  lv_label_set_text(sd_arm_lbl, LV_SYMBOL_WARNING " Armar formatacao");
  lv_obj_center(sd_arm_lbl);
  lv_obj_add_event_cb(s_sd_format_confirm_btn, settings_sd_format_arm_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *sd_exec_btn = lv_btn_create(tab_sd);
  lv_obj_t *sd_exec_lbl = lv_label_create(sd_exec_btn);
  lv_label_set_text(sd_exec_lbl, LV_SYMBOL_TRASH " Executar formatacao");
  lv_obj_center(sd_exec_lbl);
  lv_obj_add_event_cb(sd_exec_btn, settings_sd_format_exec_cb, LV_EVENT_CLICKED, nullptr);

  s_sd_format_status_lbl = lv_label_create(tab_sd);
  lv_label_set_long_mode(s_sd_format_status_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_sd_format_status_lbl, LV_PCT(100));
  settings_sd_format_status_refresh(nullptr);

  /* --- Aba Aspeto --- */
  lv_obj_t *tab_ui = lv_tabview_add_tab(tv, LV_SYMBOL_IMAGE " Font");
  lv_obj_set_layout(tab_ui, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab_ui, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_ui, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(tab_ui, 6, 0);
  lv_obj_set_style_pad_row(tab_ui, 8, 0);

  lv_obj_t *font_lbl = lv_label_create(tab_ui);
  lv_label_set_text(font_lbl, "Tamanho da fonte (interface):");

  s_font_slider = lv_slider_create(tab_ui);
  lv_obj_set_width(s_font_slider, LV_PCT(100));
  lv_slider_set_range(s_font_slider, 0, 3);
  lv_slider_set_value(s_font_slider, app_settings_font_index(), LV_ANIM_OFF);
  lv_obj_add_event_cb(s_font_slider, settings_font_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *sett_tab_btns = lv_tabview_get_tab_btns(tv);
  lv_obj_add_event_cb(sett_tab_btns, settings_tab_btns_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(tv, settings_tabview_swipe_cb, LV_EVENT_GESTURE, nullptr);

  ui_apply_font_everywhere();
}

static void create_main_screen(void) {
  const lv_coord_t w = lv_disp_get_hor_res(nullptr);
  const lv_coord_t h = lv_disp_get_ver_res(nullptr);

  s_scr_main = lv_obj_create(nullptr);
  lv_obj_set_size(s_scr_main, w, h);
  lv_obj_set_layout(s_scr_main, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_scr_main, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_scr_main, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(s_scr_main, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *bar = lv_obj_create(s_scr_main);
  lv_obj_set_size(bar, w, kStatusBarH);
  lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(bar, 8, 0);
  lv_obj_set_style_pad_all(bar, 4, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  s_bar_wifi_lbl = lv_label_create(bar);
  lv_label_set_long_mode(s_bar_wifi_lbl, LV_LABEL_LONG_CLIP);
  lv_label_set_text(s_bar_wifi_lbl, LV_SYMBOL_WIFI " ...");

  s_bar_time_lbl = lv_label_create(bar);
  lv_obj_set_flex_grow(s_bar_time_lbl, 1);
  lv_obj_set_style_text_align(s_bar_time_lbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(s_bar_time_lbl, LV_LABEL_LONG_CLIP);
  lv_label_set_text(s_bar_time_lbl, "--/--/---- --:--");

  lv_obj_t *gear = lv_btn_create(bar);
  lv_obj_set_size(gear, 40, 36);
  lv_obj_t *gl = lv_label_create(gear);
  lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
  lv_obj_center(gl);
  lv_obj_add_event_cb(gear, gear_btn_cb, LV_EVENT_CLICKED, nullptr);

  s_main_content = lv_obj_create(s_scr_main);
  lv_obj_set_width(s_main_content, w);
  lv_obj_set_flex_grow(s_main_content, 1);
  lv_obj_clear_flag(s_main_content, LV_OBJ_FLAG_SCROLLABLE);
}

static void create_wifi_screen(void) {
  const lv_coord_t w = lv_disp_get_hor_res(nullptr);
  const lv_coord_t h = lv_disp_get_ver_res(nullptr);

  s_scr_wifi = lv_obj_create(nullptr);
  lv_obj_set_size(s_scr_wifi, w, h);
  lv_obj_set_layout(s_scr_wifi, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_scr_wifi, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_scr_wifi, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(s_scr_wifi, 8, 0);
  lv_obj_set_style_pad_row(s_scr_wifi, 6, 0);

  lv_obj_t *hdr = lv_label_create(s_scr_wifi);
  lv_label_set_text(hdr, "Configuracao Wi-Fi");

  s_wifi_back_btn = lv_btn_create(s_scr_wifi);
  lv_obj_t *bl = lv_label_create(s_wifi_back_btn);
  lv_label_set_text(bl, LV_SYMBOL_LEFT " Voltar");
  lv_obj_center(bl);
  lv_obj_add_flag(s_wifi_back_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(s_wifi_back_btn, wifi_back_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *ls = lv_label_create(s_scr_wifi);
  lv_label_set_text(ls, "Nome da rede (SSID):");

  s_ta_wifi_ssid = lv_textarea_create(s_scr_wifi);
  lv_textarea_set_one_line(s_ta_wifi_ssid, true);
  lv_textarea_set_placeholder_text(s_ta_wifi_ssid, "SSID");
  lv_obj_set_width(s_ta_wifi_ssid, LV_PCT(100));
  lv_obj_add_event_cb(s_ta_wifi_ssid, wifi_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_ta_wifi_ssid, wifi_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *lp = lv_label_create(s_scr_wifi);
  lv_label_set_text(lp, "Senha:");

  s_ta_wifi_pass = lv_textarea_create(s_scr_wifi);
  lv_textarea_set_one_line(s_ta_wifi_pass, true);
  lv_textarea_set_password_mode(s_ta_wifi_pass, true);
  lv_textarea_set_placeholder_text(s_ta_wifi_pass, "Senha Wi-Fi");
  lv_obj_set_width(s_ta_wifi_pass, LV_PCT(100));
  lv_obj_add_event_cb(s_ta_wifi_pass, wifi_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_ta_wifi_pass, wifi_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *bt = lv_btn_create(s_scr_wifi);
  lv_obj_t *btl = lv_label_create(bt);
  lv_label_set_text(btl, LV_SYMBOL_OK " Guardar e ligar");
  lv_obj_center(btl);
  lv_obj_add_event_cb(bt, wifi_save_connect_cb, LV_EVENT_CLICKED, nullptr);

  s_wifi_status_lbl = lv_label_create(s_scr_wifi);
  lv_label_set_long_mode(s_wifi_status_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_wifi_status_lbl, LV_PCT(100));

  s_wifi_kb = lv_keyboard_create(s_scr_wifi);
  lv_obj_set_size(s_wifi_kb, LV_PCT(100), (lv_coord_t)(h * 35 / 100));
  lv_obj_add_flag(s_wifi_kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(s_wifi_kb, nullptr);
}

void ui_app_run(bool sd_mounted) {
  s_sd_at_boot = sd_mounted;
  app_settings_init();
  /** Com SD montado: importa fdigi.cfg se valido; senao cria/atualiza ficheiro com a NVS actual. */
  if (sd_mounted) {
    if (!app_settings_try_load_from_sd_config()) {
      app_settings_sync_config_file_to_sd();
    }
  }

  create_main_screen();
  create_wifi_screen();

  if (s_status_timer == nullptr) {
    s_status_timer = lv_timer_create(status_timer_cb, 1000, nullptr);
  }

  const bool wifi_saved = app_settings_wifi_configured();
  if (wifi_saved) {
    net_wifi_begin_saved();
  }

  /* Com SD montado, abrir sempre o explorador no fim do boot (Wi-Fi nas definições). */
  const bool show_main_first = wifi_saved || sd_mounted;
  if (show_main_first) {
    lv_scr_load(s_scr_main);
    boot_screen_destroy();
    ensure_main_content_browser();
    /* Não usar lv_refr_now aqui: ui_app_run corre com lvgl_port_lock (setup) e em RGB pode
     * bloquear em VSYNC, impedindo a tarefa LVGL de correr — ecrã/toque ficam congelados. */
  } else {
    lv_scr_load(s_scr_wifi);
    boot_screen_destroy();
    lv_textarea_set_text(s_ta_wifi_ssid, "");
    lv_textarea_set_text(s_ta_wifi_pass, "");
    lv_label_set_text(s_wifi_status_lbl, "Introduza a rede e a senha. Toque num campo para o teclado.");
  }

  ui_apply_font_everywhere();
  update_bar_wifi_text();
}
