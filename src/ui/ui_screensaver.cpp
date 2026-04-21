#include "ui_screensaver.h"
#include "app_settings.h"
#include <lvgl.h>
#include <stdlib.h>
#include <atomic>

LV_IMG_DECLARE(afr_logo_verde_large);

/* Timeout lido das settings em runtime */
#define LOGO_W                  360
#define LOGO_H                  112
#define STEP_X                  40
#define STEP_Y                  30

namespace {

lv_obj_t   *s_bg    = nullptr;
lv_obj_t   *s_img   = nullptr;
lv_timer_t *s_timer = nullptr;
std::atomic<bool> s_wake_requested{false};

int s_x = 0, s_y = 0;
int s_dx = STEP_X, s_dy = STEP_Y;

/* Paleta de cores de fundo — nenhuma muito clara (logo verde tem contraste). */
static const uint32_t kColors[] = {
    0x1A1A2E, 0x16213E, 0x0F3460, 0x1B2631,
    0x17202A, 0x212F3C, 0x1C2833, 0x0B2447,
    0x1A0533, 0x0D1B2A, 0x1B4332, 0x081C15,
};
static const int kNumColors = sizeof(kColors) / sizeof(kColors[0]);

void screensaver_tick(void) {
    if (s_bg == nullptr || s_img == nullptr) return;

    const lv_coord_t scr_w = lv_disp_get_hor_res(nullptr);
    const lv_coord_t scr_h = lv_disp_get_ver_res(nullptr);

    s_x += s_dx;
    s_y += s_dy;

    if (s_x <= 0)            { s_x = 0;              s_dx = STEP_X; }
    if (s_x >= scr_w - LOGO_W) { s_x = scr_w - LOGO_W; s_dx = -STEP_X; }
    if (s_y <= 0)            { s_y = 0;              s_dy = STEP_Y; }
    if (s_y >= scr_h - LOGO_H) { s_y = scr_h - LOGO_H; s_dy = -STEP_Y; }

    lv_obj_set_pos(s_img, (lv_coord_t)s_x, (lv_coord_t)s_y);

    (void)kColors; /* reservado para uso futuro */
}

void screensaver_destroy(void) {
    if (s_bg == nullptr) return;
    lv_obj_del(s_bg);
    s_bg  = nullptr;
    s_img = nullptr;
    lv_disp_trig_activity(nullptr);
}

void screensaver_close_cb(lv_event_t * /*e*/) {
    /* Chamado dentro de event callback — usar async para nao deletar o objeto
       que esta a processar o evento (corromperia o event system do LVGL 8). */
    if (s_bg == nullptr) return;
    lv_obj_del_async(s_bg);
    s_bg  = nullptr;
    s_img = nullptr;
    lv_disp_trig_activity(nullptr);
}

void screensaver_show(void) {
    if (s_bg != nullptr) return;

    const lv_coord_t scr_w = lv_disp_get_hor_res(nullptr);
    const lv_coord_t scr_h = lv_disp_get_ver_res(nullptr);

    s_x = (scr_w - LOGO_W) / 2;
    s_y = (scr_h - LOGO_H) / 2;
    s_dx = STEP_X;
    s_dy = STEP_Y;

    s_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_bg, scr_w, scr_h);
    lv_obj_set_style_bg_color(s_bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_bg, 0, 0);
    lv_obj_set_style_pad_all(s_bg, 0, 0);
    lv_obj_set_style_radius(s_bg, 0, 0);
    lv_obj_clear_flag(s_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_bg, screensaver_close_cb, LV_EVENT_CLICKED, nullptr);

    s_img = lv_img_create(s_bg);
    lv_img_set_src(s_img, &afr_logo_verde_large);
    lv_obj_set_pos(s_img, (lv_coord_t)s_x, (lv_coord_t)s_y);
}

void watch_cb(lv_timer_t * /*t*/) {
    if (s_wake_requested.exchange(false) && s_bg != nullptr) {
        screensaver_destroy();
        return;
    }
    if (s_bg != nullptr) {
        screensaver_tick();
    } else if (app_settings_screensaver_enabled()) {
        const uint32_t timeout_ms = (uint32_t)app_settings_screensaver_timeout() * 1000u;
        if (lv_disp_get_inactive_time(nullptr) >= timeout_ms) {
            screensaver_show();
        }
    }
}

} // namespace

void ui_screensaver_init(void) {
    if (s_timer == nullptr) {
        s_timer = lv_timer_create(watch_cb, 1000, nullptr);
    }
}

void ui_screensaver_wake(void) {
    s_wake_requested.store(true, std::memory_order_relaxed);
}
