/**
 * @file cycles_rs485.cpp
 * @brief Implementacao de cycles_rs485.h — ver comentarios no header.
 */
#include "cycles_rs485.h"
#include "ui/ui_screensaver.h"

/** Sinaliza a UI para reabrir o .txt do dia (definido em file_browser.cpp). */
void file_browser_on_rs485_line_saved(void);

#include "app_settings.h"
#include "board_pins.h"
#include "app_log.h"
#include "sd_access.h"

#include <Arduino.h>
#include <SD.h>
#include <atomic>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <time.h>

/** Buffer maximo por linha (exclui terminador nulo). */
static constexpr size_t kLineCap = 512U;

/**
 * Debug: imprimir na Serial (USB/UART0) cada linha recebida na RS485.
 * Definir 0 antes de compilar para builds de producao sem eco na consola.
 */
#ifndef CYCLES_RS485_DEBUG_SERIAL_LINES
#define CYCLES_RS485_DEBUG_SERIAL_LINES 1
#endif

/**
 * Teste de TX: a cada 5 s envia pela Serial1 os bytes `ok` + LF + CR (como UART/RS232 sem protocolo).
 * Definir 1 em build_flags (-DCYCLES_RS485_TX_TEST_EVERY_5S=1) para ativar teste de TX.
 */
#ifndef CYCLES_RS485_TX_TEST_EVERY_5S
#define CYCLES_RS485_TX_TEST_EVERY_5S 0
#endif

/** Stack da tarefa de leitura UART (leitura + sd_access_sync por linha). */
static constexpr uint32_t kReaderStackWords = 4096U;
#if CYCLES_RS485_TX_TEST_EVERY_5S
static constexpr uint32_t kTxTestStackWords = 2048U;
#endif

static TaskHandle_t s_reader_task = nullptr;
#if CYCLES_RS485_TX_TEST_EVERY_5S
static TaskHandle_t s_tx_test_task = nullptr;
#endif

/** false no arranque: a UI nao abre o .txt do dia ate `cycles_rs485_set_line_to_ui_follow(true)`. */
static std::atomic<bool> s_line_to_ui_follow{false};

/** Contador de linhas do ficheiro do dia corrente (inicializado por scan na init). */
static std::atomic<uint32_t> s_today_lines{0};
/** time() da ultima gravacao (0 se ainda nao houve gravacao desde o boot). */
static std::atomic<time_t> s_last_write_time{0};
/** Caminho do ficheiro do dia para detetar mudanca de dia (reset do contador). */
static char s_today_path_cache[48] = "";

void cycles_rs485_set_line_to_ui_follow(bool enabled) {
  s_line_to_ui_follow.store(enabled, std::memory_order_relaxed);
}

/**
 * Converte o perfil guardado em NVS para o segundo argumento de `HardwareSerial::begin`
 * (macros `SERIAL_*` do core Arduino-ESP32).
 * @param profile 0..7 conforme `app_settings_rs485_frame_profile`.
 */
static uint32_t uart_config_for_profile(uint8_t profile) {
  switch (profile) {
    case 1:
      return SERIAL_8E1;
    case 2:
      return SERIAL_8O1;
    case 3:
      return SERIAL_8N2;
    case 4:
      return SERIAL_7N1;
    case 5:
      return SERIAL_7E1;
    case 6:
      return SERIAL_7O1;
    case 7:
      return SERIAL_7N2;
    case 0:
    default:
      return SERIAL_8N1;
  }
}

#if CYCLES_RS485_TX_TEST_EVERY_5S
/**
 * Tarefa de teste: saida simples na Serial1 (transceiver RS485 em modo transparente).
 * Payload: 'o','k',0x0A,0x0D — equivalente a texto "ok" com terminacao n/r.
 */
static void tx_test_task(void * /*arg*/) {
  static const char kPayload[] = "ok\n\r";
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    (void)Serial1.write(reinterpret_cast<const uint8_t *>(kPayload), sizeof(kPayload) - 1U);
    (void)Serial1.flush();
  }
}
#endif

static bool format_path_from_local_tm(const struct tm *lt, char *out, size_t cap) {
  if (lt == nullptr || out == nullptr || cap < 48U) {
    return false;
  }
  const int y = lt->tm_year + 1900;
  const int mon = lt->tm_mon + 1;
  const int day = lt->tm_mday;
  const int n = snprintf(out, cap, "/CICLOS/%04d/%02d/%04d%02d%02d.txt", y, mon, y, mon, day);
  return n > 0 && (size_t)n < cap;
}

