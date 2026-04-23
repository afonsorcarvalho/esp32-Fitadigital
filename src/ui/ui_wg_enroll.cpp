/**
 * @file ui_wg_enroll.cpp
 * @brief Modal de enrollment WireGuard: exibe QR, contagem decrescente e estado da maquina.
 *
 * LVGL timer (500 ms) le wg_provision_get_status() e reconstroi o conteudo do modal
 * apenas quando o estado muda. Enquanto em SHOWING_QR, atualiza so o label do countdown.
 */
#include "ui_wg_enroll.h"
#include "ui/ui_app.h"
#include "ui_toast.h"
#include "wg_provision.h"

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>
#include "ui_theme.h"

namespace {

/* ── widget handles ───────────────────────────────────────────────────────── */

lv_obj_t *s_bg      = nullptr;
lv_obj_t *s_modal   = nullptr;
lv_obj_t *s_content = nullptr; /**< Container que e' limpo/recriado em cada mudanca de estado. */
lv_obj_t *s_countdown_lbl = nullptr; /**< So existe enquanto em SHOWING_QR. */

lv_timer_t   *s_timer       = nullptr;
WgProvState   s_last_state   = WgProvState::IDLE;
bool          s_auto_closing = false;

/* ── helpers ──────────────────────────────────────────────────────────────── */

void do_close(void) {
    wg_provision_cancel();
    if (s_timer != nullptr) {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
    if (s_bg != nullptr) {
        lv_obj_del(s_bg);
        s_bg      = nullptr;
        s_modal   = nullptr;
        s_content = nullptr;
        s_countdown_lbl = nullptr;
    }
    s_last_state   = WgProvState::IDLE;
    s_auto_closing = false;
}

void close_cb(lv_event_t * /*e*/) { do_close(); }

void retry_cb(lv_event_t *e) {
    const char *url = (const char *)lv_event_get_user_data(e);
    wg_provision_start(url);
    s_last_state = WgProvState::IDLE; /* force rebuild on next tick */
}

static char s_server_url_copy[128] = {};

/* ── countdown formatting ─────────────────────────────────────────────────── */

static void fmt_countdown(char *buf, size_t sz, uint32_t expires_at_ms) {
    const uint32_t now  = millis();
    const uint32_t rem  = (expires_at_ms > now) ? (expires_at_ms - now) / 1000 : 0;
    const uint32_t mins = rem / 60;
    const uint32_t secs = rem % 60;
    snprintf(buf, sz, "Expira em %02u:%02u", mins, secs);
}

/* ── content builders ─────────────────────────────────────────────────────── */

static void build_spinner(const char *msg) {
    lv_obj_t *spinner = lv_spinner_create(s_content, 1000, 60);
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_center(spinner);

    lv_obj_t *lbl = lv_label_create(s_content);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
}

static void build_qr_view(const WgProvStatus *st) {
    lv_obj_t *qr = lv_qrcode_create(s_content, 240,
                                      UI_COLOR_BLACK,
                                      UI_COLOR_WHITE);
    const lv_res_t r = lv_qrcode_update(qr, st->activation_url, strlen(st->activation_url));
    if (r != LV_RES_OK) {
        ui_toast_show(ToastKind::Error, "URL longa de mais para QR");
    }

    lv_obj_t *code_lbl = lv_label_create(s_content);
    lv_label_set_text_fmt(code_lbl, "Codigo: " LV_SYMBOL_LOOP " %s", st->activation_code);
    lv_obj_set_style_text_font(code_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(code_lbl, UI_COLOR_TEXT_DARK, 0);

    s_countdown_lbl = lv_label_create(s_content);
    char buf[32];
    fmt_countdown(buf, sizeof(buf), st->expires_at_ms);
    lv_label_set_text(s_countdown_lbl, buf);
    lv_obj_set_style_text_color(s_countdown_lbl, UI_COLOR_COUNTDOWN, 0);
}

static void build_success(void) {
    lv_obj_t *icon = lv_label_create(s_content);
    lv_label_set_text(icon, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(icon, UI_COLOR_PRIMARY, 0);

    lv_obj_t *lbl = lv_label_create(s_content);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_text(lbl, "Dispositivo provisionado!\nTunel WireGuard ativo.");
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
}

static void build_error(const char *msg) {
    lv_obj_t *icon = lv_label_create(s_content);
    lv_label_set_text(icon, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(icon, UI_COLOR_ERROR, 0);

    lv_obj_t *lbl = lv_label_create(s_content);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_text(lbl, msg && msg[0] ? msg : "Erro desconhecido");
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_ERROR, 0);

    lv_obj_t *btn_retry = lv_btn_create(s_content);
    lv_obj_set_size(btn_retry, 180, 44);
    lv_obj_set_style_bg_color(btn_retry, UI_COLOR_BLUE, 0);
    lv_obj_t *lb_r = lv_label_create(btn_retry);
    lv_label_set_text(lb_r, LV_SYMBOL_REFRESH " Tentar novamente");
    lv_obj_center(lb_r);
    lv_obj_add_event_cb(btn_retry, retry_cb, LV_EVENT_CLICKED, (void *)s_server_url_copy);
}

static void rebuild_content(const WgProvStatus *st) {
    lv_obj_clean(s_content);
    s_countdown_lbl = nullptr;

    switch (st->state) {
    case WgProvState::KEYGEN:
        build_spinner("Gerando chaves...");
        break;
    case WgProvState::ENROLLING:
        build_spinner("Registrando dispositivo...");
        break;
    case WgProvState::SHOWING_QR:
        build_qr_view(st);
        break;
    case WgProvState::APPLYING:
        build_spinner("Configurando tunel...");
        break;
    case WgProvState::ENROLLED:
        build_success();
        break;
    case WgProvState::ERROR:
        build_error(st->error_msg);
        break;
    default:
        break;
    }
}

/* ── LVGL timer ───────────────────────────────────────────────────────────── */

static void auto_close_timer_cb(lv_timer_t * /*t*/) { do_close(); }

static void poll_timer_cb(lv_timer_t * /*t*/) {
    if (s_bg == nullptr) return;

    WgProvStatus st;
    wg_provision_get_status(&st);

    if (st.state != s_last_state) {
        s_last_state = st.state;
        rebuild_content(&st);

        if (st.state == WgProvState::ENROLLED) {
            ui_app_refresh_wg_fields();
            /* Auto-close after 3 s so user sees the success screen. */
            lv_timer_create(auto_close_timer_cb, 3000, nullptr)->repeat_count = 1;
            s_auto_closing = true;
        }
    } else if (st.state == WgProvState::SHOWING_QR && s_countdown_lbl != nullptr) {
        char buf[32];
        fmt_countdown(buf, sizeof(buf), st.expires_at_ms);
        lv_label_set_text(s_countdown_lbl, buf);
    }
}

} // namespace

/* ── public API ───────────────────────────────────────────────────────────── */

void ui_wg_enroll_open(const char *server_url) {
    if (s_bg != nullptr) return; /* already open */
    if (server_url == nullptr || server_url[0] == '\0') {
        ui_toast_show(ToastKind::Warn, "URL do servidor em falta");
        return;
    }
    strncpy(s_server_url_copy, server_url, sizeof(s_server_url_copy) - 1);
    s_server_url_copy[sizeof(s_server_url_copy) - 1] = '\0';

    const lv_coord_t scr_w = lv_disp_get_hor_res(nullptr);
    const lv_coord_t scr_h = lv_disp_get_ver_res(nullptr);

    /* ── background overlay ── */
    s_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_bg, scr_w, scr_h);
    lv_obj_align(s_bg, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_bg, UI_COLOR_BLACK, 0);
    lv_obj_set_style_bg_opa(s_bg, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_bg, 0, 0);
    lv_obj_set_style_pad_all(s_bg, 0, 0);
    lv_obj_set_style_radius(s_bg, 0, 0);
    lv_obj_clear_flag(s_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* ── modal card ── */
    s_modal = lv_obj_create(s_bg);
    lv_obj_set_size(s_modal, (lv_coord_t)(scr_w - 40), (lv_coord_t)(scr_h - 40));
    lv_obj_center(s_modal);
    lv_obj_set_style_bg_color(s_modal, UI_COLOR_WHITE, 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_modal, 12, 0);
    lv_obj_set_style_pad_all(s_modal, 14, 0);
    lv_obj_set_style_pad_row(s_modal, 10, 0);
    lv_obj_set_layout(s_modal, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    /* title row */
    lv_obj_t *title = lv_label_create(s_modal);
    lv_label_set_text(title, LV_SYMBOL_LOOP " Provisionar WireGuard");
    lv_obj_set_style_text_color(title, UI_COLOR_BLUE, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    /* content container (rebuilt on each state change) */
    s_content = lv_obj_create(s_modal);
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_layout(s_content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_set_style_pad_row(s_content, 8, 0);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    /* close button */
    lv_obj_t *btn_close = lv_btn_create(s_modal);
    lv_obj_set_size(btn_close, 150, 42);
    lv_obj_set_style_bg_color(btn_close, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_t *lb_c = lv_label_create(btn_close);
    lv_label_set_text(lb_c, LV_SYMBOL_CLOSE " Fechar");
    lv_obj_center(lb_c);
    lv_obj_add_event_cb(btn_close, close_cb, LV_EVENT_CLICKED, nullptr);

    /* start provision + LVGL polling timer */
    s_last_state   = WgProvState::IDLE;
    s_auto_closing = false;
    wg_provision_start(server_url);
    s_timer = lv_timer_create(poll_timer_cb, 500, nullptr);
}

void ui_wg_enroll_close(void) {
    do_close();
}
