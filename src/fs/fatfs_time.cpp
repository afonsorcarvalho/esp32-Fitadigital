#include "app_settings.h"
#include "net_time.h"

#include <stdint.h>
#include <time.h>

/**
 * Empacota struct tm (hora local) no formato FAT:
 * YYYYYYYMMMMDDDDDHHHHHMMMMMMSSSSS (segundos/2).
 */
static uint32_t pack_fat_datetime_from_tm(const struct tm *t) {
  if (t == nullptr) {
    return 0;
  }

  int year = t->tm_year + 1900;
  if (year < 1980) {
    year = 1980;
  } else if (year > 2107) {
    year = 2107;
  }
  const uint32_t y = (uint32_t)(year - 1980);
  const uint32_t mon = (uint32_t)(t->tm_mon + 1);
  const uint32_t mday = (uint32_t)t->tm_mday;
  const uint32_t hour = (uint32_t)t->tm_hour;
  const uint32_t min = (uint32_t)t->tm_min;
  const uint32_t sec2 = (uint32_t)t->tm_sec / 2U;

  return (y << 25) | (mon << 21) | (mday << 16) | (hour << 11) | (min << 5) | sec2;
}

/**
 * Wrapper de linker para get_fattime do FatFs (via -Wl,--wrap=get_fattime).
 * Politica: usar RTC como fonte primaria; fallback para relogio local do sistema;
 * fallback final para 2020-01-01 00:00:00.
 */
extern "C" uint32_t __wrap_get_fattime(void) {
  struct tm ti = {};

  time_t rtc_utc = 0;
  if (net_time_get_rtc_epoch_utc(&rtc_utc)) {
    const time_t rtc_local = rtc_utc + (time_t)app_settings_tz_offset_sec();
    if (gmtime_r(&rtc_local, &ti) != nullptr) {
      return pack_fat_datetime_from_tm(&ti);
    }
  }

  const time_t now = time(nullptr);
  if (now > (time_t)1577836800 && localtime_r(&now, &ti) != nullptr) {
    return pack_fat_datetime_from_tm(&ti);
  }

  /* 2020-01-01 00:00:00 local */
  return ((uint32_t)(2020 - 1980) << 25) | (1U << 21) | (1U << 16);
}

