/**
 * @file net_time.cpp
 * @brief Implementacao NTP e hora (ESP32 Arduino).
 */
#include "net_time.h"
#include "app_log.h"
#include "app_settings.h"
#include "fs/fs_time_fix.h"
#include "sd_access.h"
#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include "driver/i2c.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static uint32_t s_last_resync_ms = 0;
static bool s_have_resync_anchor = false;
static bool s_sd_time_fix_done = false;
static bool s_rtc_warned_once = false;
static uint8_t s_rtc_sd_sync_attempts = 0;
static bool s_rtc_sd_sync_blocked = false;

/** Limita tentativas RTC->SD para evitar saturar SD/CH422G quando RTC falha. */
static constexpr uint8_t kRtcSdSyncMaxAttempts = 3U;

/**
 * RTC externo da placa (Waveshare ESP32-S3-Touch-LCD-4.3B):
 * - CI: PCF85063A / PCF85063ATL
 * - Barramento: I2C em GPIO8/GPIO9
 * - Endereco: 0x51
 *
 * Referencia: NXP PCF85063A datasheet (registradores 0x04..0x0A em BCD).
 */
static constexpr uint8_t kRtcAddr = 0x51U;
static constexpr uint8_t kRtcRegSeconds = 0x04U;
static constexpr uint8_t kRtcFrameLen = 7U;
static constexpr i2c_port_t kRtcI2cPort = I2C_NUM_0;
static constexpr TickType_t kRtcI2cTimeoutTicks = pdMS_TO_TICKS(50);

static bool rtc_apply_to_system_time(bool apply_local_tz);
static bool rtc_get_epoch_utc_internal(time_t *out_ts);

static uint8_t bcd_to_bin(uint8_t v) {
  return (uint8_t)(((v >> 4) * 10U) + (v & 0x0FU));
}

static uint8_t bin_to_bcd(uint8_t v) {
  return (uint8_t)(((v / 10U) << 4) | (v % 10U));
}

static bool rtc_bus_available(void) {
  /**
   * O barramento I2C da placa e' inicializado pelo ESP_Panel.
   * Aqui apenas verificamos se o driver ja esta ativo para evitar instalar
   * driver duplicado (causa "i2c driver install error" no boot).
   */
  int timeout = 0;
  const esp_err_t err = i2c_get_timeout(kRtcI2cPort, &timeout);
  if (err == ESP_OK) {
    return true;
  }
  if (!s_rtc_warned_once) {
    s_rtc_warned_once = true;
    app_log_feature_write("WARN", "RTC", "Barramento I2C do RTC ainda indisponivel (aguardando ESP_Panel).");
  }
  return false;
}

static bool rtc_read_frame(uint8_t *buf, size_t len) {
  if (buf == nullptr || len < kRtcFrameLen || !rtc_bus_available()) {
    return false;
  }
  const uint8_t reg = kRtcRegSeconds;
  const esp_err_t err = i2c_master_write_read_device(kRtcI2cPort, kRtcAddr, &reg, 1, buf, kRtcFrameLen,
                                                      kRtcI2cTimeoutTicks);
  if (err != ESP_OK) {
    return false;
  }
  return true;
}

static bool rtc_write_frame(const uint8_t *buf, size_t len) {
  if (buf == nullptr || len < kRtcFrameLen || !rtc_bus_available()) {
    return false;
  }
  uint8_t tx[1 + kRtcFrameLen];
  tx[0] = kRtcRegSeconds;
  for (uint8_t i = 0; i < kRtcFrameLen; ++i) {
    tx[1 + i] = buf[i];
  }
  return i2c_master_write_to_device(kRtcI2cPort, kRtcAddr, tx, sizeof(tx), kRtcI2cTimeoutTicks) == ESP_OK;
}

