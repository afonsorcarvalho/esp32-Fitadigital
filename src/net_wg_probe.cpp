/**
 * @file net_wg_probe.cpp
 * @brief 3 pings ICMP por janela 30s para 10.0.0.1 (server WG). v1.92.
 *
 * Modelado em net_monitor.cpp: sessao esp_ping persistente (count=0,
 * interval_ms=10000 -> ~3 pings/30s), evitando criar/destruir sessao a
 * cada ciclo (causa "create ping task failed" + heap drain).
 *
 * Diff vs net_monitor:
 *  - target fixo 10.0.0.1 (sem NVS)
 *  - cadencia 10s (3 pings por janela 30s)
 *  - loga CADA ping (success/timeout) -- net_monitor so' loga transicoes
 *  - silencia component log "ping_sock" (idempotente com net_monitor)
 */
#include "net_wg_probe.h"
#include "build_features.h"

#if !FITA_ENABLE_WG
/* v1.95 Hibrido: WG disabled -> probe disabled. Stub mantem ligacao. */
void net_wg_probe_init(void) {}
#else  /* FITA_ENABLE_WG */

#include "app_log.h"

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

extern "C" {
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"
int ets_printf(const char *fmt, ...);
}

namespace {

constexpr uint32_t kPingIntervalMs = 10000U;  /* 10s -> ~3 pings/30s */
constexpr uint32_t kPingTimeoutMs  = 3000U;   /* tunel pode ter latencia */
constexpr const char *kTargetIp    = "10.0.0.1";
constexpr uint32_t kTaskStackWords = 3072U;

TaskHandle_t s_task = nullptr;
SemaphoreHandle_t s_lock = nullptr;
esp_ping_handle_t s_session = nullptr;

/* Callbacks correm na task interna "ping" (stack=2048B default lib).
 * v1.93 crashou aqui: app_log_feature_writef ~960B char buffers + vfprintf +
 * localtime_r + File.printf estourou canary stack. Callback agora SO' atomic
 * + ets_printf (sem alloc, sem newlock). app_log diferido para task wg_probe
 * via counters + last_rtt. */
std::atomic<uint32_t> s_ok_count{0};
std::atomic<uint32_t> s_fail_count{0};
std::atomic<uint32_t> s_last_rtt_ms{0};
std::atomic<uint32_t> s_last_seq_ok{0};
std::atomic<uint32_t> s_last_seq_fail{0};

void on_ping_success(esp_ping_handle_t hdl, void * /*args*/) {
  uint32_t rtt = 0, seq = 0;
  (void)esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &rtt, sizeof(rtt));
  (void)esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seq, sizeof(seq));
  s_last_rtt_ms.store(rtt, std::memory_order_relaxed);
  s_last_seq_ok.store(seq, std::memory_order_relaxed);
  s_ok_count.fetch_add(1, std::memory_order_relaxed);
  /* ets_printf directo (sem Arduino Serial buffer, sem newlock). Stack ~100B. */
  ets_printf("[WG-PROBE] OK seq=%u rtt=%u ms\n", (unsigned)seq, (unsigned)rtt);
}

void on_ping_timeout(esp_ping_handle_t hdl, void * /*args*/) {
  uint32_t seq = 0;
  (void)esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seq, sizeof(seq));
  s_last_seq_fail.store(seq, std::memory_order_relaxed);
  s_fail_count.fetch_add(1, std::memory_order_relaxed);
  ets_printf("[WG-PROBE] TIMEOUT seq=%u\n", (unsigned)seq);
}

