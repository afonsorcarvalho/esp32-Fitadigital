#include "app_log.h"

#include "sd_access.h"

#include <Arduino.h>
#include <SD.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *const kLogPath = "/fdigi.log";
static const char *const kLogTmpPath = "/fdigi.log.tmp";
static const size_t kLogMaxBytes = 10U * 1024U * 1024U;
static const size_t kLogKeepBytesAfterRotate = 8U * 1024U * 1024U;
static const size_t kCopyBufSize = 1024U;

static SemaphoreHandle_t s_log_mutex = nullptr;

static void (*s_line_notify)(const char *line, void *user) = nullptr;
static void *s_line_notify_user = nullptr;

static bool sd_ready(void) {
  return sd_access_is_mounted();
}

static void lock_log(void) {
  if (s_log_mutex != nullptr) {
    (void)xSemaphoreTake(s_log_mutex, portMAX_DELAY);
  }
}

static void unlock_log(void) {
  if (s_log_mutex != nullptr) {
    (void)xSemaphoreGive(s_log_mutex);
  }
}

static void format_ts(char *dst, size_t cap) {
  if (dst == nullptr || cap == 0U) {
    return;
  }
  const time_t now = time(nullptr);
  struct tm ti;
  if (now > (time_t)1577836800 && localtime_r(&now, &ti) != nullptr) {
    if (strftime(dst, cap, "%Y-%m-%d %H:%M:%S", &ti) > 0U) {
      return;
    }
  }
  const unsigned long ms = millis();
  snprintf(dst, cap, "1970-01-01 00:00:%02lu", (unsigned long)((ms / 1000UL) % 60UL));
}

static bool rotate_if_needed_locked(void) {
  File src = SD.open(kLogPath, FILE_READ);
  if (!src) {
    return true;
  }
  const size_t sz = src.size();
  if (sz <= kLogMaxBytes) {
    src.close();
    return true;
  }

  SD.remove(kLogTmpPath);
  File tmp = SD.open(kLogTmpPath, FILE_WRITE);
  if (!tmp) {
    src.close();
    return false;
  }

  const size_t keep = (sz > kLogKeepBytesAfterRotate) ? kLogKeepBytesAfterRotate : sz;
  size_t start = sz - keep;
  src.seek(start);

  /* Evita começar no meio de uma linha após rotação */
  if (start != 0U) {
    int c;
    while ((c = src.read()) >= 0) {
      if (c == '\n') {
        break;
      }
    }
  }

  uint8_t copy_buf[kCopyBufSize];
  for (;;) {
    const int n = src.read(copy_buf, sizeof(copy_buf));
    if (n <= 0) {
      break;
    }
    (void)tmp.write(copy_buf, (size_t)n);
  }

  tmp.close();
  src.close();
  SD.remove(kLogPath);
  SD.rename(kLogTmpPath, kLogPath);
  return true;
}

void app_log_init(void) {
  if (s_log_mutex == nullptr) {
    s_log_mutex = xSemaphoreCreateMutex();
  }
  if (!sd_ready()) {
    return;
  }
  lock_log();
  sd_access_sync([&] {
    File f = SD.open(kLogPath, FILE_APPEND);
    if (f) {
      f.close();
    }
  });
  unlock_log();
}

void app_log_set_line_notify(void (*fn)(const char *line, void *user), void *user) {
  s_line_notify = fn;
  s_line_notify_user = user;
}

void app_log_clear_line_notify(void) {
  s_line_notify = nullptr;
  s_line_notify_user = nullptr;
}

void app_log_write(const char *level, const char *message) {
  if (!sd_ready() || message == nullptr) {
    return;
  }
  char ts[24];
  format_ts(ts, sizeof(ts));
  const char *lvl = (level != nullptr && level[0] != '\0') ? level : "INFO";

  char line_notify[384];
  snprintf(line_notify, sizeof(line_notify), "%s | %s | %s", ts, lvl, message);

  lock_log();
  sd_access_sync([&] {
    File f = SD.open(kLogPath, FILE_APPEND);
    if (f) {
      f.printf("%s | %s | %s\n", ts, lvl, message);
      f.close();
      (void)rotate_if_needed_locked();
    }
  });
  unlock_log();

  if (s_line_notify != nullptr) {
    s_line_notify(line_notify, s_line_notify_user);
  }
}

void app_log_writef(const char *level, const char *fmt, ...) {
  if (fmt == nullptr) {
    return;
  }
  char msg[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  app_log_write(level, msg);
}

void app_log_feature_write(const char *level, const char *feature, const char *message) {
  if (message == nullptr) {
    return;
  }
  const char *feat = (feature != nullptr && feature[0] != '\0') ? feature : "CORE";
  char line[320];
  snprintf(line, sizeof(line), "[%s] %s", feat, message);
  app_log_write(level, line);
}

void app_log_feature_writef(const char *level, const char *feature, const char *fmt, ...) {
  if (fmt == nullptr) {
    return;
  }
  char msg[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  app_log_feature_write(level, feature, msg);
}

bool app_log_read_tail(char *buf, size_t buf_size, size_t *out_file_size, bool *out_truncated) {
  if (buf == nullptr || buf_size == 0U) {
    return false;
  }
  buf[0] = '\0';
  if (out_file_size != nullptr) {
    *out_file_size = 0U;
  }
  if (out_truncated != nullptr) {
    *out_truncated = false;
  }
  if (!sd_ready()) {
    return false;
  }

  lock_log();
  bool ok = false;
  sd_access_sync([&] {
    File f = SD.open(kLogPath, FILE_READ);
    if (!f) {
      return;
    }

    const size_t total = f.size();
    if (out_file_size != nullptr) {
      *out_file_size = total;
    }
    const size_t room = buf_size - 1U;
    size_t start = 0U;
    bool truncated = false;
    if (total > room) {
      start = total - room;
      truncated = true;
    }
    f.seek(start);
    size_t n = f.read((uint8_t *)buf, room);
    buf[n] = '\0';
    f.close();

    if (truncated && start > 0U) {
      char *nl = strchr(buf, '\n');
      if (nl != nullptr && nl[1] != '\0') {
        memmove(buf, nl + 1, strlen(nl + 1U) + 1U);
      }
    }
    if (out_truncated != nullptr) {
      *out_truncated = truncated;
    }
    ok = true;
  });
  unlock_log();
  return ok;
}

bool app_log_clear(void) {
  if (!sd_ready()) {
    return false;
  }
  lock_log();
  bool ok = false;
  sd_access_sync([&] {
    SD.remove(kLogPath);
    File f = SD.open(kLogPath, FILE_WRITE);
    ok = (bool)f;
    if (f) {
      f.close();
    }
  });
  unlock_log();
  return ok;
}

const char *app_log_path(void) {
  return kLogPath;
}

size_t app_log_max_size_bytes(void) {
  return kLogMaxBytes;
}
