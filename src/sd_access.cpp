/**
 * @file sd_access.cpp
 * @brief Tarefa sd_io: fila de trabalhos + tick FTP (SimpleFTPServer) no mesmo contexto.
 */

#include "sd_access.h"

#include "net_services.h"

#include <Arduino.h>
#include <new>
#include <time.h>
#include <string.h>

#include "ff.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

struct SdJob {
  std::function<void()> fn;
  SemaphoreHandle_t done{nullptr};
};

static bool s_mounted = false;
static volatile uint32_t s_sd_modified_ms = 0;
static QueueHandle_t s_queue = nullptr;
static TaskHandle_t s_sd_task = nullptr;

static constexpr UBaseType_t kSdTaskPrio = 1U;
static constexpr uint32_t kSdTaskStackWords = 8192U / sizeof(StackType_t);
static constexpr uint32_t kQueueDepth = 24U;
static constexpr BaseType_t kSdTaskCore = (BaseType_t)ARDUINO_RUNNING_CORE;

static void sd_io_task(void * /*arg*/) {
  for (;;) {
    SdJob *job = nullptr;
    if (s_queue != nullptr && xQueueReceive(s_queue, &job, 0) == pdTRUE) {
      if (job != nullptr) {
        if (job->fn) {
          job->fn();
        }
        if (job->done != nullptr) {
          (void)xSemaphoreGive(job->done);
        }
        delete job;
      }
    }
    net_services_sd_worker_tick();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

} // namespace

void sd_access_set_mounted(bool mounted) { s_mounted = mounted; }

bool sd_access_is_mounted(void) { return s_mounted; }

void sd_access_notify_changed(void) {
  s_sd_modified_ms = millis();
  if (s_sd_modified_ms == 0) {
    s_sd_modified_ms = 1;
  }
}

uint32_t sd_access_last_modified_ms(void) { return s_sd_modified_ms; }

void sd_access_start_task(void) {
  if (s_sd_task != nullptr) {
    return;
  }
  if (s_queue == nullptr) {
    s_queue = xQueueCreate(kQueueDepth, sizeof(SdJob *));
    if (s_queue == nullptr) {
      return;
    }
  }
  const BaseType_t ok =
      xTaskCreatePinnedToCore(sd_io_task, "sd_io", kSdTaskStackWords, nullptr, kSdTaskPrio, &s_sd_task, kSdTaskCore);
  if (ok != pdPASS) {
    s_sd_task = nullptr;
  }
}

void sd_access_sync(const std::function<void()> &fn) {
  if (!fn) {
    return;
  }
  if (s_sd_task != nullptr && xTaskGetCurrentTaskHandle() == s_sd_task) {
    fn();
    return;
  }
  if (s_queue == nullptr) {
    /* Arranque: antes de sd_access_start_task (so' setup single-thread). */
    fn();
    return;
  }
  SemaphoreHandle_t done = xSemaphoreCreateBinary();
  if (done == nullptr) {
    fn();
    return;
  }
  SdJob *job = new (std::nothrow) SdJob;
  if (job == nullptr) {
    vSemaphoreDelete(done);
    fn();
    return;
  }
  job->fn = fn;
  job->done = done;
  if (xQueueSend(s_queue, &job, portMAX_DELAY) != pdTRUE) {
    delete job;
    vSemaphoreDelete(done);
    fn();
    return;
  }
  (void)xSemaphoreTake(done, portMAX_DELAY);
  vSemaphoreDelete(done);
}

void sd_access_async(const std::function<void()> &fn) {
  if (!fn || s_queue == nullptr) {
    return;
  }
  if (s_sd_task != nullptr && xTaskGetCurrentTaskHandle() == s_sd_task) {
    fn();
    return;
  }
  SdJob *job = new (std::nothrow) SdJob;
  if (job == nullptr) {
    return;
  }
  job->fn = fn;
  job->done = nullptr;
  if (xQueueSend(s_queue, &job, portMAX_DELAY) != pdTRUE) {
    delete job;
  }
}

time_t sd_access_fat_mtime(const char *vfs_path) {
  if (vfs_path == nullptr || vfs_path[0] == '\0') {
    return (time_t)0;
  }

  char fat[256];
  static constexpr char kMountPrefix[] = "/sd/";
  static constexpr size_t kMountPrefixLen = sizeof(kMountPrefix) - 1;

  const char *rel = vfs_path;
  if (strncmp(rel, kMountPrefix, kMountPrefixLen) == 0) {
    rel = vfs_path + kMountPrefixLen;
  } else if (strcmp(rel, "/sd") == 0) {
    rel = "";
  } else if (rel[0] == '/') {
    rel = vfs_path + 1;
  }
  snprintf(fat, sizeof(fat), "0:/%s", rel);

  FILINFO fno;
  memset(&fno, 0, sizeof(fno));
  if (f_stat(fat, &fno) != FR_OK || fno.fdate == 0) {
    return (time_t)0;
  }

  struct tm ti = {};
  ti.tm_year = ((fno.fdate >> 9) & 0x7F) + 80;
  ti.tm_mon  = ((fno.fdate >> 5) & 0x0F) - 1;
  ti.tm_mday = fno.fdate & 0x1F;
  ti.tm_hour = (fno.ftime >> 11) & 0x1F;
  ti.tm_min  = (fno.ftime >> 5) & 0x3F;
  ti.tm_sec  = (fno.ftime & 0x1F) * 2;
  ti.tm_isdst = -1;

  const time_t t = mktime(&ti);
  return (t == (time_t)-1) ? (time_t)0 : t;
}
