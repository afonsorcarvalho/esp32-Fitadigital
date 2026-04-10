/**
 * @file ui_feedback.cpp
 * @brief LEDC num GPIO livre; buzzer OFF por esp_timer (sem delay na tarefa LVGL).
 */
#include "ui_feedback.h"

#include "board_pins.h"

#include <esp_timer.h>
#include <driver/ledc.h>

#if BOARD_UI_BEEP_GPIO >= 0

static constexpr ledc_mode_t kBuzzMode = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t kBuzzTimer = LEDC_TIMER_2;
static constexpr ledc_channel_t kBuzzChannel = LEDC_CHANNEL_6;
static constexpr uint32_t kBuzzFreqHz = 2600;
static constexpr uint32_t kBuzzDutyOn = 512; /* ~50% em 10 bits */
static constexpr uint64_t kBuzzDurationUs = 38000;

static esp_timer_handle_t s_beep_off_timer = nullptr;

static void beep_off_cb(void * /*arg*/) {
  ledc_set_duty(kBuzzMode, kBuzzChannel, 0);
  ledc_update_duty(kBuzzMode, kBuzzChannel);
}

#endif

void ui_feedback_init(void) {
#if BOARD_UI_BEEP_GPIO >= 0
  static bool done = false;
  if (done) {
    return;
  }
  done = true;

  ledc_timer_config_t t = {.speed_mode = kBuzzMode,
                           .duty_resolution = LEDC_TIMER_10_BIT,
                           .timer_num = kBuzzTimer,
                           .freq_hz = kBuzzFreqHz,
                           .clk_cfg = LEDC_AUTO_CLK};
  if (ledc_timer_config(&t) != ESP_OK) {
    return;
  }

  ledc_channel_config_t ch = {.gpio_num = (int)BOARD_UI_BEEP_GPIO,
                              .speed_mode = kBuzzMode,
                              .channel = kBuzzChannel,
                              .intr_type = LEDC_INTR_DISABLE,
                              .timer_sel = kBuzzTimer,
                              .duty = 0,
                              .hpoint = 0,
                              .flags = {.output_invert = 0}};
  if (ledc_channel_config(&ch) != ESP_OK) {
    return;
  }

  const esp_timer_create_args_t args = {.callback = &beep_off_cb, .name = "beep_off"};
  if (esp_timer_create(&args, &s_beep_off_timer) != ESP_OK) {
    s_beep_off_timer = nullptr;
  }
#endif
}

void ui_feedback_touch_down(void) {
#if BOARD_UI_BEEP_GPIO >= 0
  if (s_beep_off_timer == nullptr) {
    return;
  }
  (void)esp_timer_stop(s_beep_off_timer);
  ledc_set_duty(kBuzzMode, kBuzzChannel, kBuzzDutyOn);
  ledc_update_duty(kBuzzMode, kBuzzChannel);
  (void)esp_timer_start_once(s_beep_off_timer, kBuzzDurationUs);
#endif
}
