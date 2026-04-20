/**
 * @file ui_app.cpp
 * @brief Ecra Wi-Fi (primeiro), principal com barra, definicoes, explorador SD; portal HTTP continua disponivel na rede.
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
#include "cycles_rs485.h"
#include "app_log.h"
#include "boot_screen.h"
#include "splash_screen.h"
#include "file_browser.h"
#include "lvgl_port_v8.h"
#include "cycles_rs485.h"
#include "net_monitor.h"
#include "net_services.h"
#include "sd_access.h"
#include "net_time.h"
#include "net_wireguard.h"
#include "ui/ui_loading.h"

static constexpr int kStatusBarH = 46;

static lv_obj_t *s_scr_main = nullptr;
static lv_obj_t *s_scr_settings = nullptr;
static lv_obj_t *s_scr_wifi = nullptr;
/** Barra principal: oblongo de conectividade remota (Conectado/Desconectado/Configure). */
static lv_obj_t *s_bar_monitor_pill = nullptr;
/** Barra definicoes: oblongo de conectividade remota. */
static lv_obj_t *s_bar_settings_monitor_pill = nullptr;
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

/** Aba Wi-Fi (em Definicoes): campo "IP de monitorizacao" + teclado partilhado. */
static lv_obj_t *s_ta_mon_ip = nullptr;
static lv_obj_t *s_sett_wifi_kb = nullptr;
static lv_obj_t *s_mon_feedback_lbl = nullptr;

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
/** Aba RS485: velocidade e trama UART (NVS + reinicio da Serial1). */
static lv_obj_t *s_rs485_roller_baud = nullptr;
static lv_obj_t *s_rs485_roller_frame = nullptr;
static lv_obj_t *s_rs485_feedback_lbl = nullptr;
static lv_obj_t *s_log_textarea = nullptr;
static lv_obj_t *s_log_info_lbl = nullptr;
static lv_obj_t *s_sd_format_status_lbl = nullptr;
static lv_obj_t *s_sd_format_confirm_btn = nullptr;
static bool s_sd_format_armed = false;

static lv_timer_t *s_status_timer = nullptr;

/** Vista corrente dentro de s_main_content: dashboard ou explorador de ficheiros. */
enum class MainView : uint8_t { Dashboard, FileBrowser };
static MainView s_main_view = MainView::Dashboard;
/** Botao "Home" na status bar (so visivel quando a vista e' o explorador). */
static lv_obj_t *s_bar_home_btn = nullptr;

/** Labels dinamicos do dashboard; actualizados por `dashboard_refresh_values`. */
static lv_obj_t *s_dash_rs485_cfg = nullptr;   /**< "9600 8N1" */
static lv_obj_t *s_dash_rs485_last = nullptr;  /**< "Ultima: HH:MM:SS" */
static lv_obj_t *s_dash_today_lines = nullptr; /**< "42 linhas gravadas" */
static lv_obj_t *s_dash_today_last = nullptr;  /**< "Ultima: HH:MM:SS" */
static lv_obj_t *s_dash_sd_value = nullptr;
static lv_obj_t *s_dash_ntp_value = nullptr;
static lv_obj_t *s_dash_ftp_value = nullptr;
static lv_obj_t *s_dash_wifi_value = nullptr;

static bool s_sd_at_boot = false;

static void refresh_settings_wifi_label(void);
static void refresh_settings_ftp_label(void);
static void settings_hide_wifi_keyboard(void);
static void settings_hide_ftp_keyboard(void);
static void settings_hide_time_keyboard(void);
static void settings_hide_wg_keyboard(void);
static void settings_sd_format_status_refresh(const char *msg_override);
static void settings_rs485_sync_ui_from_settings(void);

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

/**
 * Cria o oblongo (pill) central da status bar com o rotulo do estado de
 * conectividade. O tamanho e' ~1/5 da largura do ecra; flutua sobre o flex
 * do pai (posicao absoluta no centro, nao interfere com os filhos laterais).
 */