bool cycles_rs485_format_today_path(char *out, size_t out_sz) {
  if (out == nullptr || out_sz == 0U) {
    return false;
  }
  const time_t now = time(nullptr);
  struct tm lt {};
  if (localtime_r(&now, &lt) == nullptr) {
    return false;
  }
  return format_path_from_local_tm(&lt, out, out_sz);
}

/**
 * Cria /CICLOS, /CICLOS/AAAA e /CICLOS/AAAA/MM se necessario (FatFs um nivel por mkdir).
 */
static void mkdirs_for_ym(int year, int month) {
  char p1[24];
  char p2[32];
  char p3[40];
  (void)snprintf(p1, sizeof p1, "/CICLOS");
  (void)snprintf(p2, sizeof p2, "/CICLOS/%04d", year);
  (void)snprintf(p3, sizeof p3, "/CICLOS/%04d/%02d", year, month);
  if (!SD.exists(p1)) {
    (void)SD.mkdir(p1);
  }
  if (!SD.exists(p2)) {
    (void)SD.mkdir(p2);
  }
  if (!SD.exists(p3)) {
    (void)SD.mkdir(p3);
  }
}

/** Conta LFs no ficheiro indicado (executar em contexto sd_io). Usado ao inicializar o contador. */
static uint32_t count_lfs_in_file_sync(const char *path) {
  if (path == nullptr || !SD.exists(path)) {
    return 0;
  }
  File f = SD.open(path, FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) {
      f.close();
    }
    return 0;
  }
  uint32_t count = 0;
  uint8_t buf[512];
  for (;;) {
    const int n = f.read(buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    for (int i = 0; i < n; ++i) {
      if (buf[i] == '\n') {
        count++;
      }
    }
  }
  f.close();
  return count;
}

/**
 * Grava uma linha no ficheiro do dia corrente (abre, escreve, fecha).
 * Executar apenas no contexto sd_io (via sd_access_sync).
 */
static void append_line_sync(const char *line, size_t line_len) {
  if (line == nullptr || line_len == 0U) {
    return;
  }
  const time_t now = time(nullptr);
  struct tm lt {};
  if (localtime_r(&now, &lt) == nullptr) {
    app_log_write("WARN", "RS485: localtime falhou; linha nao gravada.");
    return;
  }
  const int y = lt.tm_year + 1900;
  const int mon = lt.tm_mon + 1;
  mkdirs_for_ym(y, mon);
  char path[48];
  if (!format_path_from_local_tm(&lt, path, sizeof path)) {
    return;
  }
  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    app_log_writef("WARN", "RS485: falha ao abrir para append: %s", path);
    return;
  }
  (void)f.write(reinterpret_cast<const uint8_t *>(line), line_len);
  const uint8_t nl = static_cast<uint8_t>('\n');
  (void)f.write(&nl, 1U);
  f.close();
  sd_access_notify_changed();
  /* Mudanca de dia: reinicia contador; ficheiro novo acabou de ser criado com 1 linha. */
  if (strcmp(s_today_path_cache, path) != 0) {
    strncpy(s_today_path_cache, path, sizeof(s_today_path_cache) - 1U);
    s_today_path_cache[sizeof(s_today_path_cache) - 1U] = '\0';
    s_today_lines.store(0, std::memory_order_relaxed);
  }
  s_today_lines.fetch_add(1U, std::memory_order_relaxed);
  s_last_write_time.store(now, std::memory_order_relaxed);
  if (s_line_to_ui_follow.load(std::memory_order_relaxed)) {
    file_browser_on_rs485_line_saved();
  }
}

/** Termina linha em LF ou CR (UART tipo RS232); CRLF gera um commit no CR e ignora LF com buffer vazio. */
static void reader_commit_line(char *line, size_t *n_ptr) {
  size_t n = *n_ptr;
  line[n] = '\0';
  if (n > 0U) {
#if CYCLES_RS485_DEBUG_SERIAL_LINES
    Serial.printf("[RS485] %s\n", line);
#endif
    const size_t len = n;
    sd_access_sync([line, len]() { append_line_sync(line, len); });
  }
  *n_ptr = 0;
}

