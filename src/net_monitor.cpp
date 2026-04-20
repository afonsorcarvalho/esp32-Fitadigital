/**
 * @file net_monitor.cpp
 * @brief Implementacao de net_monitor.h — ping ICMP periodico via esp_ping.
 *
 * Mantem UMA sessao esp_ping persistente (count=0 = infinito) com
 * interval_ms=60000. Quando o IP ou o estado do Wi-Fi mudam, paramos e
 * recriamos a sessao. Criar uma sessao nova a cada ping (padrao ingenuo)
 * leva a esgotamento de tasks/heap: ja' observado com "create ping task
 * failed" seguido de abort().
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
SemaphoreHandle_t s_lock = nullptr; /**< Protege a criacao/destruicao da sessao. */

esp_ping_handle_t s_session = nullptr;
String s_active_target; /**< Copia do IP/host usado na sessao corrente. */

std::atomic<NetMonitorStatus> s_status{NetMonitorStatus::Disabled};
std::atomic<uint32_t> s_last_latency_ms{0};
std::atomic<uint32_t> s_last_check_ms{0};
std::atomic<bool> s_config_dirty{true};

/** Callbacks esp_ping: chamadas pela task interna da biblioteca. */
void on_ping_success(esp_ping_handle_t hdl, void * /*args*/) {
  uint32_t elapsed = 0;
  (void)esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
  s_last_latency_ms.store(elapsed, std::memory_order_relaxed);
  s_last_check_ms.store(millis(), std::memory_order_relaxed);
  s_status.store(NetMonitorStatus::Ok, std::memory_order_relaxed);
}

void on_ping_timeout(esp_ping_handle_t /*hdl*/, void * /*args*/) {
  s_last_check_ms.store(millis(), std::memory_order_relaxed);
  s_status.store(NetMonitorStatus::Fail, std::memory_order_relaxed);
}

bool resolve_target(const String &host, ip_addr_t *out) {
  if (host.length() == 0 || out == nullptr) {
    return false;
  }
  if (ipaddr_aton(host.c_str(), out)) {
    return true;
  }
  IPAddress arduino_ip;
  if (!WiFi.hostByName(host.c_str(), arduino_ip)) {
    return false;
  }
  IP_ADDR4(out, arduino_ip[0], arduino_ip[1], arduino_ip[2], arduino_ip[3]);
  return true;
}

/** Para e destroi a sessao em curso (se existir). Chamar com `s_lock` detido. */
void session_destroy_locked(void) {
  if (s_session != nullptr) {
    (void)esp_ping_stop(s_session);
    (void)esp_ping_delete_session(s_session);
    s_session = nullptr;
  }
  s_active_target = String();
}

/** (Re)cria sessao persistente para `target`. Chamar com `s_lock` detido. */
bool session_recreate_locked(const String &target) {
  session_destroy_locked();
  if (target.length() == 0) {
    return false;
  }
  ip_addr_t addr{};
  if (!resolve_target(target, &addr)) {
    app_log_writef("WARN", "net_monitor: DNS falhou para '%s'", target.c_str());
    return false;
  }

  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.count = 0; /* loop infinito: evita recriar sessao a cada sondagem. */
  cfg.interval_ms = kPingIntervalMs;
  cfg.timeout_ms = kPingTimeoutMs;
  cfg.target_addr = addr;
  cfg.task_stack_size = 2048U;
  cfg.task_prio = 2U;

  esp_ping_callbacks_t cbs = {};
  cbs.on_ping_success = on_ping_success;
  cbs.on_ping_timeout = on_ping_timeout;
  /* Sem on_ping_end: com count=0 a sessao so termina em esp_ping_stop. */

  esp_ping_handle_t hdl = nullptr;
  esp_err_t err = esp_ping_new_session(&cfg, &cbs, &hdl);
  if (err != ESP_OK) {
    app_log_writef("ERROR", "net_monitor: esp_ping_new_session falhou (%d)", (int)err);
    return false;
  }
  err = esp_ping_start(hdl);
  if (err != ESP_OK) {
    (void)esp_ping_delete_session(hdl);
    app_log_writef("ERROR", "net_monitor: esp_ping_start falhou (%d)", (int)err);
    return false;
  }
  s_session = hdl;
  s_active_target = target;
  app_log_writef("INFO", "net_monitor: sessao iniciada -> %s", target.c_str());
  return true;
}

