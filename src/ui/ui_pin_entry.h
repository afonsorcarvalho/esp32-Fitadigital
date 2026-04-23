#pragma once
#include <lvgl.h>

/** Callback chamado apos o user confirmar a senha.
 *  @param correct  true se a senha introduzida coincide com a guardada em NVS. */
typedef void (*ui_pin_result_cb_t)(bool correct);

/** Mostra modal de entrada de senha (teclado alfanumerico, max 16 chars).
 *  Compara o texto introduzido com app_settings_settings_pin().
 *  @param cb  Funcao chamada com o resultado; modal e' fechado antes de chamar. */
void ui_pin_entry_show(ui_pin_result_cb_t cb);

/** Callback do modo captura: devolve a senha digitada (string null-terminated) ou cancelamento.
 *  @param confirmed  true se OK; false se Cancelar / tap fora.
 *  @param passwd     ponteiro para buffer null-terminated (valido apenas durante a chamada). */
typedef void (*ui_pin_capture_cb_t)(bool confirmed, const char *passwd);

/** Mostra modal de captura de senha (teclado alfanumerico), sem comparar com o guardado.
 *  Usado no fluxo de troca de senha (actual / nova / confirmar).
 *  @param title  Texto mostrado no cabecalho do modal.
 *  @param cb     Callback chamado apos OK ou Cancelar. */
void ui_pin_entry_capture_show(const char *title, ui_pin_capture_cb_t cb);
