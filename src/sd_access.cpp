/**
 * @file sd_access.cpp
 * @brief Tarefa sd_io: fila de trabalhos com acesso exclusivo ao cartao SD.
 *
 * Modulos externos registam tickers via sd_access_register_tick() para obter
 * ciclos periodicos no contexto sd_io sem que este modulo os conheca.
 *
 * Zero alocacao dinamica de heap interna em runtime:
 *   - SdJob vem de um pool estatico (s_job_pool) protegido por counting semaphore.
 *   - Semaforo de sincronizacao (sd_access_sync) e' criado na stack do chamador
 *     via xSemaphoreCreateBinaryStatic (StaticSemaphore_t).
 * Isso elimina fragmentacao de heap interna que, com o tempo, impedia a criacao
 * de semaforos/jobs e causava falha silenciosa de acesso ao SD.
 */

#include "sd_access.h"

#include <Arduino.h>
#include <time.h>
#include <string.h>

#include "ff.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* ets_printf: ROM function, zero VFS / zero FreeRTOS stack overhead.
 * Safe to call from any context including deep FatFs paths and interrupt
 * handlers. Use instead of Serial.printf / log_x inside sd_io_task to
 * avoid the VFS UART stack frame (~2 KB) that causes Double Exception. */
extern "C" int ets_printf(const char *fmt, ...);

