/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Base: examples/PlatformIO/src/app.cpp da ESP32_Display_Panel v0.2.2
 * (https://github.com/esp-arduino-libs/ESP32_Display_Panel ).
 * Placa: BOARD_WAVESHARE_ESP32_S3_Touch_LCD_4_3_B em ESP_Panel_Board_Supported.h.
 * Waveshare: https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B
 */

#include <Arduino.h>
#include <ESP_Panel_Library.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <SPI.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <WiFi.h>

#include "lvgl_port_v8.h"
#include "app_settings.h"
#include "board_pins.h"
#include "waveshare_sd_cs.h"
#include "boot_journal.h"
#include "SD.h"
#include "app_log.h"
#include "net_services.h"
#include "net_time.h"
#include "ui_feedback.h"
#include "ui/boot_screen.h"
#include "ui/ui_app.h"
#include "web_remote/web_remote.h"

/** SPI dedicado ao TF (pinos em `board_pins.h`, documentação Waveshare). */
static SPIClass s_sd_spi;

/** Registo só na porta série e no journal (sem alterar linhas do ecrã de arranque). */
static void boot_log_plain(const char *level, const char *fmt, ...) {
  char msg[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  Serial.printf("[BOOT][%s] %s\n", level ? level : "INFO", msg);
  boot_journal_append(level, msg);
}

/**
 * Registo + actualização da linha do passo no ecrã de arranque.
 * @param step Indice do subsistema (SD, RTC, …).
 */
static void boot_log_step(boot_step_t step, const char *level, const char *fmt, ...) {
  char msg[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  Serial.printf("[BOOT][%s] %s\n", level ? level : "INFO", msg);
  boot_journal_append(level, msg);
  boot_screen_set_step(step, level ? level : "INFO");
}

static bool sd_rw_self_test(void) {
  static const char *kTestPath = "/.__boot_rw_test.tmp";
  static const char *kMagic = "BOOT_SD_OK";
  File w = SD.open(kTestPath, FILE_WRITE);
  if (!w) {
    return false;
  }
  const size_t wn = w.print(kMagic);
  w.close();
  if (wn != strlen(kMagic)) {
    SD.remove(kTestPath);
    return false;
  }
  char rd[16] = {0};
  File r = SD.open(kTestPath, FILE_READ);
  if (!r) {
    SD.remove(kTestPath);
    return false;
  }
  const int rn = r.read((uint8_t *)rd, sizeof(rd) - 1U);
  r.close();
  SD.remove(kTestPath);
  if (rn <= 0) {
    return false;
  }
  rd[rn] = '\0';
  return strcmp(rd, kMagic) == 0;
}

/**
 * Tenta montar o SD com várias velocidades SPI.
 * Erro FatFs (13) = sem volume FAT válido: muitas vezes é tabela GPT (FatFs no ESP espera MBR)
 * ou leitura SPI frágil a 10 MHz — ver serial e documentação Waveshare.
 */
static bool mount_sd_fallback(void) {
  const uint32_t speeds_hz[] = {400000, 1000000, 4000000, 10000000};
  waveshare_sd_cs_set(true);
  for (uint32_t hz : speeds_hz) {
    SD.end();
    delay(20);
    if (SD.begin(0xFF, s_sd_spi, hz, "/sd", 10, false)) {
      Serial.printf("[SD] Montado a %lu Hz\n", (unsigned long)hz);
      return true;
    }
    Serial.printf("[SD] Falhou a %lu Hz (veja log FatFs acima)\n", (unsigned long)hz);
  }
  Serial.println(
      "[SD] Dica: no PC, confirme que o cartao e' MBR (nao GPT). "
      "DiskPart: list disk -> detail disk -> se GPT, apague volume e crie particao primaria MBR + FAT32.");
  return false;
}

static bool sd_mount_with_retries(uint8_t tries) {
  for (uint8_t i = 1; i <= tries; ++i) {
    boot_log_step(BOOT_STEP_SD, "INFO", "tentativa %u/%u", (unsigned)i, (unsigned)tries);
    if (mount_sd_fallback() && sd_rw_self_test()) {
      boot_log_step(BOOT_STEP_SD, "INFO", "montado + leitura/escrita OK");
      return true;
    }
    boot_log_step(BOOT_STEP_SD, "WARN", "falha na tentativa %u", (unsigned)i);
    delay(200);
  }
  return false;
}

static bool rtc_probe_with_retries(uint8_t tries) {
  for (uint8_t i = 1; i <= tries; ++i) {
    bool rtc_has_valid_time = false;
    if (net_time_probe_rtc(&rtc_has_valid_time)) {
      boot_log_step(BOOT_STEP_RTC, "INFO", "respondeu (hora %s)",
                    rtc_has_valid_time ? "valida" : "invalida");
      return true;
    }
    boot_log_step(BOOT_STEP_RTC, "WARN", "sem resposta (tentativa %u/%u)", (unsigned)i, (unsigned)tries);
    delay(200);
  }
  return false;
}

static bool wifi_connect_wait(uint32_t timeout_ms) {
  const uint32_t t0 = millis();
  while ((millis() - t0) < timeout_ms) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

void setup() {
  const char *title = "FitaDigital UI — LCD 4.3B";

  Serial.begin(115200);
  Serial.println(String(title) + " start");
  app_settings_init();
  (void)boot_journal_init();
  (void)boot_journal_reset();

  Serial.println("Initialize panel device");
  ESP_Panel *panel = new ESP_Panel();
  panel->init();
#if LVGL_PORT_AVOID_TEAR
  ESP_PanelBus_RGB *rgb_bus = static_cast<ESP_PanelBus_RGB *>(panel->getLcd()->getBus());
  rgb_bus->configRgbFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
  rgb_bus->configRgbBounceBufferSize(LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE);
#endif
  panel->begin();
  ui_feedback_init();
  lvgl_port_init(panel->getLcd(), panel->getTouch());
  boot_screen_show();
  /* Subtítulo antes do journal: feedback imediato no LCD mesmo se SPIFFS.append demorar. */
  boot_screen_set_subtitle("LCD e LVGL prontos; a iniciar subsistemas.");
  boot_log_plain("INFO", "=== Boot FitaDigital ===");

  bool sd_ok = false;
  if (panel->getExpander() == nullptr) {
    boot_log_step(BOOT_STEP_SD, "ERROR", "expander CH422G indisponivel");
  } else {
    waveshare_sd_cs_bind(panel->getExpander());
    sd_ok = sd_mount_with_retries(3U);
    if (sd_ok) {
      app_log_init();
      app_log_write("INFO", "Sistema iniciado; SD montado.");
      const bool copied = boot_journal_copy_to_sd();
      boot_log_step(BOOT_STEP_SD, copied ? "INFO" : "WARN", "copia boot.log p/ SD %s",
                    copied ? "OK" : "adiada");
    } else {
      boot_log_step(BOOT_STEP_SD, "ERROR", "falha apos 3 tentativas");
    }
  }
  boot_journal_start_sd_mirror_task();

  if (rtc_probe_with_retries(3U)) {
    net_time_bootstrap_from_rtc_early();
  } else {
    boot_log_step(BOOT_STEP_RTC, "ERROR", "falha apos 3 tentativas");
  }

  bool wifi_connected = false;
  if (app_settings_wifi_configured()) {
    boot_log_step(BOOT_STEP_WIFI, "INFO", "a ligar a rede configurada...");
    boot_screen_set_subtitle("Wi-Fi: a ligar (ate 15 s)...");
    net_wifi_begin_saved();
    wifi_connected = wifi_connect_wait(15000U);
    if (wifi_connected) {
      boot_log_step(BOOT_STEP_WIFI, "INFO", "ligado IP=%s", WiFi.localIP().toString().c_str());
      boot_screen_set_subtitle("Wi-Fi ligado.");
    } else {
      boot_log_step(BOOT_STEP_WIFI, "WARN", "sem ligacao no timeout");
      boot_screen_set_subtitle("Wi-Fi: timeout.");
    }
  } else {
    boot_log_step(BOOT_STEP_WIFI, "WARN", "sem rede configurada");
    boot_screen_set_subtitle("Wi-Fi: nao configurado.");
  }

  if (wifi_connected && app_settings_ntp_enabled()) {
    char ntp_msg[80];
    boot_screen_set_subtitle("NTP: a sincronizar...");
    const bool ntp_ok = net_time_sync_now_blocking(ntp_msg, sizeof(ntp_msg));
    if (ntp_ok) {
      net_time_push_system_time_to_rtc("boot-ntp");
      boot_log_step(BOOT_STEP_NTP, "INFO", "sincronizado; RTC atualizado");
      boot_screen_set_subtitle("NTP OK.");
    } else {
      boot_log_step(BOOT_STEP_NTP, "WARN", "falhou (%s)", ntp_msg);
      boot_screen_set_subtitle("NTP falhou.");
    }
  } else if (!app_settings_ntp_enabled()) {
    boot_log_step(BOOT_STEP_NTP, "INFO", "desativado");
  } else {
    boot_log_step(BOOT_STEP_NTP, "WARN", "sem Wi-Fi para sincronizar");
  }

  const bool ftp_cfg = app_settings_ftp_user().length() > 0;
  boot_log_step(BOOT_STEP_FTP, ftp_cfg ? "INFO" : "WARN", "%s",
                ftp_cfg ? "configurado (servico em segundo plano)" : "nao configurado");

  const bool wg_cfg = app_settings_wireguard_enabled();
  boot_log_step(BOOT_STEP_WIREGUARD, wg_cfg ? "INFO" : "WARN", "%s",
                wg_cfg ? "ativo por configuracao" : "desativado");

  const bool web_remote_cfg = true;  // Sem flag dedicada; habilitado por predefinicao.
  boot_log_step(BOOT_STEP_WEB_REMOTE, web_remote_cfg ? "INFO" : "WARN", "%s",
                web_remote_cfg ? "ativo por configuracao" : "desativado");

  boot_log_plain("INFO", "Boot concluido. A carregar interface principal...");
  boot_screen_set_footer("A carregar interface principal...");
  delay(300);

  /* Fail-safe: nunca sair do setup sem carregar a UI principal.
   * O ecra de boot só pode ser destruído dentro de `ui_app_run` depois de `lv_scr_load`,
   * senão o LVGL fica sem ecrã activo válido e o LCD pode ficar preso no arranque. */
  (void)lvgl_port_lock(-1);
  ui_app_run(sd_ok);
  (void)lvgl_port_unlock();
  /* Ceder tempo à tarefa LVGL para o primeiro frame (evita lv_refr_now com mutex + RGB). */
  delay(10);

  net_services_start_background_task();
  if (web_remote_cfg) {
    web_remote_init();
  }

  Serial.println(String(title) + " end");
}

void loop() {
  /** Rede e FTP correm na tarefa FreeRTOS net_svc (outro core em dual-core). */
  vTaskDelay(portMAX_DELAY);
}
