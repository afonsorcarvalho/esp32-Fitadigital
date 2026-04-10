/**
 * @file ui_feedback.h
 * @brief Bip curto ao tocar no ecra (LEDC + GPIO opcional em board_pins.h).
 */
#pragma once

/** Configura LEDC e temporizador para cortar o som (chamar uma vez no setup). */
void ui_feedback_init(void);

/** Chamado na transicao para "pressionado" no leitor de touch (um bip por toque). */
void ui_feedback_touch_down(void);
