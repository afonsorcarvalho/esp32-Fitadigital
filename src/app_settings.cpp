/**
 * @file app_settings.cpp
 * @brief Implementacao com Preferences (namespace "fdigi") e espelho em /sd/fdigi.cfg.
 */
#include "app_settings.h"
#include "app_settings_sd.h"
#include <Preferences.h>
#include <SD.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/**
 * Caminho para SD.open(): relativo ao volume ja' montado em /sd (app.cpp).
 * Nao' incluir o prefixo /sd — a biblioteca SD concatena e geraria /sd/sd/...
 */
const char *const kAppSettingsSdConfigPath = "/fdigi.cfg";

static Preferences s_prefs;
static constexpr char kNs[] = "fdigi";

/** Limite da biblioteca SimpleFTPServer (FTP_CRED_SIZE 16, strlen < 16). */
static constexpr size_t kFtpCredMaxLen = 15U;

const char *const kAppSettingsFtpDefaultUser = "esp32";
const char *const kAppSettingsFtpDefaultPass = "esp32";

static constexpr size_t kNtpSrvMax = 63U;
static constexpr size_t kWgIpMax = 19U;
static constexpr size_t kWgKeyMax = 127U;
static constexpr size_t kWgEpMax = 127U;
static constexpr uint8_t kVncScaleMin = 1U;
static constexpr uint8_t kVncScaleMax = 8U;
static constexpr uint8_t kVncScaleDef = 1U;
static constexpr uint8_t kVncQualityMin = 1U;
static constexpr uint8_t kVncQualityMax = 100U;
static constexpr uint8_t kVncQualityDef = 35U;
static constexpr uint16_t kVncIntervalMin = 80U;
static constexpr uint16_t kVncIntervalMax = 2000U;
static constexpr uint16_t kVncIntervalDef = 180U;

/** Tamanho maximo do ficheiro de configuracao no SD (evita RAM excessiva). */
static constexpr size_t kSdCfgFileMaxBytes = 8192U;

static void put_ftp_creds(const char *user, const char *pass);

static bool sd_card_available(void) { return SD.cardType() != CARD_NONE; }

static char *trim_line(char *s) {
  char *p = s;
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (p != s) {
    memmove(s, p, strlen(p) + 1);
  }
  size_t n = strlen(s);
  while (n > 0U && (s[n - 1U] == ' ' || s[n - 1U] == '\t' || s[n - 1U] == '\r')) {
    s[--n] = '\0';
  }
  return s;
}

static bool parse_bool_val(const char *v, bool *out) {
  if (!strcmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on")) {
    *out = true;
    return true;
  }
  if (!strcmp(v, "0") || !strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcasecmp(v, "off")) {
    *out = false;
    return true;
  }
  return false;
}

/** Secoes: 0 antes de qualquer [secao], 1 wifi, 2 ui, 3 ftp, 4 time, 5 wireguard, 6 vnc. */
typedef struct {
  bool have_fmt;
  int fmt;
  bool have_ssid;
  char ssid[33];
  bool have_pass;
  char pass[65];
  bool have_wifi_ok;
  bool wifi_ok;
  bool have_font;
  uint8_t font;
  bool have_ftp_u;
  char ftp_u[16];
  bool have_ftp_p;
  char ftp_p[16];
  bool have_ntp_on;
  bool ntp_on;
  bool have_ntp_srv;
  char ntp_srv[64];
  bool have_tz;
  int32_t tz;
  bool have_wg_on;
  bool wg_on;
  bool have_wg_ip;
  char wg_ip[20];
  bool have_wg_pk;
  char wg_pk[128];
  bool have_wg_pub;
  char wg_pub[128];
  bool have_wg_ep;
  char wg_ep[128];
  bool have_wg_pt;
  uint32_t wg_pt;
  bool have_vnc_scale;
  uint8_t vnc_scale;
  bool have_vnc_q;
  uint8_t vnc_q;
  bool have_vnc_ms;
  uint32_t vnc_ms;
} ParsedSdCfg;

