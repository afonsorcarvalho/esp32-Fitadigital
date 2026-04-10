/**
 * @file waveshare_sd_cs.h
 * @brief CS do cartão SD via CH422G (não é GPIO direto); usado por `sd_diskio_waveshare.cpp`.
 */
#pragma once

class ESP_IOExpander;

/** Liga o expansor já inicializado pelo `ESP_Panel::begin()`. */
void waveshare_sd_cs_bind(ESP_IOExpander *expander);

/**
 * @param high true = CS inativo (nível alto); false = cartão selecionado.
 */
void waveshare_sd_cs_set(bool high);