static void reader_task(void * /*arg*/) {
  char line[kLineCap + 1U];
  size_t n = 0;

  for (;;) {
    while (Serial1.available() > 0) {
      const int c = Serial1.read();
      if (c < 0) {
        break;
      }
      ui_screensaver_wake();
      if (c == '\r') {
        reader_commit_line(line, &n);
        continue;
      }
      if (c == '\n') {
        reader_commit_line(line, &n);
        continue;
      }
      if (n < kLineCap) {
        line[n++] = static_cast<char>(c);
      } else {
        /* Linha demasiado longa: descartar ate ao proximo LF. */
        n = 0;
        app_log_write("WARN", "RS485: linha excedeu buffer; descartada.");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void cycles_rs485_deinit(void) {
#if CYCLES_RS485_TX_TEST_EVERY_5S
  if (s_tx_test_task != nullptr) {
    vTaskDelete(s_tx_test_task);
    s_tx_test_task = nullptr;
  }
#endif
  if (s_reader_task != nullptr) {
    vTaskDelete(s_reader_task);
    s_reader_task = nullptr;
  }
  Serial1.end();
}

void cycles_rs485_apply_settings(void) {
  cycles_rs485_deinit();
  cycles_rs485_init();
}

void cycles_rs485_init(void) {
  if (s_reader_task != nullptr) {
    return;
  }
  if (!sd_access_is_mounted()) {
    return;
  }

  Serial1.end();
  /* RX grande para absorver stalls da task de captura enquanto a UI/SD trabalham. */
  Serial1.setRxBufferSize(16384);
  const uint32_t baud = app_settings_rs485_baud();
  const uint32_t cfg = uart_config_for_profile(app_settings_rs485_frame_profile());
  Serial1.begin(baud, cfg, BOARD_RS485_RX_GPIO, BOARD_RS485_TX_GPIO);

  /* Inicializa contador de linhas a partir do ficheiro do dia existente (se houver). */
  char today_path[48];
  if (cycles_rs485_format_today_path(today_path, sizeof(today_path))) {
    strncpy(s_today_path_cache, today_path, sizeof(s_today_path_cache) - 1U);
    s_today_path_cache[sizeof(s_today_path_cache) - 1U] = '\0';
    uint32_t initial = 0;
    sd_access_sync([&today_path, &initial]() { initial = count_lfs_in_file_sync(today_path); });
    s_today_lines.store(initial, std::memory_order_relaxed);
  }

  const BaseType_t ok = xTaskCreatePinnedToCore(reader_task, "rs485_rd", kReaderStackWords, nullptr, 3, &s_reader_task,
                                                  tskNO_AFFINITY);
  if (ok != pdPASS) {
    s_reader_task = nullptr;
    app_log_write("ERROR", "RS485: falha ao criar tarefa de leitura.");
    return;
  }
  app_log_writef("INFO", "RS485: Serial1 %lu baud RX=%d TX=%d (gravacao em /CICLOS/...).",
                 (unsigned long)baud, BOARD_RS485_RX_GPIO, BOARD_RS485_TX_GPIO);

#if CYCLES_RS485_TX_TEST_EVERY_5S
  if (s_tx_test_task == nullptr) {
    const BaseType_t tx_ok =
        xTaskCreatePinnedToCore(tx_test_task, "rs485_tx_t", kTxTestStackWords, nullptr, 3, &s_tx_test_task, tskNO_AFFINITY);
    if (tx_ok != pdPASS) {
      s_tx_test_task = nullptr;
      app_log_write("WARN", "RS485: tarefa de teste TX (5s) nao criada.");
    } else {
      app_log_write("INFO", "RS485: teste TX ativo (5 s) na Serial1.");
    }
  }
#endif
}

uint32_t cycles_rs485_today_line_count(void) {
  return s_today_lines.load(std::memory_order_relaxed);
}

void cycles_rs485_last_write_hhmmss(char *out, size_t out_sz) {
  if (out == nullptr || out_sz < 9U) {
    return;
  }
  const time_t t = s_last_write_time.load(std::memory_order_relaxed);
  if (t == 0) {
    strncpy(out, "--:--:--", out_sz);
    out[out_sz - 1U] = '\0';
    return;
  }
  struct tm lt {};
  if (localtime_r(&t, &lt) == nullptr) {
    strncpy(out, "--:--:--", out_sz);
    out[out_sz - 1U] = '\0';
    return;
  }
  (void)snprintf(out, out_sz, "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
}
