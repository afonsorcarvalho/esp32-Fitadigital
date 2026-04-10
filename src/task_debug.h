/**
 * @file task_debug.h
 * @brief Log periodico de tarefas FreeRTOS (lista, stack, tempo de CPU) e medicoes de net_services_loop.
 */
#pragma once

#include <stdint.h>

/** Intervalo por default entre snapshots completos (ms). */
#define TASK_DEBUG_DEFAULT_INTERVAL_MS 5000U

/**
 * Imprime no Serial um snapshot: nucleo da tarefa rede, duracao maxima de net_services_loop,
 * lista de tarefas (vTaskList) e, se compilado, estatisticas de tempo de execucao (vTaskGetRunTimeStats).
 */
void task_debug_print_snapshot(void);

/** Atualiza estatisticas da ultima iteracao de net_services_loop (chamar no fim do loop). */
void task_debug_note_net_loop_duration_us(uint32_t duration_us);
