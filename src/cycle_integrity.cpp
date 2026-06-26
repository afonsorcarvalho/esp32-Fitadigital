/**
 * @file cycle_integrity.cpp
 * @brief Implementacao de cycle_integrity.h — cadeia HMAC-SHA256/32 inline.
 */
#include "cycle_integrity.h"

#include "app_settings.h"
#include "app_log.h"

#include <Arduino.h>
#include <SD.h>
#include <mbedtls/md.h>
#include <string.h>
#include <stdio.h>

#if __has_include("integrity_secret.h")
#include "integrity_secret.h"
#endif

#ifndef FDIGI_INT_SECRET
#error "FDIGI_INT_SECRET nao definido. Copie src/integrity_secret.h.example -> src/integrity_secret.h e defina a password."
#endif

/** Bytes do mac truncado (4 = 32 bits, 8 chars hex). */
static constexpr size_t kMacBytes = 4U;
/** Versao do formato do cabecalho. */
static constexpr char kHeaderTag[] = "#FDIGI-INT v1 alg=HMAC-SHA256-32";

/* ------------------------------------------------------------------ */
/* Estado (acedido apenas no contexto sd_io — single-threaded).        */
/* ------------------------------------------------------------------ */
static const char  *s_key     = FDIGI_INT_SECRET;
static size_t       s_key_len = 0;
static bool         s_inited  = false;
static char         s_dev_id[16] = "";
static char         s_chain_path[48] = "";
static uint8_t      s_prev_mac[kMacBytes] = {0};
static uint32_t     s_seq = 0;

/* ------------------------------------------------------------------ */
/* Helpers de HMAC                                                      */
/* ------------------------------------------------------------------ */

/** HMAC-SHA256(key, msg) -> primeiros kMacBytes em out. */
static bool hmac_trunc(const uint8_t *msg, size_t msg_len, uint8_t out[kMacBytes]) {
  uint8_t full[32];
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) {
    return false;
  }
  if (mbedtls_md_hmac(info, reinterpret_cast<const uint8_t *>(s_key), s_key_len, msg, msg_len, full) != 0) {
    return false;
  }
  memcpy(out, full, kMacBytes);
  return true;
}

/** seed da cadeia = HMAC(key, path) truncado (mac da "linha -1"). */
static bool mac_seed(const char *path, uint8_t out[kMacBytes]) {
  return hmac_trunc(reinterpret_cast<const uint8_t *>(path), strlen(path), out);
}

/** mac_i = HMAC(key, prev || ':' || seq_ascii || ':' || raw) truncado. */
static bool mac_line(const uint8_t prev[kMacBytes], uint32_t seq, const char *raw, size_t raw_len,
                     uint8_t out[kMacBytes]) {
  /* prev(4) + ':' + seq(<=10) + ':' + raw(<=512) — folga confortavel. */
  uint8_t msg[kMacBytes + 1U + 10U + 1U + 520U];
  size_t n = 0;
  memcpy(msg, prev, kMacBytes);
  n += kMacBytes;
  msg[n++] = ':';
  n += static_cast<size_t>(snprintf(reinterpret_cast<char *>(msg + n), 12U, "%u", (unsigned)seq));
  msg[n++] = ':';
  if (raw_len > 512U) {
    raw_len = 512U; /* truncado defensivo; reader ja' limita a 512. */
  }
  memcpy(msg + n, raw, raw_len);
  n += raw_len;
  return hmac_trunc(msg, n, out);
}

static void hex8(const uint8_t mac[kMacBytes], char out[9]) {
  snprintf(out, 9U, "%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3]);
}

/* ------------------------------------------------------------------ */
/* Parse de linha assinada (recuperacao por tail)                       */
/* ------------------------------------------------------------------ */

/** Hex de 2 chars -> byte. Devolve -1 se invalido. */
static int hexbyte(char a, char b) {
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  const int hi = nib(a);
  const int lo = nib(b);
  if (hi < 0 || lo < 0) return -1;
  return (hi << 4) | lo;
}

/**
 * Confirma que `line` (sem '\n') esta' no nosso formato e extrai seq + mac.
 * Formato: "...\t<seq>\t<8hex>". Procura os 2 ultimos TABs.
 */
static bool parse_chain_line(const char *line, size_t len, uint32_t *seq_out, uint8_t mac_out[kMacBytes]) {
  if (len < 11U) { /* minimo: x \t 0 \t 8hex */
    return false;
  }
  /* ultimo TAB */
  int t2 = -1;
  for (int i = (int)len - 1; i >= 0; --i) {
    if (line[i] == '\t') { t2 = i; break; }
  }
  if (t2 < 0) return false;
  /* penultimo TAB */
  int t1 = -1;
  for (int i = t2 - 1; i >= 0; --i) {
    if (line[i] == '\t') { t1 = i; break; }
  }
  if (t1 < 0) return false;

  /* campo mac = (t2+1 .. len) tem de ter 8 hex */
  const size_t mac_len = len - (size_t)(t2 + 1);
  if (mac_len != kMacBytes * 2U) return false;
  uint8_t mac[kMacBytes];
  for (size_t k = 0; k < kMacBytes; ++k) {
    const int b = hexbyte(line[t2 + 1 + 2 * k], line[t2 + 2 + 2 * k]);
    if (b < 0) return false;
    mac[k] = (uint8_t)b;
  }
  /* campo seq = (t1+1 .. t2) tem de ser digitos */
  uint32_t seq = 0;
  bool any = false;
  for (int i = t1 + 1; i < t2; ++i) {
    if (line[i] < '0' || line[i] > '9') return false;
    seq = seq * 10U + (uint32_t)(line[i] - '0');
    any = true;
  }
  if (!any) return false;

  *seq_out = seq;
  memcpy(mac_out, mac, kMacBytes);
  return true;
}

/** Le a ultima linha completa de `path` e tenta recuperar seq+mac. */
static bool recover_from_tail(const char *path, uint32_t *seq_out, uint8_t mac_out[kMacBytes]) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  const size_t fsz = (size_t)f.size();
  if (fsz == 0U) { f.close(); return false; }

  char buf[1024];
  const size_t toread = (fsz < sizeof(buf)) ? fsz : sizeof(buf);
  if (!f.seek((uint32_t)(fsz - toread))) { f.close(); return false; }
  const int rd = f.read(reinterpret_cast<uint8_t *>(buf), toread);
  f.close();
  if (rd <= 0) return false;

  size_t blen = (size_t)rd;
  /* tira terminadores finais (linha completa termina em '\n'; pode haver '\r'). */
  while (blen > 0 && (buf[blen - 1] == '\n' || buf[blen - 1] == '\r')) {
    --blen;
  }
  if (blen == 0) return false;
  /* ultimo '\n' antes do conteudo => inicio da ultima linha completa. */
  int nl = -1;
  for (int i = (int)blen - 1; i >= 0; --i) {
    if (buf[i] == '\n') { nl = i; break; }
  }
  const char *line = buf + (nl + 1);
  const size_t llen = blen - (size_t)(nl + 1);
  return parse_chain_line(line, llen, seq_out, mac_out);
}

