/**
 * @file net_wireguard.cpp
 * @brief WireGuard via biblioteca ciniml/WireGuard-ESP32 (README do repositorio).
 *
 * Watchdog/probe pings foram removidos em v1.67 — consumiam ~5K heap interno
 * permanente e dispavam HEAP_GUARD em <30s pos-boot. Para manter NAT mapping
 * vivo, configurar PersistentKeepalive=25 no peer config DO SERVER (lado wg0.conf).
 * Lib client tem keep_alive=25 (patch FitaDigital em WireGuard.cpp) mas isso so
 * funciona com keypair valid; PersistentKeepalive server-side garante traffic
 * bilateral mesmo durante stale state.
 */
#include "net_wireguard.h"
#include "build_features.h"

#if !FITA_ENABLE_WG
/* v1.95 Hibrido: WireGuard desactivado (FITA_ENABLE_WG=0).
 * Lib smartalock/wireguard-lwip upstream bug + arquitetura Arduino-ESP32 3.X
 * instavel. Codigo preservado em disco para retomar futuramente. Stubs
 * mantem ligacao com callers (net_services, ui_app, web_portal). */
#include <stdio.h>
void net_wireguard_init(void) {}
void net_wireguard_apply(void) {}
void net_wireguard_pause_watchdog(void) {}
void net_wireguard_resume_watchdog(void) {}
size_t net_wireguard_status_json(char *out, size_t out_sz) {
  if (out == nullptr || out_sz < 32U) return 0;
  const int n = snprintf(out, out_sz, "{\"active\":false,\"disabled\":true}");
  return (n > 0 && (size_t)n < out_sz) ? (size_t)n : 0U;
}
#else  /* FITA_ENABLE_WG */

#include "app_log.h"
#include "app_settings.h"
#include "net_services.h"
#include "panic_logger.h"
#include "service_supervisor.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WireGuard-ESP32.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/pbuf.h>
#include <string.h>

static WireGuard s_wg;
static bool s_wg_active = false;

static char s_wg_priv[128];
static char s_wg_pub[128];
static char s_wg_ep[128];

/* Re-apply keepalive (v1.69→v1.76) — biblioteca cliente entra em rekey storm
 * pos-180s: envia INITIATIONs continuos com state corrompido, server rejeita
 * todas como "Invalid handshake initiation" (MAC1/static-decrypt fail).
 *
 * v1.69: detecao via is_peer_up() — reativa apos peer down 100s.
 * v1.71: detecao via in_filter_fn — FALHA (WG keepalive bypass filter).
 * v1.72: escalation esp_restart() apos 2 fails consec.
 * v1.73: REMOVIDA escalation esp_restart() + PREEMPTIVE 100s. Gate
 *        `!s_ever_up continue` causou zombie pos-#1 (task idle quando
 *        peer nao recupera).
 * v1.74: tentou remover resets s_ever_up → reboot loop ~3min. Revertido.
 * v1.75: revert v1.74. Comportamento v1.73 (zombie estavel).
 * v1.76: SIMPLIFICACAO RADICAL. Substituida logica complexa (preemptive +
 *        reactive + observe + consec_fail) por TIMER CEGO 90s. Task chama
 *        wg_apply_locked() unconditionally cada 90s, INDEPENDENTE de
 *        peer_up state. Bypass completo zombie: re-apply nunca para.
 *        is_peer_up() so' usado para telemetria (log primeiro handshake).
 *        Rate 90s ainda mais conservador que v1.73 preemptive 100s.
 *
 * Diagnostico Fase 0 sessao 2026-05-14: pcap server vps51163, kernel dmesg
 * wireguard +p. Server rejeita TODA INIT pos-1o-handshake. Causa raiz lib
 * smartalock/wireguard-lwip upstream. Match issue #29. end()+begin()
 * ressuscita ciclo handshake-OK ate proxima janela rekey broken. */
