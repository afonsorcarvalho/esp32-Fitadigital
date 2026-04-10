/**
 * @file waveshare_sd_cs.cpp
 * @brief Implementação do CS SD no CH422G (EXIO4), conforme documentação Waveshare.
 */
#include "waveshare_sd_cs.h"
#include "board_pins.h"
#include "ESP_IOExpander.h"

static ESP_IOExpander *s_sd_expander = nullptr;

void waveshare_sd_cs_bind(ESP_IOExpander *expander) {
  s_sd_expander = expander;
}

void waveshare_sd_cs_set(bool high) {
  if (!s_sd_expander) {
    return;
  }
  s_sd_expander->digitalWrite(BOARD_TF_SD_CS_EXPANDER_PIN, high ? HIGH : LOW);
}