static bool copy_key_str(char *dst, size_t cap, const char *val) {
  size_t n = strlen(val);
  if (n >= cap) {
    return false;
  }
  memcpy(dst, val, n + 1U);
  return true;
}

/**
 * Processa um par chave=valor conforme a seccao actual.
 * @return false se o ficheiro for invalido (abortar importacao).
 */
static bool cfg_parse_kv(int sec, const char *key, const char *val, ParsedSdCfg *c) {
  if (!strcmp(key, "format")) {
    if (sec != 0) {
      return false;
    }
    c->fmt = (int)strtol(val, nullptr, 10);
    c->have_fmt = true;
    return c->fmt == 1;
  }

  switch (sec) {
  case 1: /* [wifi] */
    if (!strcmp(key, "ssid")) {
      if (strlen(val) > 32U) {
        return false;
      }
      if (!copy_key_str(c->ssid, sizeof(c->ssid), val)) {
        return false;
      }
      c->have_ssid = true;
      return true;
    }
    if (!strcmp(key, "pass")) {
      if (strlen(val) > 63U) {
        return false;
      }
      if (!copy_key_str(c->pass, sizeof(c->pass), val)) {
        return false;
      }
      c->have_pass = true;
      return true;
    }
    if (!strcmp(key, "wifi_ok")) {
      if (!parse_bool_val(val, &c->wifi_ok)) {
        return false;
      }
      c->have_wifi_ok = true;
      return true;
    }
    return true;
  case 2: /* [ui] */
    if (!strcmp(key, "font_i")) {
      unsigned long u = strtoul(val, nullptr, 10);
      if (u > 3UL) {
        return false;
      }
      c->font = (uint8_t)u;
      c->have_font = true;
      return true;
    }
    return true;
  case 3: /* [ftp] */
    if (!strcmp(key, "ftp_u")) {
      if (strlen(val) > kFtpCredMaxLen) {
        return false;
      }
      if (!copy_key_str(c->ftp_u, sizeof(c->ftp_u), val)) {
        return false;
      }
      c->have_ftp_u = true;
      return true;
    }
    if (!strcmp(key, "ftp_p")) {
      if (strlen(val) > kFtpCredMaxLen) {
        return false;
      }
      if (!copy_key_str(c->ftp_p, sizeof(c->ftp_p), val)) {
        return false;
      }
      c->have_ftp_p = true;
      return true;
    }
    return true;
  case 4: /* [time] */
    if (!strcmp(key, "ntp_on")) {
      if (!parse_bool_val(val, &c->ntp_on)) {
        return false;
      }
      c->have_ntp_on = true;
      return true;
    }
    if (!strcmp(key, "ntp_srv")) {
      if (strlen(val) > kNtpSrvMax) {
        return false;
      }
      if (!copy_key_str(c->ntp_srv, sizeof(c->ntp_srv), val)) {
        return false;
      }
      c->have_ntp_srv = true;
      return true;
    }
    if (!strcmp(key, "tz_sec")) {
      long t = strtol(val, nullptr, 10);
      if (t < -50400L || t > 50400L) {
        return false;
      }
      c->tz = (int32_t)t;
      c->have_tz = true;
      return true;
    }
    return true;
  case 5: /* [wireguard] */
    if (!strcmp(key, "wg_on")) {
      if (!parse_bool_val(val, &c->wg_on)) {
        return false;
      }
      c->have_wg_on = true;
      return true;
    }
    if (!strcmp(key, "wg_ip")) {
      if (strlen(val) > kWgIpMax) {
        return false;
      }
      if (!copy_key_str(c->wg_ip, sizeof(c->wg_ip), val)) {
        return false;
      }
      c->have_wg_ip = true;
      return true;
    }
    if (!strcmp(key, "wg_pk")) {
      if (strlen(val) > kWgKeyMax) {
        return false;
      }
      if (!copy_key_str(c->wg_pk, sizeof(c->wg_pk), val)) {
        return false;
      }
      c->have_wg_pk = true;
      return true;
    }
    if (!strcmp(key, "wg_pub")) {
      if (strlen(val) > kWgKeyMax) {
        return false;
      }
      if (!copy_key_str(c->wg_pub, sizeof(c->wg_pub), val)) {
        return false;
      }
      c->have_wg_pub = true;
      return true;
    }
    if (!strcmp(key, "wg_ep")) {
      if (strlen(val) > kWgEpMax) {
        return false;
      }
      if (!copy_key_str(c->wg_ep, sizeof(c->wg_ep), val)) {
        return false;
      }
      c->have_wg_ep = true;
      return true;
    }
    if (!strcmp(key, "wg_pt")) {
      unsigned long p = strtoul(val, nullptr, 10);
      if (p == 0UL || p > 65535UL) {
        return false;
      }
      c->wg_pt = (uint32_t)p;
      c->have_wg_pt = true;
      return true;
    }
    return true;
  case 6: /* [vnc] */
    if (!strcmp(key, "scale")) {
      unsigned long s = strtoul(val, nullptr, 10);
      if (s < kVncScaleMin || s > kVncScaleMax) {
        return false;
      }
      c->vnc_scale = (uint8_t)s;
      c->have_vnc_scale = true;
      return true;
    }
    if (!strcmp(key, "jpeg_q")) {
      unsigned long q = strtoul(val, nullptr, 10);
      if (q < kVncQualityMin || q > kVncQualityMax) {
        return false;
      }
      c->vnc_q = (uint8_t)q;
      c->have_vnc_q = true;
      return true;
    }
    if (!strcmp(key, "interval_ms")) {
      unsigned long ms = strtoul(val, nullptr, 10);
      if (ms < kVncIntervalMin || ms > kVncIntervalMax) {
        return false;
      }
      c->vnc_ms = (uint32_t)ms;
      c->have_vnc_ms = true;
      return true;
    }
    return true;
  default:
    return true;
  }
}