static bool rtc_read_tm_utc(struct tm *out_tm) {
  if (out_tm == nullptr) {
    return false;
  }
  uint8_t r[kRtcFrameLen];
  if (!rtc_read_frame(r, sizeof(r))) {
    return false;
  }
  /** Bit OS (Seconds[7]) = 1 indica oscilador parado/hora invalida no RTC. */
  if ((r[0] & 0x80U) != 0U) {
    return false;
  }
  const int sec = (int)bcd_to_bin((uint8_t)(r[0] & 0x7FU));
  const int min = (int)bcd_to_bin((uint8_t)(r[1] & 0x7FU));
  const int hour = (int)bcd_to_bin((uint8_t)(r[2] & 0x3FU));
  const int mday = (int)bcd_to_bin((uint8_t)(r[3] & 0x3FU));
  const int wday = (int)bcd_to_bin((uint8_t)(r[4] & 0x07U));
  const int mon = (int)bcd_to_bin((uint8_t)(r[5] & 0x1FU));
  const int year = (int)bcd_to_bin(r[6]);
  if (sec > 59 || min > 59 || hour > 23 || mday < 1 || mday > 31 || mon < 1 || mon > 12 || year > 99) {
    return false;
  }
  struct tm t = {};
  t.tm_sec = sec;
  t.tm_min = min;
  t.tm_hour = hour;
  t.tm_mday = mday;
  t.tm_wday = wday;
  t.tm_mon = mon - 1;
  t.tm_year = (2000 + year) - 1900;
  *out_tm = t;
  return true;
}