static lv_obj_t *create_monitor_pill(lv_obj_t *parent) {
  if (parent == nullptr) {
    return nullptr;
  }
  const lv_coord_t scr_w = lv_disp_get_hor_res(nullptr);
  const lv_coord_t pill_w = scr_w / 5;
  const lv_coord_t pill_h = 30;

  lv_obj_t *p = lv_obj_create(parent);
  if (p == nullptr) {
    return nullptr;
  }
  lv_obj_set_size(p, pill_w, pill_h);
  lv_obj_set_style_radius(p, pill_h / 2, 0); /* cantos totalmente arredondados */
  lv_obj_set_style_border_width(p, 0, 0);
  lv_obj_set_style_pad_all(p, 0, 0);
  lv_obj_set_style_bg_color(p, lv_color_hex(0x808080), 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  /* Fora do fluxo flex do pai: centralizado absolutamente no meio da barra. */
  lv_obj_add_flag(p, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(p, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *lbl = lv_label_create(p);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(lbl, "Configure");
  lv_obj_center(lbl);
  /* O label guarda-se em user_data para `update_monitor_pill` o alcancar. */
  lv_obj_set_user_data(p, lbl);
  return p;
}

/** Callback de animacao: varia a opacidade do fundo do oblongo (respiracao). */
static void monitor_pill_bg_opa_exec(void *var, int32_t v) {
  lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(var), static_cast<lv_opa_t>(v), 0);
}

/**
 * Liga/desliga a animacao de respiracao (800 ms por ciclo, ease-in-out) para
 * o oblongo. Idempotente: chamar repetidamente com `on=true` nao reinicia a
 * animacao (evita flickering a cada tick do timer de status).
 */
static void monitor_pill_set_breathing(lv_obj_t *pill, bool on) {
  if (pill == nullptr) {
    return;
  }
  const bool running = (lv_anim_get(pill, monitor_pill_bg_opa_exec) != nullptr);
  if (on && !running) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, pill);
    lv_anim_set_exec_cb(&a, monitor_pill_bg_opa_exec);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_30);
    lv_anim_set_time(&a, 400);          /* 400 ms de ida */
    lv_anim_set_playback_time(&a, 400); /* 400 ms de volta (total 800 ms) */
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  } else if (!on && running) {
    lv_anim_del(pill, monitor_pill_bg_opa_exec);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  }
}

/**
 * Atualiza cor de fundo e texto do oblongo conforme `net_monitor_status()`.
 * Sempre visivel: quando desligado mostra "Configure" como call-to-action.
 * Estado "Desconectado" ganha animacao de respiracao (opacidade pulsa).
 */
static void update_monitor_pill(lv_obj_t *pill) {
  if (pill == nullptr) {
    return;
  }
  lv_obj_t *lbl = static_cast<lv_obj_t *>(lv_obj_get_user_data(pill));
  const NetMonitorStatus st = net_monitor_status();
  uint32_t color = 0x808080u;
  const char *text = "Configure";
  switch (st) {
    case NetMonitorStatus::Ok:
      color = 0x2E7D32u; /* verde escuro */
      text = "Conectado";
      break;
    case NetMonitorStatus::Fail:
      color = 0xC62828u; /* vermelho industrial */
      text = "Desconectado";
      break;
    case NetMonitorStatus::Pending:
      color = 0xF5B841u; /* ambar */
      text = "A testar...";
      break;
    case NetMonitorStatus::Disabled:
    default:
      color = 0x808080u; /* cinza neutro */
      text = "Configure";
      break;
  }
  lv_obj_set_style_bg_color(pill, lv_color_hex(color), 0);
  if (lbl != nullptr) {
    lv_label_set_text(lbl, text);
  }
  monitor_pill_set_breathing(pill, st == NetMonitorStatus::Fail);
}

static void update_bar_wifi_text(void) {
  bar_label_set_signal_only(s_bar_wifi_lbl);
  bar_label_set_date_time(s_bar_time_lbl);
  bar_label_set_signal_only(s_bar_settings_wifi_lbl);
  bar_label_set_date_time(s_bar_settings_time_lbl);
  update_monitor_pill(s_bar_monitor_pill);
  update_monitor_pill(s_bar_settings_monitor_pill);
}

static void dashboard_refresh_values(void);

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
  if (s_scr_main != nullptr && lv_disp_get_scr_act(nullptr) == s_scr_main) {
    if (s_main_view == MainView::Dashboard) {
      dashboard_refresh_values();
    } else {
      const uint32_t sd_mod = sd_access_last_modified_ms();
      const uint32_t ui_ref = file_browser_last_refresh_ms();
      if (!file_browser_should_skip_auto_list_refresh() && sd_mod != 0 &&
          (ui_ref == 0 || (int32_t)(sd_mod - ui_ref) > 0)) {
        file_browser_refresh_silent();
      }
    }
  }
}

/** Se true, o ecra Wi-Fi volta para definicoes em vez do principal. */
static bool s_wifi_return_to_settings = false;

static void create_settings_screen(void);
static void settings_screen_enter(void);
static void settings_back_cb(lv_event_t *e);