static bool cfg_parse_section_line(const char *line, int *sec) {
  if (line[0] != '[') {
    return false;
  }
  const char *rb = strchr(line + 1, ']');
  if (!rb) {
    return false;
  }
  size_t n = (size_t)(rb - (line + 1));
  char name[20];
  if (n >= sizeof(name)) {
    return false;
  }
  memcpy(name, line + 1, n);
  name[n] = '\0';
  for (size_t i = 0; i < n; i++) {
    name[i] = (char)tolower((unsigned char)name[i]);
  }
  if (!strcmp(name, "wifi")) {
    *sec = 1;
    return true;
  }
  if (!strcmp(name, "ui")) {
    *sec = 2;
    return true;
  }
  if (!strcmp(name, "ftp")) {
    *sec = 3;
    return true;
  }
  if (!strcmp(name, "time")) {
    *sec = 4;
    return true;
  }
  if (!strcmp(name, "wireguard")) {
    *sec = 5;
    return true;
  }
  if (!strcmp(name, "vnc")) {
    *sec = 6;
    return true;
  }
  return false;
}

static void cfg_apply_parsed(const ParsedSdCfg *c) {
  if (c->have_ssid) {
    s_prefs.putString("ssid", c->ssid);
  }
  if (c->have_pass) {
    s_prefs.putString("pass", c->pass);
  }
  if (c->have_wifi_ok) {
    s_prefs.putBool("wifi_ok", c->wifi_ok);
  }
  if (c->have_font) {
    s_prefs.putUChar("font_i", c->font);
  }
  if (c->have_ftp_u || c->have_ftp_p) {
    String u = c->have_ftp_u ? String(c->ftp_u) : app_settings_ftp_user();
    String p = c->have_ftp_p ? String(c->ftp_p) : app_settings_ftp_pass();
    put_ftp_creds(u.c_str(), p.c_str());
  }
  if (c->have_ntp_on) {
    s_prefs.putBool("ntp_on", c->ntp_on);
  }
  if (c->have_ntp_srv) {
    s_prefs.putString("ntp_srv", c->ntp_srv);
  }
  if (c->have_tz) {
    s_prefs.putInt("tz_sec", (int)c->tz);
  }
  if (c->have_wg_on) {
    s_prefs.putBool("wg_on", c->wg_on);
  }
  if (c->have_wg_ip) {
    s_prefs.putString("wg_ip", c->wg_ip);
  }
  if (c->have_wg_pk) {
    s_prefs.putString("wg_pk", c->wg_pk);
  }
  if (c->have_wg_pub) {
    s_prefs.putString("wg_pub", c->wg_pub);
  }
  if (c->have_wg_ep) {
    s_prefs.putString("wg_ep", c->wg_ep);
  }
  if (c->have_wg_pt) {
    s_prefs.putUInt("wg_pt", c->wg_pt);
  }
  if (c->have_vnc_scale) {
    s_prefs.putUChar("vnc_sc", c->vnc_scale);
  }
  if (c->have_vnc_q) {
    s_prefs.putUChar("vnc_q", c->vnc_q);
  }
  if (c->have_vnc_ms) {
    s_prefs.putUInt("vnc_ms", c->vnc_ms);
  }
}

