/**
 * @file ui_loading.h
 * @brief Overlay reutilizavel: fundo semitransparente, spinner e mensagem (accao lenta na UI).
 * @note Chamar com o mutex LVGL detido. Antes de trabalho bloqueante (SD, rede, etc.), chame
 *       `lvgl_port_unlock()` e depois `lvgl_port_lock(-1)` ao voltar — senao a tarefa LVGL nao
 *       executa `lv_timer_handler` e o spinner (animacao) fica estatico.
 */
#pragma once

#include <lvgl.h>
#include <stdbool.h>

/**
 * Mostra o overlay sobre o parent (tela cheia na area do parent). Remove overlay anterior se existir.
 * @param parent Area a cobrir (ex. explorador); se NULL usa lv_layer_top().
 */
void ui_loading_show(lv_obj_t *parent, const char *message);

/** Remove o overlay criado por ui_loading_show. */
void ui_loading_hide(void);

bool ui_loading_is_visible(void);

/**
 * Forca um redesenho imediato para o utilizador ver o overlay antes de trabalho longo no mesmo callback.
 */
void ui_loading_flush_display(void);
