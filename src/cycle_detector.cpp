/**
 * @file cycle_detector.cpp
 * @brief Implementacao do state machine de ciclos RS485. Ver cycle_detector.h.
 */
#include "cycle_detector.h"
#include "app_log.h"
#include "cycles_rs485.h"
#include "sd_access.h"

#include <Arduino.h>
#include <SD.h>
#include <cstring>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/* -------------------------------------------------------------------------- */
/* Estado interno                                                              */
/* -------------------------------------------------------------------------- */

namespace {

enum class State : uint8_t { IDLE = 0, ACTIVE = 1 };

constexpr size_t kPatternMax = 48;
constexpr size_t kFirstLineMax = 64;

struct Config {
  char start_pat[kPatternMax];
  char end_pat[kPatternMax];
  uint32_t idle_timeout_s;
  bool enabled;
};

struct Current {
  State state;
  time_t start_ts;
  time_t last_line_ts;
  uint32_t line_count;
  uint32_t byte_count;
  char first_line[kFirstLineMax];
};

Config s_cfg = {};
Current s_cur = {};
SemaphoreHandle_t s_mtx = nullptr;

/* v2.1.2 watchdog: baseline em RTC slow memory (sobrevive reset + fora do BSS
 * principal, menos vulneravel a stack/heap smash). Magic protege contra dados
 * lixo no primeiro boot. Restaurado por cycle_detector_tick se s_cfg zerado
 * em runtime detectado. */
constexpr uint32_t kBaselineMagic = 0xC0DE2026U;
struct ConfigBaseline {
  char start_pat[kPatternMax];
  char end_pat[kPatternMax];
  uint32_t idle_timeout_s;
};
RTC_NOINIT_ATTR ConfigBaseline s_baseline;
RTC_NOINIT_ATTR uint32_t s_baseline_magic;
/* Throttle warning de auto-heal para nao spammar log se corrupcao persistir. */
uint32_t s_last_heal_warn_ms = 0;

/* Locking pequeno; estado e' tudo POD, escrita NDJSON sai do lock antes do
 * sd_access_sync para nao serializar o reader_task no SD I/O. */
struct Lock {
  Lock() {
    if (s_mtx == nullptr) s_mtx = xSemaphoreCreateMutex();
    if (s_mtx != nullptr) xSemaphoreTake(s_mtx, portMAX_DELAY);
  }
  ~Lock() {
    if (s_mtx != nullptr) xSemaphoreGive(s_mtx);
  }
};

/* strcasestr local — Arduino-ESP32 newlib nao garante. */
const char *kw_strcasestr(const char *haystack, const char *needle) {
  if (haystack == nullptr || needle == nullptr || *needle == '\0') return nullptr;
  for (; *haystack != '\0'; ++haystack) {
    const char *h = haystack;
    const char *n = needle;
    while (*h != '\0' && *n != '\0' &&
           tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
      ++h; ++n;
    }
    if (*n == '\0') return haystack;
  }
  return nullptr;
}

void format_iso8601(time_t ts, char *out, size_t cap) {
  struct tm lt {};
  if (localtime_r(&ts, &lt) == nullptr) {
    snprintf(out, cap, "1970-01-01T00:00:00");
    return;
  }
  strftime(out, cap, "%Y-%m-%dT%H:%M:%S", &lt);
}

const char *status_str(CycleDetectorStatus s) {
  switch (s) {
    case CYCLE_STATUS_DONE: return "DONE";
    case CYCLE_STATUS_TIMEOUT: return "TIMEOUT";
    case CYCLE_STATUS_INTERRUPTED: return "INTERRUPTED";
  }
  return "UNKNOWN";
}

void escape_json_inplace(char *s) {
  for (; *s != '\0'; ++s) {
    if (*s == '"' || *s == '\\') *s = '_';
    else if ((unsigned char)*s < 0x20) *s = ' ';
  }
}

void emit_cycle_close(const Current &snap, CycleDetectorStatus st) {
  /* Constroi payload NDJSON local (stack ~256 B). */
  char start_iso[24];
  char end_iso[24];
  char first_safe[kFirstLineMax];
  format_iso8601(snap.start_ts, start_iso, sizeof start_iso);
  format_iso8601(snap.last_line_ts, end_iso, sizeof end_iso);
  strncpy(first_safe, snap.first_line, sizeof first_safe - 1U);
  first_safe[sizeof first_safe - 1U] = '\0';
  escape_json_inplace(first_safe);

  const uint32_t dur = (uint32_t)((snap.last_line_ts > snap.start_ts)
                                       ? (snap.last_line_ts - snap.start_ts) : 0);

  char line[320];
  const int n = snprintf(line, sizeof line,
      "{\"start\":\"%s\",\"end\":\"%s\",\"duration_s\":%u,\"lines\":%u,"
      "\"bytes\":%u,\"status\":\"%s\",\"first_line\":\"%s\"}\n",
      start_iso, end_iso, (unsigned)dur,
      (unsigned)snap.line_count, (unsigned)snap.byte_count,
      status_str(st), first_safe);
  if (n <= 0 || (size_t)n >= sizeof line) return;

  /* Path NDJSON do mes do INICIO do ciclo (nao do fim — agrupamento estavel). */
  struct tm lt {};
  if (localtime_r(&snap.start_ts, &lt) == nullptr) return;
  const int year = lt.tm_year + 1900;
  const int month = lt.tm_mon + 1;
  char path[48];
  if (snprintf(path, sizeof path, "/CICLOS/%04d/%02d/cycles.ndjson", year, month)
      >= (int)sizeof path) return;

  /* Escreve NDJSON via sd_access_sync (executa em sd_io task). */
  const int payload_len = n;
  sd_access_sync([year, month, path, line, payload_len]() {
    if (!sd_access_is_mounted()) return;
    cycles_rs485_mkdirs_for_ym(year, month);
    File f = SD.open(path, FILE_APPEND);
    if (!f) {
      app_log_writef("WARN", "CYCLE: falha append %s", path);
      return;
    }
    (void)f.write(reinterpret_cast<const uint8_t *>(line), payload_len);
    f.close();
  });

  /* v2.1.2: NOT logging here. Backtrace v2.11 mostrou crash em
   * xQueueGenericSend -> cross-core yield IPI durante log enqueue de dentro
   * de reader_task. SMP critical section race quando /api/cycles/list HTTP
   * task em outro core compete. NDJSON entry + /api/cycles/status sao
   * suficientes para observabilidade — log redundante removido. */
  (void)dur; (void)st; (void)snap;
}

void reset_current(time_t now, const char *first_line, size_t first_len) {
  s_cur.state = State::ACTIVE;
  s_cur.start_ts = now;
  s_cur.last_line_ts = now;
  s_cur.line_count = 1;
  s_cur.byte_count = (uint32_t)first_len;
  const size_t copy = (first_len < sizeof s_cur.first_line - 1U)
                          ? first_len : sizeof s_cur.first_line - 1U;
  memcpy(s_cur.first_line, first_line, copy);
  s_cur.first_line[copy] = '\0';
}

void mark_idle(void) {
  s_cur.state = State::IDLE;
  s_cur.start_ts = 0;
  s_cur.last_line_ts = 0;
  s_cur.line_count = 0;
  s_cur.byte_count = 0;
  s_cur.first_line[0] = '\0';
}

} /* anonymous namespace */