bool session_create_locked(void) {
  if (s_session != nullptr) return true;
  ip_addr_t addr{};
  if (!ipaddr_aton(kTargetIp, &addr)) return false;

  /* Suprime log interno "ping_sock" (idempotente; net_monitor ja' o faz). */
  esp_log_level_set("ping_sock", ESP_LOG_NONE);

  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.count = 0;
  cfg.interval_ms = kPingIntervalMs;
  cfg.timeout_ms = kPingTimeoutMs;
  cfg.target_addr = addr;
  cfg.task_stack_size = 2048U;
  cfg.task_prio = 2U;

  esp_ping_callbacks_t cbs = {};
  cbs.on_ping_success = on_ping_success;
  cbs.on_ping_timeout = on_ping_timeout;

  esp_ping_handle_t hdl = nullptr;
  if (esp_ping_new_session(&cfg, &cbs, &hdl) != ESP_OK) return false;
  if (esp_ping_start(hdl) != ESP_OK) {
    (void)esp_ping_delete_session(hdl);
    return false;
  }
  s_session = hdl;
  app_log_feature_writef("INFO", "WG_PROBE",
                         "sessao iniciada -> %s interval=%ums",
                         kTargetIp, (unsigned)kPingIntervalMs);
  Serial.printf("[WG-PROBE] sessao iniciada -> %s\n", kTargetIp);
  return true;
}

void session_destroy_locked(void) {
  if (s_session == nullptr) return;
  (void)esp_ping_stop(s_session);
  (void)esp_ping_delete_session(s_session);
  s_session = nullptr;
}

/* Task wg_probe corre com stack 3072 words (~12KB) — pode chamar app_log. */
void task_entry(void * /*arg*/) {
  bool prev_wifi_up = false;
  uint32_t prev_ok = 0, prev_fail = 0;
  uint32_t last_log_ms = 0;
  constexpr uint32_t kLogIntervalMs = 30000U;  /* log resumo cada 30s (window 3 pings) */
  for (;;) {
    const bool wifi_up = WiFi.isConnected();
    if (!wifi_up) {
      if (prev_wifi_up && s_session != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        session_destroy_locked();
        xSemaphoreGive(s_lock);
      }
      prev_wifi_up = false;
      vTaskDelay(pdMS_TO_TICKS(5000U));
      continue;
    }
    if (s_session == nullptr) {
      xSemaphoreTake(s_lock, portMAX_DELAY);
      const bool ok = session_create_locked();
      xSemaphoreGive(s_lock);
      if (!ok) {
        vTaskDelay(pdMS_TO_TICKS(10000U));
        continue;
      }
    }
    prev_wifi_up = wifi_up;

    /* Log resumo cada 30s a partir dos counters atomicos. SD/file safe aqui. */
    const uint32_t now = millis();
    if (now - last_log_ms >= kLogIntervalMs) {
      const uint32_t ok = s_ok_count.load(std::memory_order_relaxed);
      const uint32_t fail = s_fail_count.load(std::memory_order_relaxed);
      const uint32_t d_ok = ok - prev_ok;
      const uint32_t d_fail = fail - prev_fail;
      const uint32_t rtt = s_last_rtt_ms.load(std::memory_order_relaxed);
      app_log_feature_writef("INFO", "WG_PROBE",
          "30s window: ok=%u fail=%u (total ok=%u fail=%u) last_rtt=%ums",
          (unsigned)d_ok, (unsigned)d_fail,
          (unsigned)ok, (unsigned)fail, (unsigned)rtt);
      prev_ok = ok;
      prev_fail = fail;
      last_log_ms = now;
    }
    vTaskDelay(pdMS_TO_TICKS(5000U));
  }
}

} // namespace

void net_wg_probe_init(void) {
  if (s_task != nullptr) return;
  if (s_lock == nullptr) s_lock = xSemaphoreCreateMutex();
  const BaseType_t ok = xTaskCreatePinnedToCore(task_entry, "wg_probe",
                                                kTaskStackWords, nullptr, 1,
                                                &s_task, tskNO_AFFINITY);
  if (ok != pdPASS) {
    s_task = nullptr;
    app_log_feature_write("ERROR", "WG_PROBE", "falha a criar tarefa.");
  }
}

#endif  /* FITA_ENABLE_WG */
