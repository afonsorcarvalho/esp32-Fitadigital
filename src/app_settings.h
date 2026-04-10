/**
 * @file app_settings.h
 * @brief Preferencias (NVS) — Wi-Fi, FTP, NTP/fuso, WireGuard, UI e parametros VNC/WebRemote.
 * @note A palavra-passe Wi-Fi e' guardada em texto (NVS); adequado apenas a redes locais.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

void app_settings_init(void);

bool app_settings_wifi_configured(void);
String app_settings_wifi_ssid(void);
String app_settings_wifi_pass(void);
/** Grava credenciais e marca rede como configurada. */
void app_settings_set_wifi(const char *ssid, const char *pass);

/** 0 = 14px, 1 = 16, 2 = 18, 3 = 20 (Montserrat em lv_conf.h). */
uint8_t app_settings_font_index(void);
void app_settings_set_font_index(uint8_t idx);

/** Valores usados quando a NVS nao tem ftp_u / ftp_p (SimpleFTPServer: max. 15 caracteres por campo). */
extern const char *const kAppSettingsFtpDefaultUser;
extern const char *const kAppSettingsFtpDefaultPass;

String app_settings_ftp_user(void);
String app_settings_ftp_pass(void);
/** Grava credenciais FTP (truncadas ao limite da biblioteca). */
void app_settings_set_ftp(const char *user, const char *pass);

/** NTP ativo por defeito; servidor por defeito "pool.ntp.org". */
bool app_settings_ntp_enabled(void);
void app_settings_set_ntp_enabled(bool on);
const char *app_settings_ntp_server_default(void);
String app_settings_ntp_server(void);
void app_settings_set_ntp_server(const char *host);

/** Offset em segundos face a UTC (ex.: 3600 para UTC+1). Intervalo tipico -43200..50400. */
int32_t app_settings_tz_offset_sec(void);
void app_settings_set_tz_offset_sec(int32_t sec);

/** WireGuard (biblioteca ciniml/WireGuard-ESP32): requer Wi-Fi e hora valida (NTP). */
bool app_settings_wireguard_enabled(void);
void app_settings_set_wireguard_enabled(bool on);
String app_settings_wg_local_ip(void);
void app_settings_set_wg_local_ip(const char *ip);
String app_settings_wg_private_key(void);
void app_settings_set_wg_private_key(const char *key);
String app_settings_wg_peer_public_key(void);
void app_settings_set_wg_peer_public_key(const char *key);
String app_settings_wg_endpoint(void);
void app_settings_set_wg_endpoint(const char *host);
uint16_t app_settings_wg_port(void);
void app_settings_set_wg_port(uint16_t port);

/**
 * Web Remote (VNC via browser): parametros de streaming JPEG.
 * scale: fator de downscale (1..8), quality: 1..100, interval_ms: 80..2000.
 */
uint8_t app_settings_vnc_scale(void);
void app_settings_set_vnc_scale(uint8_t scale);
uint8_t app_settings_vnc_jpeg_quality(void);
void app_settings_set_vnc_jpeg_quality(uint8_t quality);
uint16_t app_settings_vnc_interval_ms(void);
void app_settings_set_vnc_interval_ms(uint16_t interval_ms);