/** Constroi um cartao do dashboard; devolve o ponteiro ao label de valor para refresh. */
static lv_obj_t *dashboard_make_card(lv_obj_t *row, const char *icon, const char *title) {
  lv_obj_t *card = lv_obj_create(row);
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0xFAFAFA), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_set_style_pad_row(card, 2, 0);
  lv_obj_set_layout(card, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *hdr = lv_label_create(card);
  lv_label_set_text_fmt(hdr, "%s %s", icon, title);
  lv_obj_set_style_text_color(hdr, lv_color_hex(0x449D48), 0); /* cor primaria */

  lv_obj_t *val = lv_label_create(card);
  lv_label_set_long_mode(val, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(val, LV_PCT(100));
  lv_label_set_text(val, "--");
  return val;
}

/** Cria dois labels num cartao: retorna ambos. Usado em cartoes com 2 linhas dinamicas. */
static lv_obj_t *dashboard_make_card_twoline(lv_obj_t *row, const char *icon, const char *title,
                                              lv_obj_t **extra_out) {
  lv_obj_t *primary = dashboard_make_card(row, icon, title);
  lv_obj_t *card = lv_obj_get_parent(primary);
  lv_obj_t *extra = lv_label_create(card);
  lv_label_set_long_mode(extra, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(extra, LV_PCT(100));
  lv_obj_set_style_text_color(extra, lv_color_hex(0x606060), 0);
  lv_label_set_text(extra, "--");
  if (extra_out != nullptr) {
    *extra_out = extra;
  }
  return primary;
}

static void dashboard_btn_today_cb(lv_event_t *e);
static void dashboard_btn_history_cb(lv_event_t *e);
static void dashboard_btn_home_cb(lv_event_t *e);
static void dashboard_refresh_values(void);
static void ensure_main_content_browser(void);

/**
 * Constroi o dashboard dentro de `parent` (s_main_content ja' limpo).
 * Layout: 3 linhas de 2 cartoes + linha de botoes de acao.
 */
static void dashboard_build(lv_obj_t *parent) {
  if (parent == nullptr) {
    return;
  }
  lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(parent, 8, 0);
  lv_obj_set_style_pad_bottom(parent, 5, 0); /* aproxima o rodape da borda inferior */
  lv_obj_set_style_pad_row(parent, 8, 0);

  auto make_row = [parent]() {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
  };

  lv_obj_t *row1 = make_row();
  s_dash_rs485_cfg = dashboard_make_card_twoline(row1, LV_SYMBOL_LOOP, "Captura RS485", &s_dash_rs485_last);
  s_dash_today_lines = dashboard_make_card_twoline(row1, LV_SYMBOL_FILE, "Hoje", &s_dash_today_last);

  lv_obj_t *row2 = make_row();
  s_dash_sd_value = dashboard_make_card(row2, LV_SYMBOL_SD_CARD, "SD");
  s_dash_ntp_value = dashboard_make_card(row2, LV_SYMBOL_BELL, "NTP");

  lv_obj_t *row3 = make_row();
  s_dash_ftp_value = dashboard_make_card(row3, LV_SYMBOL_DRIVE, "FTP");
  s_dash_wifi_value = dashboard_make_card(row3, LV_SYMBOL_WIFI, "Wi-Fi");

  /* Linha de acoes. */
  lv_obj_t *actions = lv_obj_create(parent);
  lv_obj_set_width(actions, LV_PCT(100));
  lv_obj_set_height(actions, LV_SIZE_CONTENT);
  lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(actions, 4, 0);
  lv_obj_set_style_pad_column(actions, 8, 0);
  lv_obj_set_style_border_width(actions, 0, 0);
  lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *btn_today = lv_btn_create(actions);
  lv_obj_set_height(btn_today, 102);
  lv_obj_set_flex_grow(btn_today, 1);
  lv_obj_set_style_bg_color(btn_today, lv_color_hex(0x449D48), 0);
  lv_obj_t *lbl_today = lv_label_create(btn_today);
  lv_label_set_text(lbl_today, LV_SYMBOL_EYE_OPEN " Abrir ciclo de hoje");
  lv_obj_set_style_text_font(lbl_today, &lv_font_montserrat_20, 0);
  lv_obj_center(lbl_today);
  lv_obj_add_event_cb(btn_today, dashboard_btn_today_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *btn_hist = lv_btn_create(actions);
  lv_obj_set_height(btn_hist, 102);
  lv_obj_set_flex_grow(btn_hist, 1);
  lv_obj_set_style_bg_color(btn_hist, lv_color_hex(0x449D48), 0);
  lv_obj_t *lbl_hist = lv_label_create(btn_hist);
  lv_label_set_text(lbl_hist, LV_SYMBOL_DIRECTORY " Ver historico");
  lv_obj_set_style_text_font(lbl_hist, &lv_font_montserrat_20, 0);
  lv_obj_center(lbl_hist);
  lv_obj_add_event_cb(btn_hist, dashboard_btn_history_cb, LV_EVENT_CLICKED, nullptr);

  /* Rodape: logo AFR (esquerda) + versao do software (direita). */
  LV_IMG_DECLARE(afr_logo_verde);
  lv_obj_t *footer = lv_obj_create(parent);
  lv_obj_set_width(footer, LV_PCT(100));
  lv_obj_set_height(footer, LV_SIZE_CONTENT);
  lv_obj_set_layout(footer, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(footer, 0, 0);
  lv_obj_set_style_border_width(footer, 0, 0);
  lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *logo = lv_img_create(footer);
  lv_img_set_src(logo, &afr_logo_verde);

  lv_obj_t *ver_lbl = lv_label_create(footer);
  lv_label_set_text(ver_lbl, "FITADIGITAL 1.02v");
  lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0x606060), 0);

  dashboard_refresh_values();
}

/** Invalida os ponteiros do dashboard (chamar antes de lv_obj_clean no s_main_content). */
static void dashboard_detach_stale_widgets(void) {
  s_dash_rs485_cfg = nullptr;
  s_dash_rs485_last = nullptr;
  s_dash_today_lines = nullptr;
  s_dash_today_last = nullptr;
  s_dash_sd_value = nullptr;
  s_dash_ntp_value = nullptr;
  s_dash_ftp_value = nullptr;
  s_dash_wifi_value = nullptr;
}

/** Formata "X.Y GB" ou "X MB" a partir de bytes (uso em cartao SD). */
static void human_bytes(uint64_t bytes, char *out, size_t out_sz) {
  if (bytes >= (uint64_t)1 << 30) {
    const double gb = (double)bytes / (double)((uint64_t)1 << 30);
    (void)snprintf(out, out_sz, "%.1f GB", gb);
  } else if (bytes >= (uint64_t)1 << 20) {
    const double mb = (double)bytes / (double)((uint64_t)1 << 20);
    (void)snprintf(out, out_sz, "%.0f MB", mb);
  } else if (bytes >= 1024U) {
    (void)snprintf(out, out_sz, "%u KB", (unsigned)(bytes >> 10));
  } else {
    (void)snprintf(out, out_sz, "%u B", (unsigned)bytes);
  }
}

static void dashboard_refresh_values(void) {
  char tmp[80];

  /* RS485: config + ultima linha */
  if (s_dash_rs485_cfg != nullptr) {
    static const char *const kFrames[] = {"8N1", "8E1", "8O1", "8N2", "7N1", "7E1", "7O1", "7N2"};
    const uint8_t fprof = app_settings_rs485_frame_profile();
    const char *fstr = (fprof < 8U) ? kFrames[fprof] : "?";
    (void)snprintf(tmp, sizeof(tmp), "%lu %s", (unsigned long)app_settings_rs485_baud(), fstr);
    lv_label_set_text(s_dash_rs485_cfg, tmp);
  }
  if (s_dash_rs485_last != nullptr) {
    char hms[16];
    cycles_rs485_last_write_hhmmss(hms, sizeof(hms));
    (void)snprintf(tmp, sizeof(tmp), "Ultima: %s", hms);
    lv_label_set_text(s_dash_rs485_last, tmp);
  }

  /* Hoje: linhas gravadas */
  if (s_dash_today_lines != nullptr) {
    (void)snprintf(tmp, sizeof(tmp), "%u linhas gravadas", (unsigned)cycles_rs485_today_line_count());
    lv_label_set_text(s_dash_today_lines, tmp);
  }
  if (s_dash_today_last != nullptr) {
    char hms[16];
    cycles_rs485_last_write_hhmmss(hms, sizeof(hms));
    (void)snprintf(tmp, sizeof(tmp), "Ultima: %s", hms);
    lv_label_set_text(s_dash_today_last, tmp);
  }

  /* SD */
  if (s_dash_sd_value != nullptr) {
    if (sd_access_is_mounted()) {
      const uint64_t total = SD.totalBytes();
      const uint64_t used = SD.usedBytes();
      const uint64_t free_b = (total > used) ? (total - used) : 0;
      const unsigned pct = total > 0 ? (unsigned)((used * 100U) / total) : 0;
      char free_str[24];
      human_bytes(free_b, free_str, sizeof(free_str));
      (void)snprintf(tmp, sizeof(tmp), "%s livres (%u%% usado)", free_str, pct);
      lv_label_set_text(s_dash_sd_value, tmp);
    } else {
      lv_label_set_text(s_dash_sd_value, "Nao montado");
    }
  }

  /* NTP */
  if (s_dash_ntp_value != nullptr) {
    if (!app_settings_ntp_enabled()) {
      lv_label_set_text(s_dash_ntp_value, "Desativado");
    } else if (net_time_is_valid()) {
      lv_label_set_text(s_dash_ntp_value, "Sincronizado");
    } else {
      lv_label_set_text(s_dash_ntp_value, "Sem sincronizacao");
    }
  }

  /* FTP */
  if (s_dash_ftp_value != nullptr) {
    const bool up = WiFi.isConnected();
    if (up && app_settings_ftp_user().length() > 0) {
      lv_label_set_text(s_dash_ftp_value, "Ativo - porta 21");
    } else if (!up) {
      lv_label_set_text(s_dash_ftp_value, "Inativo (sem Wi-Fi)");
    } else {
      lv_label_set_text(s_dash_ftp_value, "Nao configurado");
    }
  }

  /* Wi-Fi */
  if (s_dash_wifi_value != nullptr) {
    if (WiFi.isConnected()) {
      (void)snprintf(tmp, sizeof(tmp), "%s\n%s - %d dBm",
                     WiFi.SSID().c_str(),
                     WiFi.localIP().toString().c_str(),
                     (int)WiFi.RSSI());
      lv_label_set_text(s_dash_wifi_value, tmp);
    } else {
      lv_label_set_text(s_dash_wifi_value, "Desligado");
    }
  }
}

static void dashboard_btn_today_cb(lv_event_t *e) {
  (void)e;
  ensure_main_content_browser();
  file_browser_open_today_cycles_txt();
}

static void dashboard_btn_history_cb(lv_event_t *e) {
  (void)e;
  ensure_main_content_browser();
  file_browser_goto("/CICLOS");
}

static void dashboard_btn_home_cb(lv_event_t *e);

static void ensure_main_content_dashboard(void) {
  if (s_main_content == nullptr) {
    return;
  }
  ui_loading_hide();
  file_browser_detach_stale_widgets();
  dashboard_detach_stale_widgets();
  lv_obj_clean(s_main_content);
  dashboard_build(s_main_content);
  s_main_view = MainView::Dashboard;
  if (s_bar_home_btn != nullptr) {
    lv_obj_add_flag(s_bar_home_btn, LV_OBJ_FLAG_HIDDEN);
  }
  ui_apply_font_everywhere();
}

static void ensure_main_content_browser(void) {
  if (s_main_content == nullptr) {
    return;
  }
  /**
   * Ordem: esconder overlay de loading (ponteiro global em ui_loading), anular estaticos
   * do file_browser, depois clean — caso contrario lv_obj_del duplo ou ponteiro pendente
   * corrompe LVGL (ecra preso, touch morto, timers sem efeito).
   */
  ui_loading_hide();
  file_browser_detach_stale_widgets();
  dashboard_detach_stale_widgets();
  lv_obj_clean(s_main_content);
  /* s_main_content tinha flex column definido pelo dashboard; repoe valores default
   * para o file_browser criar o seu proprio layout a partir do zero (0 = no layout). */
  lv_obj_set_layout(s_main_content, 0);
  lv_obj_set_style_pad_all(s_main_content, 0, 0);
  lv_obj_set_style_pad_row(s_main_content, 0, 0);
  if (!(s_sd_at_boot && file_browser_init(s_main_content))) {
    lv_obj_t *msg = lv_label_create(s_main_content);
    lv_label_set_text(msg, s_sd_at_boot ? "Nao foi possivel listar o SD." : "SD nao montado.\nUse cartao FAT32 MBR.");
    lv_obj_center(msg);
  }
  s_main_view = MainView::FileBrowser;
  if (s_bar_home_btn != nullptr) {
    lv_obj_clear_flag(s_bar_home_btn, LV_OBJ_FLAG_HIDDEN);
  }
  ui_apply_font_everywhere();
}

static void dashboard_btn_home_cb(lv_event_t *e) {
  (void)e;
  ensure_main_content_dashboard();
}

static void enter_main_after_wifi_connected(void) {
  if (s_wifi_back_btn != nullptr) {
    lv_obj_add_flag(s_wifi_back_btn, LV_OBJ_FLAG_HIDDEN);
  }
  lv_scr_load(s_scr_main);
  ensure_main_content_dashboard();
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

/** Posiciona os rollers RS485 segundo a NVS (chamar ao entrar em definicoes). */
static void settings_rs485_sync_ui_from_settings(void) {
  if (s_rs485_roller_baud != nullptr) {
    const size_t idx = app_settings_rs485_std_baud_nearest_index(app_settings_rs485_baud());
    lv_roller_set_selected(s_rs485_roller_baud, (uint16_t)idx, LV_ANIM_OFF);
  }
  if (s_rs485_roller_frame != nullptr) {
    const uint16_t f = (uint16_t)app_settings_rs485_frame_profile();
    lv_roller_set_selected(s_rs485_roller_frame, f > 7U ? 0U : f, LV_ANIM_OFF);
  }
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
  if (!sd_access_is_mounted()) {
    lv_label_set_text(s_sd_format_status_lbl, "Sem cartao SD montado. Insira um cartao FAT32/MBR.");
    return;
  }
  lv_label_set_text(s_sd_format_status_lbl,
                    "Pronto para formatar.\nA operacao recria o sistema FAT e apaga todos os arquivos.");
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
  if (!sd_access_is_mounted()) {
    settings_sd_format_status_refresh("Sem cartao SD montado.");
    return;
  }

  ui_loading_show(s_scr_settings, "Formatando cartao SD...");
  ui_loading_flush_display();

  /** Sem libertar o mutex, a tarefa LVGL nao corre `lv_timer_handler` e o spinner fica estatico. */
  lvgl_port_unlock();

  bool ok = false;
  sd_access_sync([&] {
    net_services_set_ftp_suspended(true);
    ok = SD.formatFAT();
    if (ok) {
      app_settings_sync_config_file_to_sd();
    }
    net_services_set_ftp_suspended(false);
  });

  (void)lvgl_port_lock(-1);

  if (ok) {
    ensure_main_content_dashboard();
    settings_log_refresh_view();
    settings_sd_format_status_refresh("Formatacao concluida com sucesso.");
    app_log_write("INFO", "Formatacao de SD concluida via UI.");
  } else {
    settings_sd_format_status_refresh("Falha na formatacao. Verifique cartao e logs.");
  }
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
  settings_hide_wifi_keyboard();
  settings_hide_ftp_keyboard();
  settings_hide_time_keyboard();
  settings_hide_wg_keyboard();
  if (s_ta_mon_ip != nullptr) {
    lv_textarea_set_text(s_ta_mon_ip, app_settings_monitor_ip().c_str());
  }
  if (s_ta_ftp_user != nullptr) {
    lv_textarea_set_text(s_ta_ftp_user, app_settings_ftp_user().c_str());
  }
  if (s_ta_ftp_pass != nullptr) {
    lv_textarea_set_text(s_ta_ftp_pass, app_settings_ftp_pass().c_str());
  }
  if (s_font_slider != nullptr) {
    lv_slider_set_value(s_font_slider, app_settings_font_index(), LV_ANIM_OFF);
  }
  settings_rs485_sync_ui_from_settings();
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
  if (!sd_access_is_mounted()) {
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
static constexpr uint32_t kSettingsTabWifi = 0;
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
  if (sel != kSettingsTabWifi) {
    settings_hide_wifi_keyboard();
  }
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

static void settings_hide_wifi_keyboard(void) {
  if (s_sett_wifi_kb != nullptr) {
    lv_obj_add_flag(s_sett_wifi_kb, LV_OBJ_FLAG_HIDDEN);
  }
}

/** Teclado da aba Wi-Fi (apenas para "IP de monitorizacao" por agora). */
static void settings_wifi_ta_kb_event_cb(lv_event_t *e) {
  if (s_sett_wifi_kb == nullptr) {
    return;
  }
  const lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(s_sett_wifi_kb, ta);
    lv_obj_clear_flag(s_sett_wifi_kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED) {
    settings_hide_wifi_keyboard();
  }
}

static void settings_save_monitor_cb(lv_event_t *e) {
  (void)e;
  if (s_ta_mon_ip == nullptr) {
    return;
  }
  const char *ip = lv_textarea_get_text(s_ta_mon_ip);
  app_settings_set_monitor_ip(ip != nullptr ? ip : "");
  net_monitor_apply_settings();
  if (s_mon_feedback_lbl != nullptr) {
    const String saved = app_settings_monitor_ip();
    if (saved.length() > 0) {
      lv_label_set_text_fmt(s_mon_feedback_lbl, "A monitorizar %s.", saved.c_str());
    } else {
      lv_label_set_text(s_mon_feedback_lbl, "Monitorizacao desligada.");
    }
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
/** Opcoes do roller de baud RS485 (geradas a partir de `app_settings_rs485_std_baud`). */
static char s_rs485_baud_roller_buf[256];

static void build_rs485_baud_roller_options(void) {
  char *p = s_rs485_baud_roller_buf;
  const char *const end = s_rs485_baud_roller_buf + sizeof(s_rs485_baud_roller_buf);
  const size_t n = app_settings_rs485_std_baud_count();
  for (size_t i = 0U; i < n; i++) {
    if (i > 0U) {
      if (p >= end - 1) {
        break;
      }
      *p++ = '\n';
    }
    const int w = snprintf(p, (size_t)(end - p), "%lu", (unsigned long)app_settings_rs485_std_baud(i));
    if (w < 0 || (size_t)w >= (size_t)(end - p)) {
      break;
    }
    p += (size_t)w;
  }
  if (p < end) {
    *p = '\0';
  } else {
    s_rs485_baud_roller_buf[sizeof(s_rs485_baud_roller_buf) - 1U] = '\0';
  }
}

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
    ui_loading_show(s_scr_settings, "Sincronizando com o servidor NTP...");
    ui_loading_flush_display();
    lvgl_port_unlock();
    (void)net_time_sync_now_blocking(msg, sizeof msg);
    (void)lvgl_port_lock(-1);
    ui_loading_hide();
  } else {
    snprintf(msg, sizeof msg, "Salvo (NTP %s).", ntp_on ? "ativo" : "inativo");
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
  lv_label_set_text(s_wg_feedback_lbl, "Salvo.");
}

/** Mesma ordem que `uart_config_for_profile` em cycles_rs485.cpp (indices 0..7 na NVS). */
static const char *const kRs485FrameRollerOpts =
    "8N1\n"
    "8E1\n"
    "8O1\n"
    "8N2\n"
    "7N1\n"
    "7E1\n"
    "7O1\n"
    "7N2";

static void settings_save_rs485_cb(lv_event_t *e) {
  (void)e;
  if (s_rs485_roller_baud == nullptr || s_rs485_roller_frame == nullptr || s_rs485_feedback_lbl == nullptr) {
    return;
  }
  const uint32_t baud = app_settings_rs485_std_baud((size_t)lv_roller_get_selected(s_rs485_roller_baud));
  const uint8_t frame = (uint8_t)lv_roller_get_selected(s_rs485_roller_frame);
  app_settings_set_rs485(baud, frame);
  if (sd_access_is_mounted()) {
    cycles_rs485_apply_settings();
    lv_label_set_text(s_rs485_feedback_lbl, "Guardado. Serial1 reiniciada com a nova configuracao.");
  } else {
    lv_label_set_text(s_rs485_feedback_lbl,
                      "Guardado na NVS. Monte o SD e volte a salvar para aplicar na UART.");
  }
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

  s_bar_settings_monitor_pill = create_monitor_pill(bar);

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

  /* --- Monitorizacao remota (ping ICMP) --- */
  lv_obj_t *lbl_mon_sec = lv_label_create(tab_wifi);
  lv_label_set_text(lbl_mon_sec, "Monitorizacao remota (ping):");

  lv_obj_t *lbl_mon_ip = lv_label_create(tab_wifi);
  lv_label_set_text(lbl_mon_ip, "IP / host (deixe vazio para desligar):");

  s_ta_mon_ip = lv_textarea_create(tab_wifi);
  lv_textarea_set_one_line(s_ta_mon_ip, true);
  lv_textarea_set_max_length(s_ta_mon_ip, 63);
  lv_textarea_set_placeholder_text(s_ta_mon_ip, "ex. 192.168.0.1");
  lv_textarea_set_text(s_ta_mon_ip, app_settings_monitor_ip().c_str());
  lv_obj_set_width(s_ta_mon_ip, LV_PCT(100));
  lv_obj_add_event_cb(s_ta_mon_ip, settings_wifi_ta_kb_event_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_ta_mon_ip, settings_wifi_ta_kb_event_cb, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_t *bt_save_mon = lv_btn_create(tab_wifi);
  lv_obj_t *lb_save_mon = lv_label_create(bt_save_mon);
  lv_label_set_text(lb_save_mon, LV_SYMBOL_SAVE " Salvar monitorizacao");
  lv_obj_center(lb_save_mon);
  lv_obj_add_event_cb(bt_save_mon, settings_save_monitor_cb, LV_EVENT_CLICKED, nullptr);

  s_mon_feedback_lbl = lv_label_create(tab_wifi);
  lv_label_set_long_mode(s_mon_feedback_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_mon_feedback_lbl, LV_PCT(100));

  /* Teclado especifico da aba Wi-Fi (monitor IP por agora). */
  s_sett_wifi_kb = lv_keyboard_create(tab_wifi);
  lv_obj_set_size(s_sett_wifi_kb, LV_PCT(100), (lv_coord_t)(vh * 30 / 100));
  lv_keyboard_set_mode(s_sett_wifi_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_add_flag(s_sett_wifi_kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(s_sett_wifi_kb, nullptr);

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
  lv_label_set_text(lu, "Usuario FTP:");

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
  lv_label_set_text(lbs, LV_SYMBOL_SAVE " Salvar FTP");
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
  lv_label_set_text(lbst, LV_SYMBOL_SAVE " Salvar e sincronizar");
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
  lv_label_set_text(lbw, LV_SYMBOL_SAVE " Salvar WireGuard");
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

  /* --- Aba RS485 (Serial1: baud e trama; start bit UART e' sempre 1) --- */
  lv_obj_t *tab_rs485 = lv_tabview_add_tab(tv, LV_SYMBOL_LOOP " RS485");
  lv_obj_set_layout(tab_rs485, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(tab_rs485, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tab_rs485, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(tab_rs485, 6, 0);
  lv_obj_set_style_pad_row(tab_rs485, 8, 0);

  lv_obj_t *rs485_help = lv_label_create(tab_rs485);
  lv_label_set_long_mode(rs485_help, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(rs485_help, LV_PCT(100));
  lv_label_set_text(rs485_help,
                    "Leitura na Serial1 (pinos em board_pins.h).\n"
                    "UART: 1 start bit (fixo), dados, paridade e stop conforme o perfil.\n"
                    "Gravacao de linhas em /CICLOS/ no SD (cartao montado).\n"
                    "O leitor do .txt do dia abre na UI apos o arranque e quando chegar uma linha.");

  lv_obj_t *lb_baud = lv_label_create(tab_rs485);
  lv_label_set_text(lb_baud, "Velocidade (baud):");
  build_rs485_baud_roller_options();
  s_rs485_roller_baud = lv_roller_create(tab_rs485);
  lv_roller_set_options(s_rs485_roller_baud, s_rs485_baud_roller_buf, LV_ROLLER_MODE_NORMAL);
  lv_obj_set_width(s_rs485_roller_baud, LV_PCT(100));

  lv_obj_t *lb_frame = lv_label_create(tab_rs485);
  lv_label_set_text(lb_frame, "Trama (dados / paridade / stop):");
  s_rs485_roller_frame = lv_roller_create(tab_rs485);
  lv_roller_set_options(s_rs485_roller_frame, kRs485FrameRollerOpts, LV_ROLLER_MODE_NORMAL);
  lv_obj_set_width(s_rs485_roller_frame, LV_PCT(100));

  lv_obj_t *bt_rs485 = lv_btn_create(tab_rs485);
  lv_obj_t *lbl_rs485_save = lv_label_create(bt_rs485);
  lv_label_set_text(lbl_rs485_save, LV_SYMBOL_SAVE " Salvar RS485");
  lv_obj_center(lbl_rs485_save);
  lv_obj_add_event_cb(bt_rs485, settings_save_rs485_cb, LV_EVENT_CLICKED, nullptr);

  s_rs485_feedback_lbl = lv_label_create(tab_rs485);
  lv_label_set_long_mode(s_rs485_feedback_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_rs485_feedback_lbl, LV_PCT(100));
  lv_label_set_text(s_rs485_feedback_lbl, "");
  settings_rs485_sync_ui_from_settings();

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
                    "A operacao apaga todos os arquivos do cartao.\n"
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

  s_bar_monitor_pill = create_monitor_pill(bar);

  s_bar_wifi_lbl = lv_label_create(bar);
  lv_label_set_long_mode(s_bar_wifi_lbl, LV_LABEL_LONG_CLIP);
  lv_label_set_text(s_bar_wifi_lbl, LV_SYMBOL_WIFI " ...");

  s_bar_time_lbl = lv_label_create(bar);
  lv_obj_set_flex_grow(s_bar_time_lbl, 1);
  lv_obj_set_style_text_align(s_bar_time_lbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(s_bar_time_lbl, LV_LABEL_LONG_CLIP);
  lv_label_set_text(s_bar_time_lbl, "--/--/---- --:--");

  s_bar_home_btn = lv_btn_create(bar);
  lv_obj_set_size(s_bar_home_btn, 40, 36);
  lv_obj_t *home_lbl = lv_label_create(s_bar_home_btn);
  lv_label_set_text(home_lbl, LV_SYMBOL_HOME);
  lv_obj_center(home_lbl);
  lv_obj_add_event_cb(s_bar_home_btn, dashboard_btn_home_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(s_bar_home_btn, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *gear = lv_btn_create(bar);
  lv_obj_set_size(gear, 40, 36);
  lv_obj_set_style_bg_color(gear, lv_color_hex(0x449D48), 0);
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
  lv_label_set_text(btl, LV_SYMBOL_OK " Salvar e ligar");
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

void ui_app_run(bool sd_mounted, bool splash_active) {
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
    if (splash_active) {
      splash_screen_destroy();
    } else {
      boot_screen_destroy();
    }
    ensure_main_content_dashboard();
  } else {
    lv_scr_load(s_scr_wifi);
    if (splash_active) {
      splash_screen_destroy();
    } else {
      boot_screen_destroy();
    }
    lv_textarea_set_text(s_ta_wifi_ssid, "");
    lv_textarea_set_text(s_ta_wifi_pass, "");
    lv_label_set_text(s_wifi_status_lbl, "Introduza a rede e a senha. Toque num campo para o teclado.");
  }

  ui_apply_font_everywhere();
  update_bar_wifi_text();
}

void ui_app_open_cycles_txt_if_exists(void) { file_browser_open_today_cycles_txt(); }
