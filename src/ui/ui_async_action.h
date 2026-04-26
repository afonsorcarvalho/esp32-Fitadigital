#pragma once

#include <lvgl.h>
#include <stdint.h>

/**
 * @file ui_async_action.h
 * @brief Helper para acoes UI que envolvem I/O lento (SD, rede), com overlay
 *        de carregamento e callback de finalizacao na task LVGL.
 *
 * Padrao "abrir pastas" — `refresh_file_list` usa `sd_access_async` para
 * delegar o trabalho na task `sd_io` enquanto a task LVGL continua livre
 * para processar timers (overlay com delay 150 ms, animacoes, toque).
 *
 * Por que nao basta `sd_access_sync` + `lvgl_port_unlock/lock`?
 *   `sd_access_sync` bloqueia o caller em `xSemaphoreTake(done)`. O caller
 *   neste contexto E a propria `lvgl_port_task` (event handler de botao corre
 *   dentro de `lv_timer_handler`). Mesmo com lock LVGL libertado, a task
 *   esta bloqueada — nao executa `lv_timer_handler` ate o sync retornar.
 *   Resultado: timers LVGL (incluindo o de 150 ms do overlay delayed) nao
 *   disparam durante o trabalho. Spinner nunca aparece.
 *
 * Com `ui_async_action_run`:
 *   1. arma `ui_loading_show_delayed(parent, message, delay_ms)` (timer LVGL).
 *   2. enfileira `work_fn` em `sd_access_async` — caller retorna imediatamente.
 *   3. quando o trabalho na task `sd_io` termina, faz `lv_async_call` para
 *      executar `finish_fn` na task LVGL (com lock automatico).
 *   4. esconde o overlay (cancela timer pendente OU destroi overlay visivel).
 *
 * `work_fn` corre na task `sd_io` — pode chamar SD.* directamente. NAO pode
 * tocar em objetos LVGL nem em estado partilhado sem sincronizacao.
 *
 * `finish_fn` corre na task LVGL com lock — pode tocar em qualquer obj LVGL.
 *
 * `user_data` e' opaco: passado a ambas as funcoes. Lifetime garantido pelo
 * caller (tipico: static struct local ao callback).
 *
 * Limite: nao-reentrante. Apenas 1 acao em curso de cada vez. Tentativa de
 * iniciar uma segunda enquanto outra esta a correr e' rejeitada (retorna false).
 */

typedef void (*UiAsyncWorkFn)(void *user_data);    /* sd_io context (sem lock LVGL) */
typedef void (*UiAsyncFinishFn)(void *user_data);  /* LVGL context (com lock) */

bool ui_async_action_run(lv_obj_t *overlay_parent,
                         const char *overlay_message,
                         uint32_t overlay_delay_ms,
                         UiAsyncWorkFn work_fn,
                         UiAsyncFinishFn finish_fn,
                         void *user_data);

bool ui_async_action_busy(void);
