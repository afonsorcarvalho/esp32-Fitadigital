#include "panic_logger.h"
#include "boot_journal.h"

#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include "esp_attr.h"

namespace {
constexpr uint32_t kBreadcrumbMagic = 0xB6EADC60U;  /* "BREADCRUMB-O" */
constexpr size_t   kBreadcrumbMax   = 32U;

RTC_NOINIT_ATTR uint32_t s_magic;
RTC_NOINIT_ATTR char     s_tag[kBreadcrumbMax];
} /* namespace */

void panic_breadcrumb_set(const char *tag) {
  if (tag == nullptr) {
    s_magic = 0U;
    return;
  }
  strncpy(s_tag, tag, kBreadcrumbMax - 1U);
  s_tag[kBreadcrumbMax - 1U] = '\0';
  s_magic = kBreadcrumbMagic;
}

void panic_breadcrumb_clear(void) {
  s_magic = 0U;
  s_tag[0] = '\0';
}

void panic_logger_init(void) {
  if (s_magic == kBreadcrumbMagic) {
    s_tag[kBreadcrumbMax - 1U] = '\0';  /* defensive: ensure NUL-terminated */
    char buf[64];
    snprintf(buf, sizeof(buf), "Last breadcrumb before reset: %s", s_tag);
    Serial.printf("[PANIC] %s\n", buf);
    boot_journal_append("PANIC", buf);
    /* boot_journal_flush_to_spiffs is called elsewhere at end of boot; we
     * leave persistence to standard path to avoid duplicate SPIFFS writes
     * here in early boot before SPIFFS is fully validated by other modules. */
  }
  panic_breadcrumb_clear();
}
