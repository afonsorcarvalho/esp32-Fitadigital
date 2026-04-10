/**
 * @file task_debug.cpp
 * @brief Debug FreeRTOS: vTaskList, vTaskGetRunTimeStats (se CONFIG activo), watermark net_services.
 */
#include "task_debug.h"

#include <Arduino.h>

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

static uint32_t s_net_loop_max_us = 0;
static uint32_t s_net_loop_last_us = 0;

void task_debug_note_net_loop_duration_us(uint32_t duration_us) {
  s_net_loop_last_us = duration_us;
  if (duration_us > s_net_loop_max_us) {
    s_net_loop_max_us = duration_us;
  }
}

void task_debug_print_snapshot(void) {
  const BaseType_t net_core = xPortGetCoreID();

  Serial.printf("[TASK] --- snapshot (core desta tarefa=%d, heap livre=%u) ---\n", (int)net_core,
                (unsigned)ESP.getFreeHeap());

  Serial.printf("[TASK] net_services_loop: ultima iteracao=%lu us, max desde ultimo log=%lu us\n",
                (unsigned long)s_net_loop_last_us, (unsigned long)s_net_loop_max_us);
  s_net_loop_max_us = 0;

#if defined(CONFIG_FREERTOS_USE_TRACE_FACILITY) && CONFIG_FREERTOS_USE_TRACE_FACILITY
  {
    char *buf = (char *)malloc(3072U);
    if (buf != nullptr) {
      vTaskList(buf);
      Serial.println("[TASK] Nome | Estad | Pri | Stack livre | Core");
      Serial.print(buf);
      free(buf);
    }
  }
#else
  Serial.println("[TASK] vTaskList indisponivel (CONFIG_FREERTOS_USE_TRACE_FACILITY=n)");
#endif

#if defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS) && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && \
    defined(CONFIG_FREERTOS_USE_TRACE_FACILITY) && CONFIG_FREERTOS_USE_TRACE_FACILITY
  {
    char *sbuf = (char *)malloc(3072U);
    if (sbuf != nullptr) {
      vTaskGetRunTimeStats(sbuf);
      Serial.println("[TASK] CPU por tarefa (tempo acumulado / % — ver documentacao FreeRTOS IDF):");
      Serial.print(sbuf);
      free(sbuf);
    }
  }
#else
  Serial.println("[TASK] vTaskGetRunTimeStats indisponivel (activar sdkconfig.defaults no projeto).");
#endif

  Serial.println("[TASK] --- fim snapshot ---");
  Serial.flush();
}
