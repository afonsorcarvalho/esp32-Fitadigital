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

/* Re-apply keepalive (v1.69→v1.74) — biblioteca cliente entra em rekey storm
 * pos-180s: envia INITIATIONs continuos com state corrompido, server rejeita
 * todas como "Invalid handshake initiation" (MAC1/static-decrypt fail).
 *
 * v1.69: detecao via is_peer_up() — reativa apos peer down 100s.
 * v1.71: detecao via in_filter_fn — FALHA (WG keepalive bypass filter).
 * v1.72: escalation esp_restart() apos 2 fails consec.
 * v1.73: REMOVIDA escalation esp_restart() (constraint: dispositivo registrador
 *        ciclos esterilizacao RS485, reboot = perda mensagem catastrofica).
 *        Adicionado PREEMPTIVE re-apply a cada 100s — bypass lib broken rekey
 *        (so' dispara a t=180s). end()+begin() reseta state lib fresh.
 * v1.74: TENTATIVA fix deadlock task removendo resets em wg_apply_locked.
 *        FALHOU produca:o: reboot loop ~3min (boot_count +3 em 15min).
 *        Causa raiz nao isolada — provavelmente rapid reactive fire
 *        sobrecarrega SD ou lib. Revertido em v1.75.
 * v1.75: REVERT v1.74 → comportamento v1.73 (zombie aceitavel vs crashing).
 *        Investigacao proper diferida proxima sessao. RS485 captura
 *        intacta, WG zombie pos-#1 e' tradeoff aceito.
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
static volatile uint32_t s_preemptive_count = 0;
static volatile uint32_t s_consec_fail = 0;
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

static void wg_keepalive_task(void *arg) {
  const TickType_t tick = pdMS_TO_TICKS(20000U);
  const uint32_t down_threshold_ms = 100000U;
  const uint32_t preemptive_reapply_ms = 100000U; /* re-apply antes lib broken rekey (t>=180s) */
  const uint32_t observe_post_apply_ms = 30000U;  /* min gap entre re-applies p/ evitar storm */
  for (;;) {
    vTaskDelay(tick);
    if (!s_wg_active) continue;
    if (WiFi.status() != WL_CONNECTED) continue;
    uint32_t now = millis();
    uint32_t since_apply = now - s_last_apply_ms;
    bool peer_up = s_wg.is_peer_up();
    if (peer_up) {
      s_last_up_ms = now;
      if (!s_ever_up) {
        s_ever_up = true;
        Serial.println("[WG-KA] peer up (first handshake)");
        app_log_feature_write("INFO", "WIREGUARD", "Peer up (first handshake).");
      }
      if (s_consec_fail > 0U) {
        Serial.printf("[WG-KA] recovery OK apos %u fails\n", (unsigned)s_consec_fail);
        app_log_feature_writef("INFO", "WIREGUARD",
                               "Recovery OK apos %u re-apply consecutivos.",
                               (unsigned)s_consec_fail);
        s_consec_fail = 0;
      }
      /* Preemptive re-apply: peer up mas state lib aproxima janela rekey broken.
       * Forca end()+begin() ANTES dos 180s para evitar storm INITs invalidos. */
      if (since_apply >= preemptive_reapply_ms) {
        s_preemptive_count++;
        Serial.printf("[WG-KA] preemptive re-apply (apos %us) #%u\n",
                      (unsigned)(since_apply / 1000U), (unsigned)s_preemptive_count);
        app_log_feature_writef("INFO", "WIREGUARD",
                               "Re-apply preemptivo apos %us #%u.",
                               (unsigned)(since_apply / 1000U), (unsigned)s_preemptive_count);
        if (s_apply_mtx != nullptr) {
          xSemaphoreTake(s_apply_mtx, portMAX_DELAY);
        }
        wg_apply_locked();
        if (s_apply_mtx != nullptr) {
          xSemaphoreGive(s_apply_mtx);
        }
      }
      continue;
    }
    /* peer NOT up */
    if (!s_ever_up) continue;
    uint32_t age = now - s_last_up_ms;
    if (age < down_threshold_ms) continue;
    if (since_apply < observe_post_apply_ms) continue; /* aguarda observe window apos re-apply */
    /* Re-apply nao recuperou em tempo razoavel — telemetria + retry (NUNCA reboot). */
    if (since_apply >= preemptive_reapply_ms + 30000U) {
      s_consec_fail++;
      Serial.printf("[WG-KA] re-apply nao recuperou (%us pos-apply, %u consec fails) — retry\n",
                    (unsigned)(since_apply / 1000U), (unsigned)s_consec_fail);
      app_log_feature_writef("ERROR", "WIREGUARD",
                             "Re-apply nao recuperou (%us pos-apply, %u consec fails).",
                             (unsigned)(since_apply / 1000U), (unsigned)s_consec_fail);
    }
    s_reapply_count++;
    Serial.printf("[WG-KA] peer down %us — re-apply reativo #%u\n",
                  (unsigned)(age / 1000U), (unsigned)s_reapply_count);
    app_log_feature_writef("WARN", "WIREGUARD",
                           "Peer down ha %us — re-apply reativo #%u.",
                           (unsigned)(age / 1000U), (unsigned)s_reapply_count);
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
    BaseType_t ok = xTaskCreatePinnedToCore(wg_keepalive_task, "wg_keepalive",
                                            3072, nullptr, 1, &s_keepalive_task, 1);
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
