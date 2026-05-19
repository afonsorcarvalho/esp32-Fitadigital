/**
 * @file cycle_detector.h
 * @brief State machine que entende ciclos no stream RS485.
 *
 * Stream RS485 = linhas continuas. Detector reconhece padroes:
 *   - start_pattern (substring case-insensitive)   -> abre ciclo
 *   - end_pattern   (substring case-insensitive)   -> fecha ciclo
 *   - idle_timeout (segundos sem nova linha)       -> auto-fecha (TIMEOUT)
 *
 * Quando ciclo fecha, escreve 1 linha NDJSON em
 *   /CICLOS/AAAA/MM/cycles.ndjson
 * via sd_access_sync. Schema:
 *   {"start":"AAAA-MM-DDTHH:MM:SS","end":"...","duration_s":N,"lines":N,
 *    "bytes":N,"status":"DONE|TIMEOUT|INTERRUPTED","first_line":"..."}
 *
 * Thread-safe para chamada de reader_task (cycles_rs485.cpp). Estado interno
 * sob mutex leve (state pequeno, escrita NDJSON faz-se em sd_io via sd_access).
 *
 * v2.1.0 — primeira versao. Patterns hardcoded defaults; NVS NVS config futura.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum CycleDetectorStatus {
  CYCLE_STATUS_DONE = 0,        /* end_pattern matched durante ACTIVE */
  CYCLE_STATUS_TIMEOUT = 1,     /* idle_timeout expirou em ACTIVE */
  CYCLE_STATUS_INTERRUPTED = 2  /* novo start_pattern recebido enquanto ACTIVE */
};

/**
 * Inicializa detector com patterns + idle timeout.
 * Idempotente; chamadas subsequentes substituem config.
 *
 * @param start_pattern substring case-insensitive (NULL/"" desactiva detector)
 * @param end_pattern   substring case-insensitive (NULL/"" -> so' fecha por timeout)
 * @param idle_timeout_s segundos sem nova linha em ACTIVE para auto-fechar (0 = sem timeout)
 */
void cycle_detector_init(const char *start_pattern,
                         const char *end_pattern,
                         uint32_t idle_timeout_s);

/**
 * Re-aplica configuracao em runtime. Se state==ACTIVE, fecha ciclo como
 * CYCLE_STATUS_INTERRUPTED + escreve NDJSON antes de re-init.
 * Atualiza watchdog RTC baseline. Thread-safe.
 *
 * @param start_pattern  novo start; NULL/"" desactiva detector
 * @param end_pattern    novo end; NULL/"" -> so' fecha por timeout
 * @param idle_timeout_s novo timeout (0 = sem timeout)
 */
void cycle_detector_reconfigure(const char *start_pattern,
                                const char *end_pattern,
                                uint32_t idle_timeout_s);

/**
 * Processa 1 linha do RS485. Chamado de reader_commit_line em cycles_rs485.cpp,
 * apos commit SD. Atualiza state machine + acumula metadata. Em transicao para
 * DONE/INTERRUPTED escreve NDJSON.
 *
 * @param line   conteudo da linha (terminada em \0)
 * @param len    comprimento (sem \0)
 * @param now    timestamp em segundos epoch UTC (geralmente time(nullptr))
 */
void cycle_detector_process_line(const char *line, size_t len, time_t now);

/**
 * Tick periodico para detectar idle timeout. Chamar de loop com frequencia
 * razoavel (ex: cada 5s no reader_task). No-op se IDLE ou idle_timeout=0.
 *
 * @param now timestamp segundos epoch UTC
 */
void cycle_detector_tick(time_t now);

/**
 * Devolve JSON com estado actual para /api/cycles/status.
 * @return bytes escritos (sem \0), 0 se erro.
 */
size_t cycle_detector_status_json(char *out, size_t cap);

#ifdef __cplusplus
}
#endif