namespace {

static constexpr size_t kMaxTickers = 4U;
static SdWorkerTickFn s_tickers[kMaxTickers] = {};
static size_t s_ticker_count = 0;

/**
 * Pool estatico de jobs — evita new/delete em runtime.
 * Dimensao 6: cobre as tasks que podem chamar sd_access_sync simultaneamente
 * (LVGL/UI, net_svc, web_portal/async_tcp, app_log, net_time) com 1 de margem.
 */
static constexpr unsigned kJobPoolSize = 6U;

struct SdJob {
  std::function<void()> fn;
  SemaphoreHandle_t done{nullptr};
  volatile bool in_use{false};
};

static SdJob s_job_pool[kJobPoolSize];
static StaticSemaphore_t s_pool_sem_buf;
/** Counting semaphore: valor inicial = kJobPoolSize (slots livres). */
static SemaphoreHandle_t s_pool_sem = nullptr;

/**
 * Empresta um SdJob do pool (zero heap).
 * @param wait Tempo maximo de espera por um slot livre.
 * @return Ponteiro para o job reservado, ou nullptr se timeout.
 */
static SdJob *pool_acquire(TickType_t wait) {
  if (s_pool_sem == nullptr || xSemaphoreTake(s_pool_sem, wait) != pdTRUE) {
    return nullptr;
  }
  for (unsigned i = 0; i < kJobPoolSize; ++i) {
    if (!s_job_pool[i].in_use) {
      s_job_pool[i].in_use = true;
      s_job_pool[i].fn = nullptr;
      s_job_pool[i].done = nullptr;
      return &s_job_pool[i];
    }
  }
  xSemaphoreGive(s_pool_sem);
  return nullptr;
}

/** Devolve um SdJob ao pool (zero heap). */
static void pool_release(SdJob *job) {
  if (job == nullptr) {
    return;
  }
  job->fn = nullptr;
  job->done = nullptr;
  job->in_use = false;
  if (s_pool_sem != nullptr) {
    xSemaphoreGive(s_pool_sem);
  }
}

static bool s_mounted = false;
static volatile uint32_t s_sd_modified_ms = 0;
static QueueHandle_t s_queue = nullptr;
static TaskHandle_t s_sd_task = nullptr;

static constexpr UBaseType_t kSdTaskPrio = 1U;
/**
 * Stack da task sd_io em BYTES (ESP-IDF xTaskCreatePinnedToCore usa bytes).
 *
 * Aumentado de 8192 para 16384 apos crash observado em 2026-04-25:
 *   Double exception (EXCCAUSE=2) em _xt_context_save com EXCVADDR=0x20.
 *   addr2line: 0x420954d3 = uart_write (vfs_uart.c:213) -- log_w("no token")
 *   em sdCommand() causou stack overflow no caminho profundo:
 *     sd_io_task -> sd_hotplug_tick -> f_opendir/f_readdir (FatFs) ->
 *     diskio read -> sdCommand loop -> log_w -> esp_log_write -> vfs uart.
 *   A medicao anterior de "~5 KB livres" nao cobria este caminho sob stress.
 *   16 KB da margem confortavel (+8 KB) para paths FatFs + log + std::function.
 *
 * Aumentado de 16384 para 32768 em 2026-04-25 (segundo crash -- poll 8):
 *   Mesma assinatura: EXCCAUSE=2, EXCVADDR=0xb33ffffc (stack sentinel poison).
 *   A9/A13/A14/A15=0xb33fffff nos registos: stack overflow mesmo a 16 KB.
 *   Fix duplo: (1) stack 32 KB; (2) log_w/log_e removidos das funcoes quentes
 *   sdWait/sdSelectCard/sdCommand/sdReadSectors -- VFS logging nestas funcoes
 *   e o verdadeiro motor do overflow no caminho FatFs profundo.
 *
 * Fix 4 (2026-04-25): Serial.printf dentro da sd_io_task substituido por
 *   ets_printf (ROM, sem VFS, stack footprint ~100 B vs ~2 KB do VFS path).
 *   Serial.printf -> vfs_uart write consome stack extra mesmo quando chamado
 *   de sd_io_task directamente (nao apenas do path FatFs profundo), confirmado
 *   por EXCVADDR=0xb33ffffc apos 25 min de uptime com stack 32 KB.
 */
static constexpr uint32_t kSdTaskStackBytes = 32768U;
static constexpr uint32_t kQueueDepth = 24U;
static constexpr BaseType_t kSdTaskCore = (BaseType_t)ARDUINO_RUNNING_CORE;

/** Intervalo entre logs de diagnostico de heap interna (ms). */
static constexpr uint32_t kHeapDiagIntervalMs = 60000U;
static uint32_t s_last_heap_diag_ms = 0;

static void sd_io_task(void * /*arg*/) {
  /**
   * Debug: usa ets_printf (ROM, zero VFS stack) em vez de Serial.printf.
   * Serial.printf -> vfs_uart -> esp_log_write consome ~2 KB de stack extra
   * no contexto da sd_io_task e causou Double Exception (Fix 4).
   */
  ets_printf("[sd_io] start: configured=%u bytes  initial_free=%u words\n",
             (unsigned)kSdTaskStackBytes,
             (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  uint32_t s_last_stack_log_ms = 0;
  for (;;) {
    SdJob *job = nullptr;
    if (s_queue != nullptr && xQueueReceive(s_queue, &job, 0) == pdTRUE) {
      if (job != nullptr) {
        if (job->fn) {
          job->fn();
        }
        if (job->done != nullptr) {
          (void)xSemaphoreGive(job->done);
          /* Job sincrono: o chamador (sd_access_sync) libera o slot
           * apos xSemaphoreTake retornar, pois precisa que o job
           * exista enquanto espera o semaforo. */
        } else {
          pool_release(job);
        }
      }
    }
    for (size_t i = 0; i < s_ticker_count; ++i) {
      s_tickers[i]();
    }

    const uint32_t now = millis();
    if ((now - s_last_heap_diag_ms) >= kHeapDiagIntervalMs) {
      s_last_heap_diag_ms = now;
      ets_printf("[SD] heap_int_free=%u min=%u\n",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if ((now - s_last_stack_log_ms) >= 5000U) {
      s_last_stack_log_ms = now;
      ets_printf("[sd_io] stack_hwm=%u words (lower=closer to overflow)\n",
                 (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    }

    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

} // namespace

void sd_access_register_tick(SdWorkerTickFn fn) {
  if (fn && s_ticker_count < kMaxTickers) {
    s_tickers[s_ticker_count++] = fn;
  }
}

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
  if (s_pool_sem == nullptr) {
    s_pool_sem = xSemaphoreCreateCountingStatic(
        kJobPoolSize, kJobPoolSize, &s_pool_sem_buf);
  }
  if (s_queue == nullptr) {
    s_queue = xQueueCreate(kQueueDepth, sizeof(SdJob *));
    if (s_queue == nullptr) {
      return;
    }
  }
  const BaseType_t ok =
      xTaskCreatePinnedToCore(sd_io_task, "sd_io", kSdTaskStackBytes, nullptr, kSdTaskPrio, &s_sd_task, kSdTaskCore);
  if (ok != pdPASS) {
    s_sd_task = nullptr;
  }
}

static void sd_access_sync_impl(const std::function<void()> &fn, bool to_front) {
  if (!fn) {
    return;
  }
  if (s_sd_task != nullptr && xTaskGetCurrentTaskHandle() == s_sd_task) {
    fn();
    return;
  }
  if (s_queue == nullptr) {
    fn();
    return;
  }

  StaticSemaphore_t sem_buf;
  SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&sem_buf);

  SdJob *job = pool_acquire(portMAX_DELAY);
  if (job == nullptr) {
    return;
  }
  job->fn = fn;
  job->done = done;
  const BaseType_t qok =
      to_front ? xQueueSendToFront(s_queue, &job, portMAX_DELAY) : xQueueSend(s_queue, &job, portMAX_DELAY);
  if (qok != pdTRUE) {
    pool_release(job);
    return;
  }
  (void)xSemaphoreTake(done, portMAX_DELAY);
  pool_release(job);
}

void sd_access_sync(const std::function<void()> &fn) {
  sd_access_sync_impl(fn, false);
}

void sd_access_sync_front(const std::function<void()> &fn) {
  sd_access_sync_impl(fn, true);
}

void sd_access_async(const std::function<void()> &fn) {
  if (!fn || s_queue == nullptr) {
    return;
  }
  if (s_sd_task != nullptr && xTaskGetCurrentTaskHandle() == s_sd_task) {
    fn();
    return;
  }
  SdJob *job = pool_acquire(pdMS_TO_TICKS(100));
  if (job == nullptr) {
    return;
  }
  job->fn = fn;
  job->done = nullptr;
  if (xQueueSend(s_queue, &job, portMAX_DELAY) != pdTRUE) {
    pool_release(job);
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
