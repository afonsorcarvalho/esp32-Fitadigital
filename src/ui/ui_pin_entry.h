#pragma once
#include <lvgl.h>

/** Callback chamado apos o user confirmar o PIN.
 *  @param correct  true se o PIN introduzido coincide com o guardado. */
typedef void (*ui_pin_result_cb_t)(bool correct);

/** Mostra modal de entrada de PIN (4 rollers 0-9).
 *  @param cb  Funcao chamada com o resultado; modal e' fechado antes de chamar. */
void ui_pin_entry_show(ui_pin_result_cb_t cb);
