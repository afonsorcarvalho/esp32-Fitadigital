#pragma once
/* build_features.h — compile-time feature flags for incremental soak testing.
 *
 * Default: all enabled (full firmware behavior).
 * Per-stage soak: override via platformio.ini build_flags `-DFITA_ENABLE_X=0`.
 *
 * Stages (see TODO + session 2026-05-17):
 *  Stage 1: RS485+WiFi only (all FITA_ENABLE_X=0 except RS485+WiFi which always on)
 *  Stage 2: + WG
 *  Stage 3: + FTP (probe via curl FTP each 1min)
 *  Stage 4: + WEB_PORTAL (probe /api/health)
 *
 * Always-on regardless of flags: app_settings, panic_logger, boot_journal,
 * service_supervisor, heap_monitor, LVGL UI, RS485 capture, WiFi STA.
 */

#ifndef FITA_ENABLE_WG
#define FITA_ENABLE_WG 1
#endif
#ifndef FITA_ENABLE_FTP
#define FITA_ENABLE_FTP 1
#endif
#ifndef FITA_ENABLE_WEB_PORTAL
#define FITA_ENABLE_WEB_PORTAL 1
#endif
#ifndef FITA_ENABLE_MQTT
#define FITA_ENABLE_MQTT 1
#endif
#ifndef FITA_ENABLE_SCREENSHOT
#define FITA_ENABLE_SCREENSHOT 1
#endif
#ifndef FITA_ENABLE_NTP
#define FITA_ENABLE_NTP 1
#endif