bool app_settings_try_load_from_sd_config(void) {
  if (!sd_card_available()) {
    return false;
  }
  File f = SD.open(kAppSettingsSdConfigPath, FILE_READ);
  if (!f) {
    return false;
  }
  size_t n = f.size();
  if (n == 0U || n > kSdCfgFileMaxBytes) {
    f.close();
    return false;
  }
  char *buf = (char *)malloc(n + 1U);
  if (!buf) {
    f.close();
    return false;
  }
  size_t nread = f.read((uint8_t *)buf, n);
  if (nread != n) {
    free(buf);
    f.close();
    return false;
  }
  buf[n] = '\0';
  f.close();

  ParsedSdCfg c;
  memset(&c, 0, sizeof(c));
  int sec = 0;

  for (char *p = buf; *p != '\0';) {
    char *eol = strchr(p, '\n');
    if (eol) {
      *eol = '\0';
    }
    trim_line(p);
    if (p[0] != '\0' && p[0] != '#') {
      if (cfg_parse_section_line(p, &sec)) {
        if (sec != 0 && !c.have_fmt) {
          free(buf);
          return false;
        }
        p = eol ? (eol + 1) : (p + strlen(p));
        continue;
      }
      char *eq = strchr(p, '=');
      if (!eq) {
        free(buf);
        return false;
      }
      *eq = '\0';
      const char *key = trim_line(p);
      const char *val = trim_line(eq + 1);
      if (!cfg_parse_kv(sec, key, val, &c)) {
        free(buf);
        return false;
      }
    }
    p = eol ? (eol + 1) : (p + strlen(p));
  }
  free(buf);

  if (!c.have_fmt || c.fmt != 1) {
    return false;
  }
  cfg_apply_parsed(&c);
  return true;
}

void app_settings_sync_config_file_to_sd(void) {
  if (!sd_card_available()) {
    return;
  }
  File f = SD.open(kAppSettingsSdConfigPath, FILE_WRITE);
  if (!f) {
    return;
  }
  f.print("# FitaDigital fdigi.cfg (formato=1). Espelho da NVS; linhas com # sao comentarios.\n");
  f.print("format=1\n\n");
  f.print("[wifi]\n");
  f.printf("wifi_ok=%d\n", app_settings_wifi_configured() ? 1 : 0);
  f.print("ssid=");
  f.print(app_settings_wifi_ssid().c_str());
  f.print("\npass=");
  f.print(app_settings_wifi_pass().c_str());
  f.print("\n\n[ui]\n");
  f.printf("font_i=%u\n\n", (unsigned)app_settings_font_index());
  f.print("[ftp]\nftp_u=");
  f.print(app_settings_ftp_user().c_str());
  f.print("\nftp_p=");
  f.print(app_settings_ftp_pass().c_str());
  f.print("\n\n[time]\n");
  f.printf("ntp_on=%d\n", app_settings_ntp_enabled() ? 1 : 0);
  f.print("ntp_srv=");
  f.print(app_settings_ntp_server().c_str());
  f.printf("\ntz_sec=%ld\n\n", (long)app_settings_tz_offset_sec());
  f.print("[wireguard]\n");
  f.printf("wg_on=%d\n", app_settings_wireguard_enabled() ? 1 : 0);
  f.print("wg_ip=");
  f.print(app_settings_wg_local_ip().c_str());
  f.print("\nwg_pk=");
  f.print(app_settings_wg_private_key().c_str());
  f.print("\nwg_pub=");
  f.print(app_settings_wg_peer_public_key().c_str());
  f.print("\nwg_ep=");
  f.print(app_settings_wg_endpoint().c_str());
  f.printf("\nwg_pt=%u\n\n", (unsigned)app_settings_wg_port());
  f.print("[vnc]\n");
  f.printf("scale=%u\n", (unsigned)app_settings_vnc_scale());
  f.printf("jpeg_q=%u\n", (unsigned)app_settings_vnc_jpeg_quality());
  f.printf("interval_ms=%u\n", (unsigned)app_settings_vnc_interval_ms());
  f.close();
}