static bool tm_to_epoch_utc(const struct tm *utc_tm, time_t *out_ts) {
  if (utc_tm == nullptr || out_ts == nullptr) {
    return false;
  }
  char old_tz[64];
  bool had_old_tz = false;
  const char *tz = getenv("TZ");
  if (tz != nullptr) {
    strncpy(old_tz, tz, sizeof(old_tz) - 1U);
    old_tz[sizeof(old_tz) - 1U] = '\0';
    had_old_tz = true;
  }
  setenv("TZ", "UTC0", 1);
  tzset();
  struct tm t = *utc_tm;
  const time_t ts = mktime(&t);
  if (had_old_tz) {
    setenv("TZ", old_tz, 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  if (ts == (time_t)-1) {
    return false;
  }
  *out_ts = ts;
  return true;
}

static bool rtc_write_from_epoch_utc(time_t ts) {
  if (ts <= 0) {
    return false;
  }
  struct tm tu;
  if (gmtime_r(&ts, &tu) == nullptr) {
    return false;
  }
  const int year = tu.tm_year + 1900;
  if (year < 2000 || year > 2099) {
    return false;
  }
  uint8_t wday = (tu.tm_wday >= 0 && tu.tm_wday <= 6) ? (uint8_t)tu.tm_wday : 0U;
  const uint8_t frame[kRtcFrameLen] = {
      bin_to_bcd((uint8_t)tu.tm_sec),
      bin_to_bcd((uint8_t)tu.tm_min),
      bin_to_bcd((uint8_t)tu.tm_hour),
      bin_to_bcd((uint8_t)tu.tm_mday),
      bin_to_bcd(wday),
      bin_to_bcd((uint8_t)(tu.tm_mon + 1)),
      bin_to_bcd((uint8_t)(year - 2000)),
  };
  return rtc_write_frame(frame, sizeof(frame));
}

static bool rtc_apply_to_system_time(bool apply_local_tz) {
  time_t ts = 0;
  if (!rtc_get_epoch_utc_internal(&ts)) {
    return false;
  }
  const struct timeval tv = {.tv_sec = ts, .tv_usec = 0};
  if (settimeofday(&tv, nullptr) != 0) {
    return false;
  }
  /** Em fase precoce de boot, app_settings ainda pode nao estar inicializado. */
  if (apply_local_tz) {
    net_time_apply_settings();
  }
  return true;
}

static bool rtc_get_epoch_utc_internal(time_t *out_ts) {
  if (out_ts == nullptr) {
    return false;
  }
  struct tm rtc_tm;
  if (!rtc_read_tm_utc(&rtc_tm)) {
    return false;
  }
  time_t ts = 0;
  if (!tm_to_epoch_utc(&rtc_tm, &ts)) {
    return false;
  }
  if (ts <= (time_t)1577836800) {
    return false;
  }
  *out_ts = ts;
  return true;
}

bool net_time_get_rtc_epoch_utc(time_t *out_ts) {
  return rtc_get_epoch_utc_internal(out_ts);
}

bool net_time_probe_rtc(bool *rtc_has_valid_time) {
  if (rtc_has_valid_time != nullptr) {
    *rtc_has_valid_time = false;
  }
  if (!rtc_bus_available()) {
    return false;
  }
  uint8_t frame[kRtcFrameLen];
  if (!rtc_read_frame(frame, sizeof(frame))) {
    return false;
  }
  if (rtc_has_valid_time != nullptr) {
    *rtc_has_valid_time = ((frame[0] & 0x80U) == 0U);
  }
  return true;
}

static void rtc_push_system_time_if_valid(const char *reason) {
  const time_t now = time(nullptr);
  if (now <= (time_t)1577836800) {
    return;
  }
  if (!rtc_write_from_epoch_utc(now)) {
    if (!s_rtc_warned_once) {
      s_rtc_warned_once = true;
      app_log_feature_write("WARN", "RTC", "RTC nao respondeu na escrita; mantendo apenas relogio do sistema.");
    }
    return;
  }
  s_rtc_warned_once = false;
  if (reason != nullptr) {
    app_log_feature_writef("INFO", "RTC", "RTC atualizado a partir da hora do sistema (%s).", reason);
  }
}

void net_time_push_system_time_to_rtc(const char *reason) {
  rtc_push_system_time_if_valid(reason);
}

bool net_time_is_valid(void) {
  return time(nullptr) > (time_t)1577836800; /* >= 2020-01-01 UTC */
}

void net_time_bootstrap_from_rtc_early(void) {
  if (net_time_is_valid()) {
    return;
  }
  (void)rtc_apply_to_system_time(false);
}

void net_time_init(void) {
  s_last_resync_ms = 0;
  s_have_resync_anchor = false;
  s_sd_time_fix_done = false;
  s_rtc_warned_once = false;
  s_rtc_sd_sync_attempts = 0;
  s_rtc_sd_sync_blocked = false;
  if (rtc_bus_available() && !net_time_is_valid()) {
    if (rtc_apply_to_system_time(true)) {
      app_log_feature_write("INFO", "RTC", "Hora do sistema carregada a partir do RTC.");
    }
  }
}

void net_time_apply_settings(void) {
  const int32_t off = app_settings_tz_offset_sec();
  const long gmt = (long)off;
  if (!app_settings_ntp_enabled()) {
    configTime(gmt, 0, nullptr, nullptr, nullptr);
    return;
  }
  String srv = app_settings_ntp_server();
  if (srv.length() == 0) {
    srv = String(app_settings_ntp_server_default());
  }
  const char *s2 = "pool.ntp.org";
  const char *s3 = "time.google.com";
  configTime(gmt, 0, srv.c_str(), s2, s3);
}

void net_time_on_wifi_connected(void) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  s_sd_time_fix_done = false;
  s_rtc_sd_sync_attempts = 0;
  s_rtc_sd_sync_blocked = false;
  net_time_apply_settings();
  s_last_resync_ms = millis();
  s_have_resync_anchor = true;
}

void net_time_loop(void) {
  if (WiFi.status() != WL_CONNECTED || !app_settings_ntp_enabled() || !s_have_resync_anchor) {
    return;
  }
  const uint32_t now = millis();
  if (now - s_last_resync_ms < 3600000U) {
    if (!s_sd_time_fix_done && !s_rtc_sd_sync_blocked && net_time_is_valid()) {
      sd_access_sync([&] {
        if (SD.cardType() == CARD_NONE) {
          return;
        }
        time_t rtc_ts = 0;
        if (rtc_get_epoch_utc_internal(&rtc_ts)) {
          const uint32_t touched = fs_time_fix_touch_all("/sd", rtc_ts);
          app_log_feature_writef("INFO", "RTC", "Datas do SD sincronizadas pelo RTC: %lu entradas.",
                                 (unsigned long)touched);
          s_sd_time_fix_done = true;
          s_rtc_sd_sync_attempts = 0;
        } else {
          if (s_rtc_sd_sync_attempts < 255U) {
            s_rtc_sd_sync_attempts++;
          }
          if (s_rtc_sd_sync_attempts >= kRtcSdSyncMaxAttempts) {
            s_rtc_sd_sync_blocked = true;
            app_log_feature_writef("ERROR", "RTC",
                                   "Sincronizacao RTC->SD desativada apos %u falhas consecutivas.",
                                   (unsigned)kRtcSdSyncMaxAttempts);
          } else {
            app_log_feature_writef("WARN", "RTC",
                                   "RTC indisponivel para sincronizar datas do SD (tentativa %u/%u).",
                                   (unsigned)s_rtc_sd_sync_attempts,
                                   (unsigned)kRtcSdSyncMaxAttempts);
          }
        }
      });
    }
    return;
  }
  s_last_resync_ms = now;
  net_time_apply_settings();
  rtc_push_system_time_if_valid("ntp-resync-1h");
}

bool net_time_sync_now_blocking(char *msg, size_t msg_len) {
  if (msg != nullptr && msg_len > 0) {
    msg[0] = '\0';
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (msg && msg_len) {
      snprintf(msg, msg_len, "Wi-Fi desligado.");
    }
    return false;
  }
  net_time_apply_settings();
  struct tm ti;
  if (!getLocalTime(&ti, 3000)) {
    if (msg && msg_len) {
      snprintf(msg, msg_len, "NTP sem resposta.");
    }
    return false;
  }
  if (msg && msg_len) {
    snprintf(msg, msg_len, "Sincronizado.");
  }
  rtc_push_system_time_if_valid("ntp-sync-now");
  return true;
}

static bool is_leap(int y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) {
    return 31;
  }
  if (m == 2 && is_leap(y)) {
    return 29;
  }
  return d[m - 1];
}

