/**
 * @file net_wireguard.cpp
 * @brief WireGuard: buffers estaticos para chaves (nao usar String::c_str() em begin).
 */
#include "net_wireguard.h"
#include "app_log.h"
#include "app_settings.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WireGuard-ESP32.h>
#include <string.h>

static WireGuard s_wg;
static bool s_wg_active = false;

static char s_wg_priv[128];
static char s_wg_pub[128];
static char s_wg_ep[128];

void net_wireguard_init(void) {
  s_wg.end();
  if (s_wg_active) {
    s_wg_active = false;
    app_log_feature_write("INFO", "WIREGUARD", "Tunel parado.");
  }
}

void net_wireguard_apply(void) {
  s_wg.end();
  s_wg_active = false;
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

  /* Pre-resolver o endpoint antes de passar à biblioteca WireGuard:
   * begin() com hostname faz DNS internamente e crasha com double exception
   * quando a resolução falha (stack overflow no código da biblioteca).
   * Passamos sempre um IP literal para evitar esse caminho. */
  IPAddress ep_resolved;
  if (ep_resolved.fromString(ep.c_str())) {
    strncpy(s_wg_ep, ep.c_str(), sizeof(s_wg_ep) - 1U);
  } else {
    if (!WiFi.hostByName(ep.c_str(), ep_resolved)) {
      app_log_feature_writef("ERROR", "WIREGUARD",
                             "DNS falhou para endpoint '%s'. Inicio adiado.", ep.c_str());
      return;
    }
    const String resolved_str = ep_resolved.toString();
    strncpy(s_wg_ep, resolved_str.c_str(), sizeof(s_wg_ep) - 1U);
  }
  s_wg_ep[sizeof(s_wg_ep) - 1U] = '\0';

  const uint16_t port = app_settings_wg_port();
  /**
   * API Tinkerforge/WireGuard-ESP32 (diferente de ciniml): subnet /32, gateway 0,
   * localPort 0, trafego predefinido nao forçado para WG (FTP continua no Wi-Fi).
   */
  const IPAddress wg_mask(255, 255, 255, 255);
  const IPAddress wg_gw(0, 0, 0, 0);
  if (!s_wg.begin(local, wg_mask, 0, wg_gw, s_wg_priv, s_wg_ep, s_wg_pub, port, IPAddress(0, 0, 0, 0),
                  IPAddress(255, 255, 255, 255), false)) {
    Serial.println("[WG] begin() falhou (ver hora NTP e chaves)");
    app_log_feature_write("ERROR", "WIREGUARD", "Falha ao iniciar tunel (begin).");
    return;
  }
  s_wg_active = true;
  Serial.println("[WG] tunel ativo");
  app_log_feature_writef("INFO", "WIREGUARD", "Tunel ativo endpoint=%s port=%u",
                         s_wg_ep, (unsigned)port);
}