const char *app_settings_ntp_server_default(void) {
  return "pool.ntp.org";
}

static void put_ftp_creds(const char *user, const char *pass) {
  char u[16] = {0};
  char p[16] = {0};
  if (user) {
    strncpy(u, user, kFtpCredMaxLen);
  }
  if (pass) {
    strncpy(p, pass, kFtpCredMaxLen);
  }
  s_prefs.putString("ftp_u", u);
  s_prefs.putString("ftp_p", p);
}

void app_settings_init(void) {
  s_prefs.begin(kNs, false);
}

bool app_settings_wifi_configured(void) {
  return s_prefs.getBool("wifi_ok", false) && s_prefs.getString("ssid", "").length() > 0;
}

String app_settings_wifi_ssid(void) {
  return s_prefs.getString("ssid", "");
}

String app_settings_wifi_pass(void) {
  return s_prefs.getString("pass", "");
}

void app_settings_set_wifi(const char *ssid, const char *pass) {
  s_prefs.putString("ssid", ssid ? ssid : "");
  s_prefs.putString("pass", pass ? pass : "");
  s_prefs.putBool("wifi_ok", true);
  app_settings_sync_config_file_to_sd();
}

uint8_t app_settings_font_index(void) {
  uint8_t v = s_prefs.getUChar("font_i", 0);
  if (v > 3) {
    v = 0;
  }
  return v;
}

void app_settings_set_font_index(uint8_t idx) {
  if (idx > 3) {
    idx = 3;
  }
  s_prefs.putUChar("font_i", idx);
  app_settings_sync_config_file_to_sd();
}

String app_settings_ftp_user(void) {
  String u = s_prefs.getString("ftp_u", "");
  return u.length() ? u : String(kAppSettingsFtpDefaultUser);
}

String app_settings_ftp_pass(void) {
  String p = s_prefs.getString("ftp_p", "");
  return p.length() ? p : String(kAppSettingsFtpDefaultPass);
}

void app_settings_set_ftp(const char *user, const char *pass) {
  put_ftp_creds(user, pass);
  app_settings_sync_config_file_to_sd();
}

bool app_settings_ntp_enabled(void) {
  return s_prefs.getBool("ntp_on", true);
}

void app_settings_set_ntp_enabled(bool on) {
  s_prefs.putBool("ntp_on", on);
  app_settings_sync_config_file_to_sd();
}

String app_settings_ntp_server(void) {
  String h = s_prefs.getString("ntp_srv", "");
  return h.length() ? h : String(app_settings_ntp_server_default());
}

void app_settings_set_ntp_server(const char *host) {
  char b[kNtpSrvMax + 1] = {0};
  if (host) {
    strncpy(b, host, kNtpSrvMax);
  }
  s_prefs.putString("ntp_srv", b);
  app_settings_sync_config_file_to_sd();
}

int32_t app_settings_tz_offset_sec(void) {
  int32_t v = s_prefs.getInt("tz_sec", 0);
  if (v < -50400 || v > 50400) {
    v = 0;
  }
  return v;
}

void app_settings_set_tz_offset_sec(int32_t sec) {
  if (sec < -50400) {
    sec = -50400;
  }
  if (sec > 50400) {
    sec = 50400;
  }
  s_prefs.putInt("tz_sec", (int)sec);
  app_settings_sync_config_file_to_sd();
}

