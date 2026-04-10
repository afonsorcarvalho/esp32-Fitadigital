/**
 * @file file_browser.h
 * @brief Explorador estilo lista (nome + tamanho) no volume SD (SD.open paths com raiz '/').
 */
#pragma once

#include <lvgl.h>
#include <stdbool.h>

/**
 * @param parent Contentor (ex.: area abaixo da barra de estado); deve ter tamanho definido.
 * @return true se o SD estava disponivel e a lista foi criada.
 */
bool file_browser_init(lv_obj_t *parent);

/** Atualiza a listagem do diretorio atual. */
void file_browser_refresh(void);

/** Aplica fonte a todos os widgets do explorador (apos mudar tamanho nas definicoes). */
void file_browser_apply_font(const lv_font_t *font);
