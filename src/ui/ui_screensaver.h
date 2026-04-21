#pragma once

/** Inicia timer LVGL que vigia inatividade; mostra logo em bounce apos 60 s. */
void ui_screensaver_init(void);

/** Pode ser chamado de qualquer task (thread-safe); fecha screensaver no proximo tick LVGL. */
void ui_screensaver_wake(void);