static TaskHandle_t s_keepalive_task = nullptr;
static SemaphoreHandle_t s_apply_mtx = nullptr;
static volatile uint32_t s_last_up_ms = 0;
static volatile uint32_t s_last_apply_ms = 0;
static volatile bool s_ever_up = false;
static volatile uint32_t s_reapply_count = 0;
static volatile uint32_t s_last_rx_ms = 0;
static volatile bool s_ever_rx = false;

static void wg_apply_locked(void);

/* v1.82: WiFi self-healing layered escalation state.
 *
 * Arduino-ESP32 WiFi.setAutoReconnect(true) confirmado estagnar pos-ASSOC_LEAVE
 * sob heap-pressure (sessao 2026-05-15: 37+ min sem reconnect spontaneo). Sem
 * kick explicito, device fica offline indefinidamente. Escalacao:
 *   T1 = 30s sem WL_CONNECTED   -> SOFT: net_wifi_begin_saved (Arduino API),
 *                                  uma unica tentativa por janela down.
 *   T2 = 5min ainda down        -> HARD: esp_wifi_stop/start + net_wifi_begin_saved
 *                                  (full IDF stack restart). REPETIVEL a cada 5 min
 *                                  enquanto WiFi nao voltar.
 *
 * Sem esp_restart() — constraint zero-reboot do device. Device fica online com
 * outras tasks (sd_io, RS485, LVGL, MQTT-quando-rede-volta) mesmo enquanto rede
 * estiver intermitente. Hard reset isola completamente o WiFi sem afectar resto.
 *
 * Executa no mesmo task que o WG re-apply (tick 20s). WG re-apply gated por
 * WiFi up — sem WiFi nao faz sentido WG. */
static volatile uint32_t s_wifi_down_since_ms = 0;
static volatile bool     s_wifi_soft_tried    = false;
static volatile uint32_t s_wifi_last_hard_ms  = 0;  /* timestamp ultimo hard reset */
static volatile uint32_t s_wifi_soft_count    = 0;
static volatile uint32_t s_wifi_hard_count    = 0;

/* Devolve true se WiFi up neste tick; false se down (caller skip WG re-apply). */
static bool wifi_keepalive_tick(uint32_t now_ms) {
  /* v1.89: removed service_supervisor_heartbeat("wifi") — WiFi nao esta
   * registado no supervisor (ver wg_keepalive_start_once nota v1.89). */
  if (WiFi.status() == WL_CONNECTED) {
    if (s_wifi_down_since_ms != 0) {
      const uint32_t down_ms = now_ms - s_wifi_down_since_ms;
      Serial.printf("[NET-KA] WiFi voltou apos %u ms (soft=%u hard=%u)\n",
                    (unsigned)down_ms, (unsigned)s_wifi_soft_count,
                    (unsigned)s_wifi_hard_count);
      app_log_feature_writef("INFO", "WIFI",
                             "Voltou apos %u s (soft=%u hard=%u).",
                             (unsigned)(down_ms / 1000U),
                             (unsigned)s_wifi_soft_count,
                             (unsigned)s_wifi_hard_count);
    }
    s_wifi_down_since_ms = 0;
    s_wifi_soft_tried = false;
    s_wifi_last_hard_ms = 0;
    return true;
  }

  /* WiFi down */
  if (s_wifi_down_since_ms == 0) {
    s_wifi_down_since_ms = now_ms;
    Serial.println("[NET-KA] WiFi DOWN detectado");
    app_log_feature_write("WARN", "WIFI", "WiFi DOWN detectado.");
    return false;
  }

  const uint32_t down_ms = now_ms - s_wifi_down_since_ms;
  constexpr uint32_t kSoftThresholdMs = 30000U;   /* 30 s — 1x soft */
  constexpr uint32_t kHardThresholdMs = 300000U;  /* 5 min — hard repetivel */

  /* Hard reset repetivel: a cada 5 min sem WiFi (medido desde inicio do down
   * OU desde ultimo hard, o que for mais tarde). */
  const uint32_t since_last_hard = (s_wifi_last_hard_ms == 0)
                                       ? down_ms
                                       : (now_ms - s_wifi_last_hard_ms);
  if (down_ms >= kHardThresholdMs && since_last_hard >= kHardThresholdMs) {
    s_wifi_last_hard_ms = now_ms;
    s_wifi_hard_count++;
    Serial.printf("[NET-KA] WiFi down %u s — HARD reset stack (#%u)\n",
                  (unsigned)(down_ms / 1000U), (unsigned)s_wifi_hard_count);
    app_log_feature_writef("WARN", "WIFI",
                           "Hard reset stack apos %u s (#%u).",
                           (unsigned)(down_ms / 1000U),
                           (unsigned)s_wifi_hard_count);
    panic_breadcrumb_set("wifi_ka:hard:stop");
    (void)esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(200));
    panic_breadcrumb_set("wifi_ka:hard:start");
    (void)esp_wifi_start();
    vTaskDelay(pdMS_TO_TICKS(200));
    panic_breadcrumb_set("wifi_ka:hard:begin_saved");
    net_wifi_begin_saved();
    panic_breadcrumb_clear();
    return false;
  }

  if (!s_wifi_soft_tried && down_ms >= kSoftThresholdMs) {
    s_wifi_soft_tried = true;
    s_wifi_soft_count++;
    Serial.printf("[NET-KA] WiFi down %u s — SOFT reconnect (#%u)\n",
                  (unsigned)(down_ms / 1000U), (unsigned)s_wifi_soft_count);
    app_log_feature_writef("WARN", "WIFI",
                           "Soft reconnect apos %u s (#%u).",
                           (unsigned)(down_ms / 1000U),
                           (unsigned)s_wifi_soft_count);
    panic_breadcrumb_set("wifi_ka:soft:begin_saved");
    net_wifi_begin_saved();
    panic_breadcrumb_clear();
    return false;
  }

  return false;
}

