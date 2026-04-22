#pragma once
#include <lvgl.h>

/** Callback chamado apos o user confirmar o PIN.
 *  @param correct  true se o PIN introduzido coincide com o guardado. */
typedef void (*ui_pin_result_cb_t)(bool correct);

/** Mostra modal de entrada de PIN (4 rollers 0-9).
 *  @param cb  Funcao chamada com o resultado; modal e' fechado antes de chamar. */
void ui_pin_entry_show(ui_pin_result_cb_t cb);

/** Callback do modo captura: devolve o PIN digitado (ou cancelamento).
 *  @param confirmed  true se OK; false se Cancelar / tap fora.
 *  @param pin4       ponteiro para buffer de 4 chars (sem null); valido apenas durante a chamada. */
typedef void (*ui_pin_capture_cb_t)(bool confirmed, const char *pin4);

/** Mostra modal de captura de PIN (4 rollers), sem comparar com o guardado.
 *  Usado no fluxo de troca de PIN (actual / novo / confirmar).
 *  @param title  Texto mostrado no cabecalho do modal.
 *  @param cb     Callback chamado apos OK ou Cancelar. */
void ui_pin_entry_capture_show(const char *title, ui_pin_capture_cb_t cb);
