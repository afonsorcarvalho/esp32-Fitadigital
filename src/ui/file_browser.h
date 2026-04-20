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

/**
 * Anula ponteiros LVGL do explorador (overlay, lista, raiz) sem chamar lv_obj_del.
 * Usar antes de lv_obj_clean no pai (ex.: s_main_content) para evitar ponteiro pendente
 * e libertar indice PSRAM do visualizador.
 */
void file_browser_detach_stale_widgets(void);

/**
 * Abre manualmente o .txt do dia em /CICLOS (se existir), com scroll no fim.
 * O arranque nao chama isto: o leitor abre so apos linha RS485 (com follow activo).
 * Chamar com mutex LVGL detido.
 */
void file_browser_open_today_cycles_txt(void);

/**
 * Chamado apos gravar uma linha RS485 no SD (contexto sd_io). Sem LVGL: apenas sinaliza
 * para a tarefa LVGL reabrir o .txt do dia com scroll no fim.
 */
void file_browser_on_rs485_line_saved(void);

/**
 * Navega para uma pasta absoluta (ex.: "/CICLOS"). Se o caminho for invalido ou
 * nao existir, permanece onde estava.
 */
void file_browser_goto(const char *path);

/** Atualiza a listagem do diretorio atual (com overlay "A carregar..."). */
void file_browser_refresh(void);

/** Atualiza a listagem sem mostrar overlay (para auto-refresh em segundo plano). */
void file_browser_refresh_silent(void);

/**
 * true quando nao deve correr file_browser_refresh_silent (timer da barra de estado):
 * indexacao do .txt em curso ou visualizador em overlay (evita lista vazia / corrida com o SD).
 */
bool file_browser_should_skip_auto_list_refresh(void);

/** Timestamp (millis) da ultima vez que a lista de ficheiros foi desenhada na UI. */
uint32_t file_browser_last_refresh_ms(void);

/** Aplica fonte a todos os widgets do explorador (apos mudar tamanho nas definicoes). */
void file_browser_apply_font(const lv_font_t *font);
