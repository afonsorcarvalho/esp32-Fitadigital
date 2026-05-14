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
#include "app_log.h"
#include "app_settings.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WireGuard-ESP32.h>
#include <esp_system.h>
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
    if (!s_wg_active) continue;
    if (WiFi.status() != WL_CONNECTED) continue;
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
  s_last_apply_ms = millis(); /* v1.73: timer preemptive re-apply, sempre actualizado */
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
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    app_log_feature_write("WARN", "WIREGUARD", "Inicio adiado: Wi-Fi desconectado.");
    return;
  }
  String ip = app_settings_wg_local_ip();
  IPAddress local;
  if (!local.fromString(ip.c_str())) {
    Serial.println("[WG] IP local invalido");
    app_log_feature_writef("ERROR", "WIREGUARD", "IP local invalido: '%s'", ip.c_str());
    return;
  }
  String priv = app_settings_wg_private_key();
  String pub = app_settings_wg_peer_public_key();
  String ep = app_settings_wg_endpoint();
  if (priv.length() == 0 || pub.length() == 0 || ep.length() == 0) {
    Serial.println("[WG] Chaves ou endpoint em falta");
    app_log_feature_write("ERROR", "WIREGUARD", "Chaves/endpoint em falta.");
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
    return;
  }

  const uint16_t port = app_settings_wg_port();
  /**
   * allowed_ip + allowed_mask define que subnet o lwip route table envia pelo peer
   * WG. Derivamos /24 a partir do local IP (ex: 10.0.0.2 → 10.0.0.0/24) para
   * cobrir o intervalo standard de servidores WG. Antes era 0.0.0.0/32 (nenhuma
   * rota) — bloqueava qualquer ping/HTTP para a rede WG interna.
   *
   * localPort=0 (ephemeral) — lib bind a porta aleatoria. localPort=51820 fixo
   * foi tentado (v1.64) sem beneficio observavel; voltou a 0 (default lib) para
   * reduzir risco de conflito futuro.
   */
  const IPAddress wg_mask(255, 255, 255, 255);
  const IPAddress wg_gw(0, 0, 0, 0);
  const IPAddress allowed_ip(local[0], local[1], local[2], 0);
  const IPAddress allowed_mask(255, 255, 255, 0);
  /* in_filter_fn = wg_in_filter para detecao real de traffic vivo (v1.71). */
  if (!s_wg.begin(local, wg_mask, 0, wg_gw, s_wg_priv, s_wg_ep, s_wg_pub, port,
                  allowed_ip, allowed_mask, false, nullptr, &wg_in_filter)) {
    Serial.println("[WG] begin() falhou (ver hora NTP e chaves)");
    app_log_feature_write("ERROR", "WIREGUARD", "Falha ao iniciar tunel (begin).");
    return;
  }
  s_wg_active = true;
  Serial.println("[WG] tunel ativo");
  app_log_feature_writef("INFO", "WIREGUARD",
                         "Tunel ativo endpoint=%s port=%u allowed=%u.%u.%u.0/24",
                         s_wg_ep, (unsigned)port,
                         (unsigned)allowed_ip[0], (unsigned)allowed_ip[1],
                         (unsigned)allowed_ip[2]);
}
