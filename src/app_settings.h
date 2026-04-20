/**
 * @file app_settings.h
 * @brief Preferencias (NVS) — Wi-Fi, FTP, NTP/fuso, WireGuard, UI e RS485.
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

/**
 * IP/hostname alvo para monitorizacao de conectividade (ping ICMP).
 * String vazia = monitorizacao desligada (icone da status bar oculto).
 * Max. 63 caracteres para suportar IPv4 ou hostname curto.
 */
String app_settings_monitor_ip(void);
void app_settings_set_monitor_ip(const char *host);

/**
 * URL base para descargas remotas de ciclos (sem `?path=`).
 * Vazia = usa o proprio portal do dispositivo
 * (`http://<IP>/api/fs/file`). Max. 127 caracteres.
 */
String app_settings_download_url(void);
void app_settings_set_download_url(const char *url);

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

/** Duração do splash de boot em segundos (0 = desativado, máximo 10). Default: 3. */
uint8_t app_settings_splash_seconds(void);
void app_settings_set_splash_seconds(uint8_t secs);

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
 * Serial1 (RS485): velocidade em baud (valor da lista padrao ou o ultimo guardado).
 * @see app_settings_rs485_std_baud_count
 */
uint32_t app_settings_rs485_baud(void);
/**
 * Perfil de trama UART (data / paridade / stop), 0..7:
 * 0=8N1, 1=8E1, 2=8O1, 3=8N2, 4=7N1, 5=7E1, 6=7O1, 7=7N2 (convencao Arduino `SERIAL_*`).
 */
uint8_t app_settings_rs485_frame_profile(void);
void app_settings_set_rs485(uint32_t baud, uint8_t frame_profile);
/** Quantidade de bauds pre-definidos (mesma ordem que o roller na UI). */
size_t app_settings_rs485_std_baud_count(void);
uint32_t app_settings_rs485_std_baud(size_t index);
/** Indice do baud mais proximo na lista (para posicionar o roller). */
size_t app_settings_rs485_std_baud_nearest_index(uint32_t baud);