/* ------------------------------------------------------------------ */
/* API publica                                                          */
/* ------------------------------------------------------------------ */

void cycle_integrity_init(void) {
  s_key_len = strlen(s_key);
  uint64_t mac = ESP.getEfuseMac();
  snprintf(s_dev_id, sizeof(s_dev_id), "%012llX", (unsigned long long)mac);
  s_chain_path[0] = '\0';
  s_inited = true;
  app_log_writef("INFO", "cycle_integrity: pronto (dev=%s, mac=%uB, key=%uB).",
                 s_dev_id, (unsigned)kMacBytes, (unsigned)s_key_len);
}

/** Init preguicoso: rs485_buffer flush pode correr antes do init explicito do boot. */
static inline void ensure_init(void) {
  if (!s_inited) {
    cycle_integrity_init();
  }
}

bool cycle_integrity_enabled(void) {
  ensure_init();
  return app_settings_integrity_enabled();
}

size_t cycle_integrity_prepare(const char *path, char *hdr, size_t hdr_cap) {
  ensure_init();
  if (path == nullptr || hdr == nullptr || hdr_cap == 0U) {
    return 0;
  }
  if (strcmp(path, s_chain_path) == 0) {
    return 0; /* mesma cadeia ja' activa — sem cabecalho. */
  }

  /* Mudou de ficheiro (novo dia / arranque). Tenta recuperar do tail. */
  uint32_t rseq = 0;
  uint8_t rmac[kMacBytes];
  if (recover_from_tail(path, &rseq, rmac)) {
    memcpy(s_prev_mac, rmac, kMacBytes);
    s_seq = rseq + 1U;
    strncpy(s_chain_path, path, sizeof(s_chain_path) - 1U);
    s_chain_path[sizeof(s_chain_path) - 1U] = '\0';
    return 0; /* cadeia continua; sem cabecalho. */
  }

  /* Ficheiro novo ou legado sem cabecalho -> inicia cadeia + cabecalho (seq 0). */
  uint8_t seed[kMacBytes];
  if (!mac_seed(path, seed)) {
    return 0;
  }
  char hdr_content[80];
  const int hc = snprintf(hdr_content, sizeof(hdr_content), "%s dev=%s sep=TAB", kHeaderTag, s_dev_id);
  if (hc <= 0) {
    return 0;
  }
  uint8_t mac0[kMacBytes];
  if (!mac_line(seed, 0U, hdr_content, (size_t)hc, mac0)) {
    return 0;
  }
  char machex[9];
  hex8(mac0, machex);
  const int wn = snprintf(hdr, hdr_cap, "%s\t0\t%s", hdr_content, machex);
  if (wn <= 0 || (size_t)wn >= hdr_cap) {
    return 0;
  }
  memcpy(s_prev_mac, mac0, kMacBytes);
  s_seq = 1U;
  strncpy(s_chain_path, path, sizeof(s_chain_path) - 1U);
  s_chain_path[sizeof(s_chain_path) - 1U] = '\0';
  return (size_t)wn;
}

size_t cycle_integrity_compose(const char *raw, size_t raw_len, char *out, size_t out_cap) {
  if (raw == nullptr || out == nullptr || out_cap == 0U) {
    return 0;
  }
  uint8_t mac[kMacBytes];
  if (!mac_line(s_prev_mac, s_seq, raw, raw_len, mac)) {
    return 0;
  }
  char machex[9];
  hex8(mac, machex);
  const int wn = snprintf(out, out_cap, "%.*s\t%u\t%s", (int)raw_len, raw, (unsigned)s_seq, machex);
  if (wn <= 0 || (size_t)wn >= out_cap) {
    return 0;
  }
  memcpy(s_prev_mac, mac, kMacBytes);
  ++s_seq;
  return (size_t)wn;
}
