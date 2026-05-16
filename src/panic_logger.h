#pragma once
/* panic_logger — RTC_NOINIT_ATTR breadcrumb persistent across resets.
 *
 * Long-running paths set breadcrumb before risky operations. On next boot,
 * panic_logger_init() reads the breadcrumb and appends to boot_journal as
 * "PANIC | Last breadcrumb before reset: <tag>". Identifies exact code
 * position when TWDT/panic/heap_guard hits. Cleared after read so reboot
 * via clean shutdown shows no breadcrumb.
 *
 * Safe to call from any task context. Not ISR-safe (uses snprintf).
 *
 * v1.84 — added to diagnose TASK_WDT reboots in v1.83 stress test.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Set breadcrumb tag (truncated to 31 chars). Called before risky op. */
void panic_breadcrumb_set(const char *tag);

/** Clear breadcrumb after successful completion of risky op. */
void panic_breadcrumb_clear(void);

/** Called once at boot, AFTER boot_journal_init/reset. Reads RTC breadcrumb,
 *  if magic valid logs "PANIC: last breadcrumb=<tag>" to boot_journal then
 *  invalidates magic. */
void panic_logger_init(void);

#ifdef __cplusplus
}
#endif