/* in_filter_fn sentinel — atualiza last_rx_ms mas NAO eh usado para detection
 * (ver comment v1.72 acima). Util para diagnostico via /api/system/status
 * futuro: se ever_rx ficar true alguma vez, sabemos que data packets chegaram. */
static int wg_in_filter(struct pbuf *p) {
  (void)p;
  s_last_rx_ms = millis();
  if (!s_ever_rx) s_ever_rx = true;
  return 0;
}

/* v1.76 task simplificada: timer cego 90s. Substitui logica preemptive +
 * reactive + observe + consec_fail por re-apply incondicional periodico.
 * is_peer_up() so' p/ telemetria (log primeiro handshake). */
static void wg_keepalive_task(void *arg) {
  const TickType_t tick = pdMS_TO_TICKS(20000U);
  const uint32_t force_reapply_ms = 90000U; /* re-apply blind cada 90s */
  for (;;) {
    vTaskDelay(tick);
    /* v1.89: removed service_supervisor_heartbeat("wg") — WG nao registado. */
    /* v1.82: WiFi self-healing antes do WG. Sem WiFi nao faz sentido WG;
     * tambem evita s_wg.begin() ler netif_default invalido. */
    if (!wifi_keepalive_tick(millis())) continue;
    if (!s_wg_active) continue;
    /* Telemetria peer_up — so' p/ log primeiro handshake. NAO gate o re-apply. */
    if (s_wg.is_peer_up()) {
      s_last_up_ms = millis();
      if (!s_ever_up) {
        s_ever_up = true;
        Serial.println("[WG-KA] peer up (first handshake)");
        app_log_feature_write("INFO", "WIREGUARD", "Peer up (first handshake).");
      }
    }
    /* Re-apply incondicional cada N segundos. Bypass zombie state.
     * Cada apply: ~200ms (end+begin). Rate 90s = ~0.2% CPU. */
    uint32_t since_apply = millis() - s_last_apply_ms;
    if (since_apply < force_reapply_ms) continue;
    s_reapply_count++;
    Serial.printf("[WG-KA] periodic re-apply (apos %us) #%u\n",
                  (unsigned)(since_apply / 1000U), (unsigned)s_reapply_count);
    app_log_feature_writef("INFO", "WIREGUARD",
                           "Re-apply periodico apos %us #%u.",
                           (unsigned)(since_apply / 1000U), (unsigned)s_reapply_count);
    if (s_apply_mtx != nullptr) {
      xSemaphoreTake(s_apply_mtx, portMAX_DELAY);
    }
    wg_apply_locked();
    if (s_apply_mtx != nullptr) {
      xSemaphoreGive(s_apply_mtx);
    }
  }
}