/* -------------------------------------------------------------------------- */
/* API publica                                                                  */
/* -------------------------------------------------------------------------- */

void cycle_detector_init(const char *start_pattern,
                         const char *end_pattern,
                         uint32_t idle_timeout_s) {
  Lock lk;
  memset(&s_cfg, 0, sizeof s_cfg);
  memset(&s_cur, 0, sizeof s_cur);
  if (start_pattern != nullptr && *start_pattern != '\0') {
    strncpy(s_cfg.start_pat, start_pattern, sizeof s_cfg.start_pat - 1U);
    s_cfg.enabled = true;
  }
  if (end_pattern != nullptr && *end_pattern != '\0') {
    strncpy(s_cfg.end_pat, end_pattern, sizeof s_cfg.end_pat - 1U);
  }
  s_cfg.idle_timeout_s = idle_timeout_s;
  s_cur.state = State::IDLE;

  /* v2.1.2: snapshot baseline para watchdog auto-heal se s_cfg corromper. */
  if (s_cfg.enabled) {
    memset(&s_baseline, 0, sizeof s_baseline);
    strncpy(s_baseline.start_pat, s_cfg.start_pat, sizeof s_baseline.start_pat - 1U);
    strncpy(s_baseline.end_pat, s_cfg.end_pat, sizeof s_baseline.end_pat - 1U);
    s_baseline.idle_timeout_s = s_cfg.idle_timeout_s;
    s_baseline_magic = kBaselineMagic;
  }

  app_log_feature_writef("INFO", "CYCLE",
                         "init start='%s' end='%s' idle=%us enabled=%s",
                         s_cfg.start_pat, s_cfg.end_pat,
                         (unsigned)s_cfg.idle_timeout_s,
                         s_cfg.enabled ? "yes" : "no");
}

/* v2.1.2: detecta corrupcao runtime de s_cfg + restaura baseline RTC.
 * Chamado de cycle_detector_tick. Lock ja tomado pelo caller.
 * Retorna true se restauracao foi necessaria. */
static bool watchdog_heal_locked(void) {
  if (s_cfg.enabled) return false;
  if (s_baseline_magic != kBaselineMagic) return false;
  if (s_baseline.start_pat[0] == '\0') return false;

  memset(&s_cfg, 0, sizeof s_cfg);
  strncpy(s_cfg.start_pat, s_baseline.start_pat, sizeof s_cfg.start_pat - 1U);
  strncpy(s_cfg.end_pat, s_baseline.end_pat, sizeof s_cfg.end_pat - 1U);
  s_cfg.idle_timeout_s = s_baseline.idle_timeout_s;
  s_cfg.enabled = true;
  memset(&s_cur, 0, sizeof s_cur);
  s_cur.state = State::IDLE;
  return true;
}

