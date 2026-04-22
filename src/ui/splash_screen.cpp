/**
 * @file splash_screen.cpp
 * @brief Splash de boot: fundo branco com logo AFR centralizada.
 *
 * Após lv_scr_load, o ecrã de boot (checklist) é destruído aqui.
 * O chamador deve aguardar o tempo de splash (vTaskDelay) antes de
 * carregar a interface principal e chamar splash_screen_destroy().
 */

#include "splash_screen.h"
#include "boot_screen.h"
#include "lvgl_port_v8.h"
#include <lvgl.h>

#ifndef FITADIGITAL_VERSION
#define FITADIGITAL_VERSION "unknown"
#endif

LV_IMG_DECLARE(afr_logo);

namespace {
lv_obj_t *s_splash_scr = nullptr;
}

void splash_screen_show(void) {
  if (!lvgl_port_lock(-1)) {
    return;
  }

  s_splash_scr = lv_obj_create(nullptr);
  lv_obj_set_size(s_splash_scr, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_splash_scr, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_splash_scr, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_splash_scr, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_splash_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *img = lv_img_create(s_splash_scr);
  lv_img_set_src(img, &afr_logo);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t *lbl = lv_label_create(s_splash_scr);
  lv_label_set_text(lbl, "Desenvolvido por AFR Solucoes Inteligentes");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x555555), LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_width(lbl, LV_PCT(90));
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 50);

  lv_obj_t *ver_lbl = lv_label_create(s_splash_scr);
  lv_label_set_text(ver_lbl, "v" FITADIGITAL_VERSION);
  lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0xaaaaaa), LV_PART_MAIN);
  lv_obj_align(ver_lbl, LV_ALIGN_BOTTOM_MID, 0, -10);

  lv_scr_load(s_splash_scr);

  (void)lvgl_port_unlock();

  /* Seguro destruir o boot screen: o splash já é o ecrã activo. */
  boot_screen_destroy();
}

void splash_screen_destroy(void) {
  if (!lvgl_port_lock(-1)) {
    return;
  }
  if (s_splash_scr != nullptr) {
    lv_obj_del(s_splash_scr);
    s_splash_scr = nullptr;
  }
  (void)lvgl_port_unlock();
}