bool app_settings_wireguard_enabled(void) {
  return s_prefs.getBool("wg_on", false);
}

void app_settings_set_wireguard_enabled(bool on) {
  s_prefs.putBool("wg_on", on);
  app_settings_sync_config_file_to_sd();
}

static void put_str_max(const char *key, const char *val, size_t max) {
  char b[128];
  if (max >= sizeof(b)) {
    max = sizeof(b) - 1;
  }
  memset(b, 0, sizeof(b));
  if (val) {
    strncpy(b, val, max);
  }
  s_prefs.putString(key, b);
}

String app_settings_wg_local_ip(void) {
  return s_prefs.getString("wg_ip", "10.0.0.2");
}

void app_settings_set_wg_local_ip(const char *ip) {
  put_str_max("wg_ip", ip, kWgIpMax);
  app_settings_sync_config_file_to_sd();
}

String app_settings_wg_private_key(void) {
  return s_prefs.getString("wg_pk", "");
}

void app_settings_set_wg_private_key(const char *key) {
  put_str_max("wg_pk", key, kWgKeyMax);
  app_settings_sync_config_file_to_sd();
}

String app_settings_wg_peer_public_key(void) {
  return s_prefs.getString("wg_pub", "");
}

void app_settings_set_wg_peer_public_key(const char *key) {
  put_str_max("wg_pub", key, kWgKeyMax);
  app_settings_sync_config_file_to_sd();
}

String app_settings_wg_endpoint(void) {
  return s_prefs.getString("wg_ep", "");
}

void app_settings_set_wg_endpoint(const char *host) {
  put_str_max("wg_ep", host, kWgEpMax);
  app_settings_sync_config_file_to_sd();
}

uint16_t app_settings_wg_port(void) {
  uint32_t p = s_prefs.getUInt("wg_pt", 51820);
  if (p == 0 || p > 65535) {
    p = 51820;
  }
  return (uint16_t)p;
}

void app_settings_set_wg_port(uint16_t port) {
  if (port == 0) {
    port = 51820;
  }
  s_prefs.putUInt("wg_pt", port);
  app_settings_sync_config_file_to_sd();
}

uint8_t app_settings_vnc_scale(void) {
  uint8_t s = s_prefs.getUChar("vnc_sc", kVncScaleDef);
  if (s < kVncScaleMin || s > kVncScaleMax) {
    s = kVncScaleDef;
  }
  return s;
}

void app_settings_set_vnc_scale(uint8_t scale) {
  if (scale < kVncScaleMin) {
    scale = kVncScaleMin;
  }
  if (scale > kVncScaleMax) {
    scale = kVncScaleMax;
  }
  s_prefs.putUChar("vnc_sc", scale);
  app_settings_sync_config_file_to_sd();
}

uint8_t app_settings_vnc_jpeg_quality(void) {
  uint8_t q = s_prefs.getUChar("vnc_q", kVncQualityDef);
  if (q < kVncQualityMin || q > kVncQualityMax) {
    q = kVncQualityDef;
  }
  return q;
}

void app_settings_set_vnc_jpeg_quality(uint8_t quality) {
  if (quality < kVncQualityMin) {
    quality = kVncQualityMin;
  }
  if (quality > kVncQualityMax) {
    quality = kVncQualityMax;
  }
  s_prefs.putUChar("vnc_q", quality);
  app_settings_sync_config_file_to_sd();
}

uint16_t app_settings_vnc_interval_ms(void) {
  uint32_t ms = s_prefs.getUInt("vnc_ms", kVncIntervalDef);
  if (ms < kVncIntervalMin || ms > kVncIntervalMax) {
    ms = kVncIntervalDef;
  }
  return (uint16_t)ms;
}

void app_settings_set_vnc_interval_ms(uint16_t interval_ms) {
  if (interval_ms < kVncIntervalMin) {
    interval_ms = kVncIntervalMin;
  }
  if (interval_ms > kVncIntervalMax) {
    interval_ms = kVncIntervalMax;
  }
  s_prefs.putUInt("vnc_ms", interval_ms);
  app_settings_sync_config_file_to_sd();
}
