#include "heap_monitor.h"

#include <Arduino.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

#include "boot_journal.h"
#include "app_settings.h"

/* ets_printf: ROM function, sem VFS — mesmo padrão usado em sd_access.cpp/sd_hotplug.cpp. */
extern "C" int ets_printf(const char *fmt, ...);

namespace {

constexpr uint32_t kReportIntervalMs   = 30000U;
constexpr uint32_t kRebootThresholdMin = 4096U;   /* mínimo defensivo (4 KB) */
constexpr uint32_t kRebootThresholdDef = 6144U;   /* default 6 KB */
constexpr uint32_t kTaskStackBytes     = 3072U;
constexpr UBaseType_t kTaskPrio        = 1U;
constexpr BaseType_t kTaskCore         = 1;       /* core 1 — fora do core dos drivers SPI/SD */

uint32_t s_threshold_bytes = kRebootThresholdDef;
TaskHandle_t s_task = nullptr;

inline uint32_t heap_int_free_bytes() {
  return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

inline uint32_t heap_int_min_bytes() {
  return (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

inline uint32_t heap_psram_free_bytes() {
  return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

void heap_monitor_task(void *) {
  /* Pequeno atraso inicial para deixar boot estabilizar antes da primeira amostra. */
  vTaskDelay(pdMS_TO_TICKS(2000));

  for (;;) {
    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    const uint32_t int_free = heap_int_free_bytes();
    const uint32_t int_min  = heap_int_min_bytes();
    const uint32_t ps_free  = heap_psram_free_bytes();

    ets_printf("[HEAP] t=%u int=%u min=%u psram=%u\n",
                   (unsigned)now_ms, (unsigned)int_free, (unsigned)int_min, (unsigned)ps_free);

    if (int_free < s_threshold_bytes) {
      ets_printf("[HEAP_GUARD] free=%u < %u threshold — restarting\n",
                     (unsigned)int_free, (unsigned)s_threshold_bytes);
      char buf[96];
      snprintf(buf, sizeof(buf), "free=%u min=%u threshold=%u",
               (unsigned)int_free, (unsigned)int_min, (unsigned)s_threshold_bytes);
      boot_journal_append("HEAP_GUARD", buf);
      boot_journal_flush_to_spiffs();
      app_settings_heap_guard_count_increment();
      vTaskDelay(pdMS_TO_TICKS(150));
      esp_restart();
    }

    vTaskDelay(pdMS_TO_TICKS(kReportIntervalMs));
  }
}

} /* namespace */

void heap_monitor_set_threshold(uint32_t bytes) {
  s_threshold_bytes = (bytes < kRebootThresholdMin) ? kRebootThresholdMin : bytes;
}

void heap_monitor_start(void) {
  if (s_task != nullptr) return;
  xTaskCreatePinnedToCore(heap_monitor_task, "heap_mon",
                          kTaskStackBytes, nullptr, kTaskPrio, &s_task, kTaskCore);
}
