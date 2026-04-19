/**
 * @file board_i2c0_bus_lock.cpp
 * @brief Implementacao de board_i2c0_bus_lock.h.
 */
#include "board_i2c0_bus_lock.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "esp_log.h"

static SemaphoreHandle_t s_i2c0_mutex;

/**
 * Timeout maximo para aquisicao do mutex I2C0. portMAX_DELAY pode prender
 * indefinidamente uma task (LVGL, sd_io, net_time) se a stack Espressif ou o
 * bus I2C ficarem presos — disparando TASK_WDT. 2s e' suficiente para
 * operacoes RTC (~ms) e CH422G (ms) com folga para uma transacao em curso.
 */
static constexpr TickType_t kI2c0LockTimeoutTicks = pdMS_TO_TICKS(2000);

static volatile uint32_t s_i2c0_lock_timeouts = 0U;

void board_i2c0_bus_lock_init(void) {
  if (s_i2c0_mutex == nullptr) {
    s_i2c0_mutex = xSemaphoreCreateMutex();
  }
}

void board_i2c0_bus_lock(void) {
  if (s_i2c0_mutex == nullptr) {
    return;
  }
  if (xSemaphoreTake(s_i2c0_mutex, kI2c0LockTimeoutTicks) == pdTRUE) {
    return;
  }
  /**
   * Timeout: prefere seguir em frente sem lock (transacao I2C pode falhar ou
   * colidir, mas o driver i2c tem proprio timeout) do que prender a task. O
   * contador permite diagnostico posterior via board_i2c0_bus_lock_timeouts().
   */
  const uint32_t total = s_i2c0_lock_timeouts + 1U;
  s_i2c0_lock_timeouts = total;
  ESP_EARLY_LOGW("i2c0lock", "timeout take (total=%lu)", (unsigned long)total);
}

void board_i2c0_bus_unlock(void) {
  if (s_i2c0_mutex != nullptr) {
    (void)xSemaphoreGive(s_i2c0_mutex);
  }
}

uint32_t board_i2c0_bus_lock_timeouts(void) {
  return s_i2c0_lock_timeouts;
}

void board_i2c0_bus_quiet_ioexpander_serial_logs(void) {
  (void)esp_log_level_set("ESP_IOExpander", ESP_LOG_NONE);
  (void)esp_log_level_set("ch422g", ESP_LOG_NONE);
}