static int wg_supervisor_restart(void) {
  if (s_apply_mtx != nullptr) {
    xSemaphoreTake(s_apply_mtx, portMAX_DELAY);
  }
  s_wg.end();
  s_wg_active = false;
  if (s_apply_mtx != nullptr) {
    xSemaphoreGive(s_apply_mtx);
  }
  vTaskDelay(pdMS_TO_TICKS(100));
  net_wireguard_apply();  /* takes mutex internally */
  return 0;
}

static void wg_keepalive_start_once(void) {
  if (s_apply_mtx == nullptr) {
    s_apply_mtx = xSemaphoreCreateMutex();
  }
  if (s_keepalive_task == nullptr) {
    /* v1.77: stack 3072→8192. crash-analyzer sessao 2026-05-14 diagnosticou
     * stack canary trip em wg_keepalive — a task corre sincronamente
     * wg_apply_locked() → s_wg.begin() (WG netif bring-up profundo) →
     * app_log_feature_writef (~984 B buffers char empilhados: msg[256] +
     * line[320] + line_notify[384] + ts[24] + strftime). Com ISR nesting
     * 3072 B estoura. Esta era a causa raiz dos reboots v1.74/v1.76 (e
     * v1.73 so' "estavel" porque task zombie nunca atingia o deep path). */
    BaseType_t ok = xTaskCreatePinnedToCore(wg_keepalive_task, "wg_keepalive",
                                            8192, nullptr, 1, &s_keepalive_task, 1);
    if (ok != pdPASS) {
      s_keepalive_task = nullptr;
      app_log_feature_write("ERROR", "WIREGUARD",
                            "Falha a criar task wg_keepalive.");
    }
    /* v1.89: NAO registar WG no service_supervisor. WG ja tem keepalive_tick
     * proprio (wg_keepalive_task com re-apply blind 90s, v1.76). Supervisor
     * em cima duplica recovery e cria race quando supervisor restart_cb roda
     * mesmo esp_wifi_stop/start que keepalive_tick HARD path → PANIC observado
     * v1.88. wg_supervisor_restart fica como dead code (pode ser util futuro
     * se removermos keepalive_tick). */
  }
}

void net_wireguard_init(void) {
  s_wg.end();
  if (s_wg_active) {
    s_wg_active = false;
    app_log_feature_write("INFO", "WIREGUARD", "Tunel parado.");
  }
  wg_keepalive_start_once();
}

/* API pause/resume mantida como no-op para compatibilidade com wg_provision.cpp
 * (chama estas funcoes para libertar heap pre-xTaskCreate). Sem watchdog activo,
 * nao ha nada a pausar — heap interno fica disponivel naturalmente. */
void net_wireguard_pause_watchdog(void) { /* no-op */ }
void net_wireguard_resume_watchdog(void) { /* no-op */ }

