/**
 * @file sd_hotplug.h
 * @brief Detecao de insercao/remocao do SD card por polling.
 *
 * Sem pino CD dedicado: usa SD.cardType() para detectar presenca.
 * Ticker registado em sd_access via sd_access_register_tick() — executa
 * a cada iteracao da tarefa sd_io, mas aplica debounce interno de 2000 ms.
 *
 * Ao inserir: chama mount_sd_fallback() + sd_access_set_mounted(true) +
 *             [callback on_inserted, se registado] +
 *             sd_access_notify_changed() + log "[sd_hotplug] inserted".
 * Ao remover: chama SD.end() + sd_access_set_mounted(false) +
 *             sd_access_notify_changed() + log "[sd_hotplug] removed".
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Funcao de callback invocada apos mount sucesso na insercao do SD.
 * Corre no contexto sd_io — pode aceder ao SD directamente.
 */
typedef void (*sd_hotplug_event_fn)(void);

/**
 * Regista um callback a invocar na insercao do SD (apos mount sucesso, antes do log "inserted").
 * Substituicao directa: apenas um callback de cada vez.
 * Chamar antes de sd_hotplug_init() ou apos — ambos sao validos.
 * @param fn Funcao a chamar, ou NULL para desregistar.
 */
void sd_hotplug_set_on_inserted(sd_hotplug_event_fn fn);

/**
 * Inicializa o modulo de hotplug.
 * Regista o ticker via sd_access_register_tick().
 * Chamar apos sd_access_start_task() no setup().
 */
void sd_hotplug_init(void);

#ifdef __cplusplus
}
#endif