void cycle_detector_process_line(const char *line, size_t len, time_t now) {
  if (line == nullptr || len == 0) return;
  Current snap_to_emit;
  bool emit = false;
  CycleDetectorStatus emit_status = CYCLE_STATUS_DONE;

  {
    Lock lk;
    if (!s_cfg.enabled) return;

    const bool has_start = (s_cfg.start_pat[0] != '\0') &&
                           (kw_strcasestr(line, s_cfg.start_pat) != nullptr);
    const bool has_end = (s_cfg.end_pat[0] != '\0') &&
                         (kw_strcasestr(line, s_cfg.end_pat) != nullptr);

    if (s_cur.state == State::IDLE) {
      if (has_start) {
        reset_current(now, line, len);
        /* v2.1.2: dropped log (ver emit_cycle_close coment). */
      }
      /* Sem start_pattern: ignora linhas em IDLE (lixo entre ciclos). */
    } else {
      /* ACTIVE */
      s_cur.last_line_ts = now;
      s_cur.line_count++;
      s_cur.byte_count += (uint32_t)len;

      if (has_end) {
        snap_to_emit = s_cur;
        emit = true;
        emit_status = CYCLE_STATUS_DONE;
        mark_idle();
      } else if (has_start) {
        /* Novo start sem fim explicito = ciclo anterior INTERRUPTED. */
        snap_to_emit = s_cur;
        emit = true;
        emit_status = CYCLE_STATUS_INTERRUPTED;
        reset_current(now, line, len);
      }
    }
  } /* lock released */

  if (emit) emit_cycle_close(snap_to_emit, emit_status);
}

void cycle_detector_tick(time_t now) {
  Current snap_to_emit;
  bool emit = false;
  bool healed = false;

  {
    Lock lk;
    healed = watchdog_heal_locked();
    if (!s_cfg.enabled) return;
    if (s_cfg.idle_timeout_s == 0U) {
      /* fall-through: log heal abaixo se ocorreu */
    } else if (s_cur.state == State::ACTIVE) {
      const time_t elapsed = now - s_cur.last_line_ts;
      if (elapsed >= (time_t)s_cfg.idle_timeout_s) {
        snap_to_emit = s_cur;
        emit = true;
        mark_idle();
      }
    }
  }

  if (healed) {
    /* v2.1.2: no log enqueue inside reader_task (SMP yield crash risk).
     * Heal count exposed via /api/cycles/status quando addr. */
    s_last_heal_warn_ms = (uint32_t)millis();
  }
  if (emit) emit_cycle_close(snap_to_emit, CYCLE_STATUS_TIMEOUT);
}

size_t cycle_detector_status_json(char *out, size_t cap) {
  if (out == nullptr || cap < 64U) return 0;
  Lock lk;
  const char *state_str = (s_cur.state == State::ACTIVE) ? "ACTIVE" : "IDLE";
  char first_safe[kFirstLineMax];
  strncpy(first_safe, s_cur.first_line, sizeof first_safe - 1U);
  first_safe[sizeof first_safe - 1U] = '\0';
  escape_json_inplace(first_safe);
  const int n = snprintf(out, cap,
      "{\"enabled\":%s,\"state\":\"%s\",\"start_pattern\":\"%s\","
      "\"end_pattern\":\"%s\",\"idle_timeout_s\":%u,"
      "\"current\":{\"start_ts\":%ld,\"last_line_ts\":%ld,"
      "\"lines\":%u,\"bytes\":%u,\"first_line\":\"%s\"}}",
      s_cfg.enabled ? "true" : "false",
      state_str, s_cfg.start_pat, s_cfg.end_pat,
      (unsigned)s_cfg.idle_timeout_s,
      (long)s_cur.start_ts, (long)s_cur.last_line_ts,
      (unsigned)s_cur.line_count, (unsigned)s_cur.byte_count, first_safe);
  if (n <= 0 || (size_t)n >= cap) return 0;
  return (size_t)n;
}

void cycle_detector_reconfigure(const char *start_pattern,
                                const char *end_pattern,
                                uint32_t idle_timeout_s) {
  Current snap;
  bool emit_interrupted = false;
  {
    Lock lk;
    if (s_cur.state == State::ACTIVE) {
      snap = s_cur;
      emit_interrupted = true;
    }
  }  /* lock released antes de emit_cycle_close (faz sd_access_sync) */
  if (emit_interrupted) {
    emit_cycle_close(snap, CYCLE_STATUS_INTERRUPTED);
  }
  cycle_detector_init(start_pattern, end_pattern, idle_timeout_s);
}
