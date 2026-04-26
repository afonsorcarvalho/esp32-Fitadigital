#pragma once

#include <stdint.h>

/**
 * @file heap_monitor.h
 * @brief Task periódica de telemetria de heap + watchdog de baixo-nível.
 *
 * A task imprime via ets_printf (ROM, sem VFS) uma linha CSV-friendly a cada
 * `kReportIntervalMs` (30s):
 *   `[HEAP] t=<ms> int=<bytes> min=<bytes> psram=<bytes>`
 *
 * Se a heap interna livre cair abaixo de `kRebootThresholdBytes` (6 KB),
 * regista entrada no boot_journal e chama esp_restart() — reboot graceful
 * antes do panic OOM/Double exception observado no soak de 1h.
 */

void heap_monitor_start(void);

void heap_monitor_set_threshold(uint32_t bytes);
