/**
 * @file net_monitor.cpp
 * @brief Implementacao de net_monitor.h — ping ICMP periodico via esp_ping.
 */
#include "net_monitor.h"

#include "app_log.h"
#include "app_settings.h"

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

extern "C" {
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"
}

namespace {

constexpr uint32_t kPingIntervalMs = 60000U;  /**< 60 s entre sondagens. */
constexpr uint32_t kPingTimeoutMs  = 2000U;   /**< Timeout por eco ICMP. */
constexpr uint32_t kTaskStackWords = 4096U;

TaskHandle_t s_task = nullptr;
SemaphoreHandle_t s_done_sem = nullptr;
std::atomic<NetMonitorStatus> s_status{NetMonitorStatus::Disabled};
std::atomic<uint32_t> s_last_latency_ms{0};
std::atomic<uint32_t> s_last_check_ms{0};
std::atomic<bool> s_last_attempt_ok{false};
std::atomic<bool> s_config_dirty{true}; /**< Acorda a task num resize/replace. */

void on_ping_success(esp_ping_handle_t hdl, void * /*args*/) {
  uint32_t elapsed = 0;
  (void)esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
  s_last_latency_ms.store(elapsed, std::memory_order_relaxed);
  s_last_attempt_ok.store(true, std::memory_order_relaxed);
}

void on_ping_timeout(esp_ping_handle_t /*hdl*/, void * /*args*/) {
  s_last_attempt_ok.store(false, std::memory_order_relaxed);
}

void on_ping_end(esp_ping_handle_t /*hdl*/, void * /*args*/) {
  if (s_done_sem != nullptr) {
    (void)xSemaphoreGive(s_done_sem);
  }
}

/**
 * Resolve uma string (IPv4 literal ou hostname) para ip_addr_t.
 * @return true em sucesso (e `out` preenchido), false em caso contrario.
 */
bool resolve_target(const String &host, ip_addr_t *out) {
  if (host.length() == 0 || out == nullptr) {
    return false;
  }
  /* Primeiro caminho: IP literal (nao toca no DNS; mais rapido). */
  if (ipaddr_aton(host.c_str(), out)) {
    return true;
  }
  /* Hostname: depende de DNS. `WiFi.hostByName` ja lida com IPv4. */
  IPAddress arduino_ip;
  if (!WiFi.hostByName(host.c_str(), arduino_ip)) {
    return false;
  }
  IP_ADDR4(out, arduino_ip[0], arduino_ip[1], arduino_ip[2], arduino_ip[3]);
  return true;
}

/**
 * Faz 1 ping sincrono. Preenche `latency_ms` se tiver sucesso.
 * @return true se recebeu resposta antes do timeout.
 */
bool ping_once(const ip_addr_t &target, uint32_t *latency_ms) {
  if (s_done_sem == nullptr) {
    return false;
  }
  /* Garante que o semaforo nao tem contagem pendente de uma sessao anterior. */
  (void)xSemaphoreTake(s_done_sem, 0);

  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.count = 1;
  cfg.interval_ms = 0;
  cfg.timeout_ms = kPingTimeoutMs;
  cfg.target_addr = target;
  cfg.task_stack_size = 3072U;
  cfg.task_prio = 2U;

  esp_ping_callbacks_t cbs = {};
  cbs.on_ping_success = on_ping_success;
  cbs.on_ping_timeout = on_ping_timeout;
  cbs.on_ping_end = on_ping_end;

  s_last_attempt_ok.store(false, std::memory_order_relaxed);
  esp_ping_handle_t hdl = nullptr;
  if (esp_ping_new_session(&cfg, &cbs, &hdl) != ESP_OK) {
    return false;
  }
  if (esp_ping_start(hdl) != ESP_OK) {
    (void)esp_ping_delete_session(hdl);
    return false;
  }
  /* Espera ate `count * (interval+timeout)` + margem. Com count=1 e' ~timeout. */
  const TickType_t wait_ticks = pdMS_TO_TICKS(kPingTimeoutMs + 1000U);
  (void)xSemaphoreTake(s_done_sem, wait_ticks);
  (void)esp_ping_delete_session(hdl);

  const bool ok = s_last_attempt_ok.load(std::memory_order_relaxed);
  if (ok && latency_ms != nullptr) {
    *latency_ms = s_last_latency_ms.load(std::memory_order_relaxed);
  }
  return ok;
}

void task_entry(void * /*arg*/) {
  NetMonitorStatus prev_logged = NetMonitorStatus::Disabled;
  uint32_t next_check_ms = 0;
  for (;;) {
    const String target = app_settings_monitor_ip();
    const bool configured = target.length() > 0;
    const bool wifi_up = WiFi.isConnected();

    /* Wi-Fi em baixo (ou ainda nao configurado) => Desconectado. Trata-se de uma
     * falha real de rede do ponto de vista do operador, independente de haver
     * IP de monitorizacao preenchido. */
    if (!wifi_up) {
      if (s_status.exchange(NetMonitorStatus::Fail, std::memory_order_relaxed) != NetMonitorStatus::Fail) {
        if (prev_logged != NetMonitorStatus::Fail) {
          app_log_write("WARN", "net_monitor: sem Wi-Fi (Desconectado)");
          prev_logged = NetMonitorStatus::Fail;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(2000U));
      continue;
    }

    /* Wi-Fi OK mas IP alvo vazio => Configure (call-to-action). */
    if (!configured) {
      if (s_status.exchange(NetMonitorStatus::Disabled, std::memory_order_relaxed) != NetMonitorStatus::Disabled) {
        if (prev_logged != NetMonitorStatus::Disabled) {
          app_log_write("INFO", "net_monitor: sem IP (Configure)");
          prev_logged = NetMonitorStatus::Disabled;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(2000U));
      continue;
    }

    const uint32_t now = millis();
    const bool config_changed = s_config_dirty.exchange(false, std::memory_order_relaxed);
    if (!config_changed && (int32_t)(now - next_check_ms) < 0) {
      vTaskDelay(pdMS_TO_TICKS(500U));
      continue;
    }

    s_status.store(NetMonitorStatus::Pending, std::memory_order_relaxed);

    ip_addr_t addr{};
    bool ok = false;
    uint32_t lat = 0;
    if (resolve_target(target, &addr)) {
      ok = ping_once(addr, &lat);
    }

    s_last_check_ms.store(millis(), std::memory_order_relaxed);
    const NetMonitorStatus st = ok ? NetMonitorStatus::Ok : NetMonitorStatus::Fail;
    s_status.store(st, std::memory_order_relaxed);
    if (st != prev_logged) {
      if (ok) {
        app_log_writef("INFO", "Ping %s OK (%u ms)", target.c_str(), (unsigned)lat);
      } else {
        app_log_writef("WARN", "Ping %s FALHOU", target.c_str());
      }
      prev_logged = st;
    }
    next_check_ms = millis() + kPingIntervalMs;
  }
}

} // namespace

void net_monitor_init(void) {
  if (s_task != nullptr) {
    return;
  }
  if (s_done_sem == nullptr) {
    s_done_sem = xSemaphoreCreateBinary();
  }
  const BaseType_t ok = xTaskCreatePinnedToCore(task_entry, "net_mon", kTaskStackWords, nullptr, 2, &s_task,
                                                tskNO_AFFINITY);
  if (ok != pdPASS) {
    s_task = nullptr;
    app_log_write("ERROR", "net_monitor: falha a criar tarefa.");
  }
}

void net_monitor_apply_settings(void) {
  s_config_dirty.store(true, std::memory_order_relaxed);
}

NetMonitorStatus net_monitor_status(void) {
  return s_status.load(std::memory_order_relaxed);
}

uint32_t net_monitor_last_latency_ms(void) {
  return s_last_latency_ms.load(std::memory_order_relaxed);
}

uint32_t net_monitor_last_check_ms(void) {
  return s_last_check_ms.load(std::memory_order_relaxed);
}