size_t net_wireguard_status_json(char *out, size_t out_sz) {
  if (out == nullptr || out_sz < 64U) return 0;
  const uint32_t now = millis();
  const bool peer_up = s_wg_active && s_wg.is_peer_up();
  const uint32_t last_up_ago = (s_last_up_ms == 0U) ? 0U : (now - s_last_up_ms);
  const uint32_t last_rx_ago = (s_last_rx_ms == 0U) ? 0U : (now - s_last_rx_ms);
  const uint32_t last_apply_ago = (s_last_apply_ms == 0U) ? 0U : (now - s_last_apply_ms);
  const int n = snprintf(out, out_sz,
      "{\"active\":%s,\"peer_up\":%s,\"ever_up\":%s,\"ever_rx\":%s,"
      "\"last_up_ms_ago\":%u,\"last_rx_ms_ago\":%u,\"last_apply_ms_ago\":%u,"
      "\"reapply_count\":%u,\"wifi_soft_count\":%u,\"wifi_hard_count\":%u,"
      "\"uptime_ms\":%u}",
      s_wg_active ? "true" : "false",
      peer_up ? "true" : "false",
      s_ever_up ? "true" : "false",
      s_ever_rx ? "true" : "false",
      (unsigned)last_up_ago, (unsigned)last_rx_ago, (unsigned)last_apply_ago,
      (unsigned)s_reapply_count,
      (unsigned)s_wifi_soft_count, (unsigned)s_wifi_hard_count,
      (unsigned)now);
  if (n <= 0 || (size_t)n >= out_sz) return 0;
  return (size_t)n;
}

void net_wireguard_apply(void) {
  if (s_apply_mtx != nullptr) {
    xSemaphoreTake(s_apply_mtx, portMAX_DELAY);
  }
  wg_apply_locked();
  if (s_apply_mtx != nullptr) {
    xSemaphoreGive(s_apply_mtx);
  }
}

