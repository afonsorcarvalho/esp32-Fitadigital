#pragma once
/* build_features.h — branch v2 (linha 2.x.x) scope reduzido.
 *
 * v2 inclui: WiFi STA, HTTP portal, RS485 captura+logico, NTP, FTP server.
 * v2 EXCLUI (default): WG, MQTT, SCREENSHOT (cliente browser snapshot remoto).
 * FTP CLIENT (push para servidor) sera adicionado depois de tudo estavel.
 *
 * Always-on regardless of flags: app_settings, panic_logger, boot_journal,
 * service_supervisor, heap_monitor, LVGL UI, RS485 capture, WiFi STA.
 */

/* WG desactivado em v2 (heranca do main 3.X v1.95 Hibrido). */
#ifndef FITA_ENABLE_WG
#define FITA_ENABLE_WG 0
#endif
/* FTP server (SimpleFTPServer) — necessario para upload manual ficheiros via UI. */
#ifndef FITA_ENABLE_FTP
#define FITA_ENABLE_FTP 1
#endif
/* HTTP portal de configuracao + logs + file browser. Core feature v2. */
#ifndef FITA_ENABLE_WEB_PORTAL
#define FITA_ENABLE_WEB_PORTAL 1
#endif
/* v2: MQTT removido do scope inicial. Pode re-activar depois com flag explicita. */
#ifndef FITA_ENABLE_MQTT
#define FITA_ENABLE_MQTT 0
#endif
/* v2: SCREENSHOT removido (sem cliente VNC/browser remoto inicial). */
#ifndef FITA_ENABLE_SCREENSHOT
#define FITA_ENABLE_SCREENSHOT 0
#endif
/* NTP sync via SNTP — core feature v2 para timestamps RS485 cycles. */
#ifndef FITA_ENABLE_NTP
#define FITA_ENABLE_NTP 1
#endif
