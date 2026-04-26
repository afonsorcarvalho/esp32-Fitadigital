#include "boot_journal.h"

#include "sd_access.h"
#include "app_settings.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_system.h"
#include "esp_rom_sys.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *reset_reason_to_str(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT_PIN";
    case ESP_RST_SW:        return "SW_RESET";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_UNKNOWN:   /* fallthrough */
    default:                return "UNKNOWN";
  }
}

static const char *const kBootLogFlashPath = "/boot.log";
static const char *const kBootLogSdPath = "/boot.log";
static constexpr uint32_t kMirrorPollMs = 5000U;
static constexpr size_t kCopyBufSize = 512U;
/** Buffer em RAM até `boot_journal_flush_to_spiffs()` (evita SPIFFS open/close repetido com LVGL activo). */
static constexpr size_t kBootJournalRamMax = 8192U;

static SemaphoreHandle_t s_boot_log_mutex = nullptr;
static TaskHandle_t s_boot_mirror_task = nullptr;
static bool s_spiffs_ok = false;
static char s_boot_journal_ram[kBootJournalRamMax];
static size_t s_boot_journal_ram_len = 0U;

static void boot_log_lock(void) {
  if (s_boot_log_mutex != nullptr) {
    (void)xSemaphoreTake(s_boot_log_mutex, portMAX_DELAY);
  }
}

static void boot_log_unlock(void) {
  if (s_boot_log_mutex != nullptr) {
    (void)xSemaphoreGive(s_boot_log_mutex);
  }
}

bool boot_journal_init(void) {
  if (s_boot_log_mutex == nullptr) {
    s_boot_log_mutex = xSemaphoreCreateMutex();
  }
  /* app_settings_init() foi chamado antes deste ponto em app.cpp. */
  app_settings_boot_count_increment();
  s_spiffs_ok = SPIFFS.begin(true);
  return s_spiffs_ok;
}

bool boot_journal_reset(void) {
  if (!s_spiffs_ok) {
    return false;
  }
  const esp_reset_reason_t reason = esp_reset_reason();
  boot_log_lock();
  s_boot_journal_ram_len = 0U;
  {
    const int n = snprintf(s_boot_journal_ram, kBootJournalRamMax,
                           "BOOT START | ms=%lu | prev_reset=%s(%d) | heap=%lu/%lu\n",
                           (unsigned long)millis(), reset_reason_to_str(reason), (int)reason,
                           (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getHeapSize());
    if (n > 0 && (size_t)n < kBootJournalRamMax) {
      s_boot_journal_ram_len = (size_t)n;
    }
  }
  boot_log_unlock();
  /**
   * Reset reason tambem para Serial: facilita correlacionar com o monitor sem
   * depender do flush SPIFFS/SD.
   */
  Serial.printf("[BOOT] prev_reset=%s(%d) free_heap=%lu\n", reset_reason_to_str(reason), (int)reason,
                (unsigned long)ESP.getFreeHeap());
  return true;
}

void boot_journal_append(const char *level, const char *message) {
  if (!s_spiffs_ok || message == nullptr) {
    return;
  }
  const char *lvl = (level != nullptr && level[0] != '\0') ? level : "INFO";
  boot_log_lock();
  const size_t room = (kBootJournalRamMax > s_boot_journal_ram_len)
                          ? (kBootJournalRamMax - s_boot_journal_ram_len - 1U)
                          : 0U;
  if (room < 48U) {
    boot_log_unlock();
    return;
  }
  const int n = snprintf(s_boot_journal_ram + s_boot_journal_ram_len, room, "%lu | %s | %s\n",
                         (unsigned long)millis(), lvl, message);
  if (n > 0 && (size_t)n <= room) {
    s_boot_journal_ram_len += (size_t)n;
  }
  boot_log_unlock();
}

bool boot_journal_flush_to_spiffs(void) {
  if (!s_spiffs_ok) {
    return false;
  }
  boot_log_lock();
  (void)SPIFFS.remove(kBootLogFlashPath);
  File f = SPIFFS.open(kBootLogFlashPath, FILE_WRITE);
  if (!f) {
    boot_log_unlock();
    return false;
  }
  const size_t w = f.write((const uint8_t *)s_boot_journal_ram, s_boot_journal_ram_len);
  f.close();
  boot_log_unlock();
  return w == s_boot_journal_ram_len;
}

bool boot_journal_copy_to_sd(void) {
  if (!s_spiffs_ok) {
    return false;
  }

  boot_log_lock();
  File src = SPIFFS.open(kBootLogFlashPath, FILE_READ);
  if (!src) {
    boot_log_unlock();
    return false;
  }
  const size_t sz = src.size();
  uint8_t *const heap = (uint8_t *)malloc(sz);
  if (heap == nullptr) {
    src.close();
    boot_log_unlock();
    return false;
  }
  const size_t nread = src.read(heap, sz);
  src.close();
  boot_log_unlock();

  bool ok = false;
  sd_access_sync([&] {
    if (SD.cardType() == CARD_NONE) {
      return;
    }
    SD.remove(kBootLogSdPath);
    File dst = SD.open(kBootLogSdPath, FILE_WRITE);
    if (!dst) {
      return;
    }
    (void)dst.write(heap, nread);
    dst.close();
    ok = true;
  });
  free(heap);
  return ok;
}

static void boot_journal_mirror_task(void * /*arg*/) {
  size_t last_flash_size = 0U;
  for (;;) {
    if (s_spiffs_ok) {
      File src = SPIFFS.open(kBootLogFlashPath, FILE_READ);
      if (src) {
        const size_t now_size = src.size();
        src.close();
        if (now_size != last_flash_size && boot_journal_copy_to_sd()) {
          last_flash_size = now_size;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kMirrorPollMs));
  }
}

void boot_journal_start_sd_mirror_task(void) {
  if (s_boot_mirror_task != nullptr) {
    return;
  }
  const BaseType_t ok = xTaskCreatePinnedToCore(boot_journal_mirror_task, "boot_log_mirror",
                                                 4096U / sizeof(StackType_t), nullptr, 1U,
                                                 &s_boot_mirror_task, (BaseType_t)ARDUINO_RUNNING_CORE);
  if (ok != pdPASS) {
    s_boot_mirror_task = nullptr;
  }
}