static void wg_apply_locked(void) {
  panic_breadcrumb_set("wg_apply:start");
  s_last_apply_ms = millis(); /* v1.73: timer preemptive re-apply, sempre actualizado */
  panic_breadcrumb_set("wg_apply:end");
  s_wg.end();
  s_wg_active = false;
  /* v1.75: REVERTIDO v1.74. Manter resets s_ever_up + s_last_up_ms.
   * v1.74 tentou eliminar resets para permitir reactive recovery quando
   * preemptive nao traz peer up. Resultado: reboot loop ~3min (boot_count
   * +3 em 15min). Causa raiz nao isolada — possivel rapid reactive fire
   * sobrecarregar SD/lib. Preferir stable + zombie ao crashing.
   * Investigacao deferida. */
  s_ever_up = false;
  s_last_up_ms = millis();
  s_ever_rx = false;
  s_last_rx_ms = millis();
  if (!app_settings_wireguard_enabled()) {
    app_log_feature_write("INFO", "WIREGUARD", "Feature desativada nas configuracoes.");
    panic_breadcrumb_clear();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    app_log_feature_write("WARN", "WIREGUARD", "Inicio adiado: Wi-Fi desconectado.");
    panic_breadcrumb_clear();
    return;
  }
  String ip = app_settings_wg_local_ip();
  IPAddress local;
  if (!local.fromString(ip.c_str())) {
    Serial.println("[WG] IP local invalido");
    app_log_feature_writef("ERROR", "WIREGUARD", "IP local invalido: '%s'", ip.c_str());
    panic_breadcrumb_clear();
    return;
  }
  String priv = app_settings_wg_private_key();
  String pub = app_settings_wg_peer_public_key();
  String ep = app_settings_wg_endpoint();
  if (priv.length() == 0 || pub.length() == 0 || ep.length() == 0) {
    Serial.println("[WG] Chaves ou endpoint em falta");
    app_log_feature_write("ERROR", "WIREGUARD", "Chaves/endpoint em falta.");
    panic_breadcrumb_clear();
    return;
  }
  strncpy(s_wg_priv, priv.c_str(), sizeof(s_wg_priv) - 1U);
  s_wg_priv[sizeof(s_wg_priv) - 1U] = '\0';
  strncpy(s_wg_pub, pub.c_str(), sizeof(s_wg_pub) - 1U);
  s_wg_pub[sizeof(s_wg_pub) - 1U] = '\0';

  /* Endpoint TEM de ser IP literal. Nao usar WiFi.hostByName aqui — a chamada
   * (ou a propria biblioteca WireGuard fazendo DNS interno) tem corrompido a
   * stack e causado Double exception ~14s pos-boot quando a resolucao falha
   * (testado em v1.18/v1.19 — boot loop reproducivel). User configura IP via UI. */
  IPAddress ep_resolved;
  if (!ep_resolved.fromString(ep.c_str())) {
    Serial.println("[WG] Endpoint nao e IP literal — DNS desactivado p/ evitar crash.");
    app_log_feature_writef("ERROR", "WIREGUARD",
                           "Endpoint '%s' nao e IP literal. Configure IPv4 explicito.", ep.c_str());
    panic_breadcrumb_clear();
    return;
  }
  strncpy(s_wg_ep, ep.c_str(), sizeof(s_wg_ep) - 1U);
  s_wg_ep[sizeof(s_wg_ep) - 1U] = '\0';

  /* Endpoint resolvido a 0.0.0.0 = provision incompleta. begin() crasha
   * (Double exception apos add_peer) se lhe passarmos endereco invalido.
   * Bail antes para evitar boot loop. */
  if (ep_resolved == IPAddress(0, 0, 0, 0)) {
    Serial.println("[WG] Endpoint resolveu a 0.0.0.0 — provision incompleta. Inicio adiado.");
    app_log_feature_writef("ERROR", "WIREGUARD",
                           "Endpoint '%s' resolveu a 0.0.0.0 (provision incompleta).", ep.c_str());
    panic_breadcrumb_clear();
    return;
  }

  const uint16_t port = app_settings_wg_port();
  /**
   * v1.92 FIX outbound TX morto: wg_mask 255.255.255.255 → 255.255.255.0.
   * Mask /32 limitava netif WG ao proprio host (10.0.0.2 only). lwip routing
   * para destinos 10.0.0.x (ex: 10.0.0.3 PC, 10.0.0.1 server) nao match wg
   * netif subnet → falha p/ WiFi netif default → drop. Inbound funcionava
   * porque wireguardif decapsula antes da decisao de routing (recebe UDP
   * raw, despoja header WG, faz ip_input direto). Outbound depende de lwip
   * escolher netif certa: precisa subnet WG cobrir destino.
   *
   * /24 via netif (wg_mask 255.255.255.0) + /24 via wireguardif filter
   * (allowed_mask 255.255.255.0) = stack consistente. Sessao 2026-05-17.
   *
   * allowed_ip + allowed_mask define que peer WG aceita/envia (filter interno
   * wireguardif). Derivamos /24 a partir do local IP (ex: 10.0.0.2 → 10.0.0.0/24).
   *
   * localPort=0 (ephemeral) — lib bind a porta aleatoria. localPort=51820 fixo
   * foi tentado (v1.64) sem beneficio observavel; voltou a 0 (default lib) para
   * reduzir risco de conflito futuro.
   */
  const IPAddress wg_mask(255, 255, 255, 0);
  const IPAddress wg_gw(0, 0, 0, 0);
  const IPAddress allowed_ip(local[0], local[1], local[2], 0);
  const IPAddress allowed_mask(255, 255, 255, 0);
  /* in_filter_fn = wg_in_filter para detecao real de traffic vivo (v1.71). */
  panic_breadcrumb_set("wg_apply:begin");
  if (!s_wg.begin(local, wg_mask, 0, wg_gw, s_wg_priv, s_wg_ep, s_wg_pub, port,
                  allowed_ip, allowed_mask, false, nullptr, &wg_in_filter)) {
    Serial.println("[WG] begin() falhou (ver hora NTP e chaves)");
    app_log_feature_write("ERROR", "WIREGUARD", "Falha ao iniciar tunel (begin).");
    panic_breadcrumb_clear();
    return;
  }
  s_wg_active = true;
  Serial.println("[WG] tunel ativo");
  app_log_feature_writef("INFO", "WIREGUARD",
                         "Tunel ativo endpoint=%s port=%u allowed=%u.%u.%u.0/24",
                         s_wg_ep, (unsigned)port,
                         (unsigned)allowed_ip[0], (unsigned)allowed_ip[1],
                         (unsigned)allowed_ip[2]);
  panic_breadcrumb_clear();
}

#endif  /* FITA_ENABLE_WG */