void task_entry(void * /*arg*/) {
  NetMonitorStatus prev_logged = NetMonitorStatus::Disabled;
  String prev_ip;
  bool prev_wifi_up = false;
  for (;;) {
    const String target = app_settings_monitor_ip();
    const bool configured = target.length() > 0;
    const bool wifi_up = WiFi.isConnected();
    const bool config_changed = s_config_dirty.exchange(false, std::memory_order_relaxed);

    if (!wifi_up) {
      if (prev_wifi_up || s_session != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        session_destroy_locked();
        xSemaphoreGive(s_lock);
      }
      s_status.store(NetMonitorStatus::Fail, std::memory_order_relaxed);
      if (prev_logged != NetMonitorStatus::Fail) {
        app_log_write("WARN", "net_monitor: sem Wi-Fi (Desconectado)");
        prev_logged = NetMonitorStatus::Fail;
      }
      prev_wifi_up = false;
      prev_ip = String();
      vTaskDelay(pdMS_TO_TICKS(2000U));
      continue;
    }

    if (!configured) {
      if (s_session != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        session_destroy_locked();
        xSemaphoreGive(s_lock);
      }
      s_status.store(NetMonitorStatus::Disabled, std::memory_order_relaxed);
      if (prev_logged != NetMonitorStatus::Disabled) {
        app_log_write("INFO", "net_monitor: sem IP (Configure)");
        prev_logged = NetMonitorStatus::Disabled;
      }
      prev_wifi_up = wifi_up;
      prev_ip = String();
      vTaskDelay(pdMS_TO_TICKS(2000U));
      continue;
    }

    /* (Re)cria sessao quando: acabamos de ligar o Wi-Fi, IP mudou, config foi
     * alterada na UI, ou a sessao nao existe por alguma razao. */
    const bool need_recreate = (s_session == nullptr) || config_changed ||
                                (target != s_active_target) ||
                                (!prev_wifi_up);
    if (need_recreate) {
      xSemaphoreTake(s_lock, portMAX_DELAY);
      const bool ok = session_recreate_locked(target);
      xSemaphoreGive(s_lock);
      if (ok) {
        s_status.store(NetMonitorStatus::Pending, std::memory_order_relaxed);
        if (prev_logged != NetMonitorStatus::Pending) {
          prev_logged = NetMonitorStatus::Pending;
        }
      } else {
        s_status.store(NetMonitorStatus::Fail, std::memory_order_relaxed);
        /* Cadencia para nao martelar DNS/init em caso de erro persistente. */
        vTaskDelay(pdMS_TO_TICKS(5000U));
        continue;
      }
    }

    /* Publica transicoes de estado no log. */
    const NetMonitorStatus st = s_status.load(std::memory_order_relaxed);
    if (st != prev_logged) {
      if (st == NetMonitorStatus::Ok) {
        app_log_writef("INFO", "Ping %s OK (%u ms)", target.c_str(),
                       (unsigned)s_last_latency_ms.load(std::memory_order_relaxed));
      } else if (st == NetMonitorStatus::Fail) {
        app_log_writef("WARN", "Ping %s FALHOU", target.c_str());
      }
      prev_logged = st;
    }

    prev_wifi_up = wifi_up;
    prev_ip = target;
    vTaskDelay(pdMS_TO_TICKS(1000U));
  }
}

} // namespace

void net_monitor_init(void) {
  if (s_task != nullptr) {
    return;
  }
  if (s_lock == nullptr) {
    s_lock = xSemaphoreCreateMutex();
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