bool net_time_set_manual_utc(int year, int mon, int day, int hour, int min, char *err_msg, size_t err_len) {
  if (err_msg != nullptr && err_len > 0) {
    err_msg[0] = '\0';
  }
  if (year < 2000 || year > 2099 || mon < 1 || mon > 12 || hour < 0 || hour > 23 || min < 0 || min > 59) {
    if (err_msg && err_len) {
      snprintf(err_msg, err_len, "Valores fora do intervalo.");
    }
    return false;
  }
  const int dim = days_in_month(year, mon);
  if (day < 1 || day > dim) {
    if (err_msg && err_len) {
      snprintf(err_msg, err_len, "Dia invalido para o mes.");
    }
    return false;
  }
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_sec = 0;
  setenv("TZ", "UTC0", 1);
  tzset();
  const time_t ts = mktime(&t);
  if (ts == (time_t)-1) {
    if (err_msg && err_len) {
      snprintf(err_msg, err_len, "mktime falhou.");
    }
    return false;
  }
  struct timeval tv = {.tv_sec = ts, .tv_usec = 0};
  if (settimeofday(&tv, nullptr) != 0) {
    if (err_msg && err_len) {
      snprintf(err_msg, err_len, "settimeofday falhou.");
    }
    return false;
  }
  net_time_apply_settings();
  rtc_push_system_time_if_valid("ajuste-manual");
  return true;
}

void net_time_format_status_line(char *buf, size_t buf_len) {
  if (buf == nullptr || buf_len == 0) {
    return;
  }
  /**
   * getLocalTime(..., 0) falha com frequencia (NTP a sincronizar, reconfiguracao SNTP).
   * time()+localtime_r reflecte o relogio do sistema; guardamos a ultima linha valida para
   * evitar piscar "--:--" na barra.
   */
  static char s_last_line[32];
  static bool s_have_last = false;

  struct tm ti;
  time_t rtc_utc = 0;
  if (rtc_get_epoch_utc_internal(&rtc_utc)) {
    const time_t rtc_local = rtc_utc + (time_t)app_settings_tz_offset_sec();
    if (gmtime_r(&rtc_local, &ti) != nullptr) {
      /** Barra: dia/mes/ano + hora local derivada diretamente do RTC. */
      const size_t n = strftime(buf, buf_len, "%d/%m/%Y  %H:%M", &ti);
      if (n > 0U) {
        strncpy(s_last_line, buf, sizeof(s_last_line) - 1U);
        s_last_line[sizeof(s_last_line) - 1U] = '\0';
        s_have_last = true;
        return;
      }
    }
  }
  /** Fallback de resiliencia: manter ultima linha valida para evitar piscar "--:--". */
  if (s_have_last) {
    strncpy(buf, s_last_line, buf_len - 1U);
    buf[buf_len - 1U] = '\0';
    return;
  }
  /**
   * Ultimo fallback: caso o RTC ainda nao tenha hora valida, usa relogio local do sistema.
   * NTP continua atualizando apenas o relogio local.
   */
  const time_t raw = time(nullptr);
  const bool plausible = raw > (time_t)1577836800; /* >= 2020-01-01 UTC */
  if (plausible && localtime_r(&raw, &ti) != nullptr) {
    /** Barra: dia/mes/ano + hora (local). */
    const size_t n = strftime(buf, buf_len, "%d/%m/%Y  %H:%M", &ti);
    if (n > 0U) {
      strncpy(s_last_line, buf, sizeof(s_last_line) - 1U);
      s_last_line[sizeof(s_last_line) - 1U] = '\0';
      s_have_last = true;
      return;
    }
  }
  snprintf(buf, buf_len, "--/--/---- --:--");
}
