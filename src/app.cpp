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
#include <ESPmDNS.h>

#include "lvgl_port_v8.h"
#include "app_settings.h"
#include "board_pins.h"
#include "board_i2c0_bus_lock.h"
#include "waveshare_sd_cs.h"
#include "boot_journal.h"
#include "SD.h"
#include "app_log.h"
#include "net_monitor.h"
#include "net_services.h"
#include "sd_access.h"
#include "cycles_rs485.h"
#include "rs485_buffer.h"
#include "net_time.h"
#include "ui_feedback.h"
#include "ui/boot_screen.h"
#include "ui/splash_screen.h"
#include "ui/ui_app.h"
#include "web_portal/web_portal.h"
#include "sd_mount.h"
#include "sd_hotplug.h"
#include "heap_monitor.h"
#include "net_mqtt.h"
#include "net_mqtt_keywords.h"

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

bool sd_rw_self_test(void) {
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
bool mount_sd_fallback(void) {
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
  /* Equipamento em rede eletrica continua: prioridade a desempenho; PM/tickless no sdkconfig; Wi-Fi sem PS em net_services. */
  Serial.printf("[SYS] CPU %u MHz\n", (unsigned)getCpuFrequencyMhz());
  Serial.println(String(title) + " start");
  app_settings_init();
  (void)boot_journal_init();
  (void)boot_journal_reset();
  /* SPIFFS montado por boot_journal_init — inicializar buffer RS485 agora. */
  rs485_buffer_init();

  Serial.println("Initialize panel device");
  ESP_Panel *panel = new ESP_Panel();
  panel->init();
#if LVGL_PORT_AVOID_TEAR
  ESP_PanelBus_RGB *rgb_bus = static_cast<ESP_PanelBus_RGB *>(panel->getLcd()->getBus());
  /* PCLK reduzido 16MHz -> 12MHz em v1.41: bounce *20 não chegava para evitar drift visual
   * ("rolling/tearing tipo TV CRT") sob carga MQTT publish + RS485 SD writes + LVGL render.
   * 12MHz dá ~29 fps com porches do board built-in (820x500 totais), suficiente para UI
   * dominantemente estática. *40 starvou heap interno (network tasks falharam). */
  rgb_bus->configRgbTimingFreqHz(12 * 1000 * 1000);
  rgb_bus->configRgbFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
  rgb_bus->configRgbBounceBufferSize(LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE);
#endif
  panel->begin();
  /** I2C0 partilhado: CH422G (CS TF) + RTC; touch/painel tambem usam o bus (stack Espressif). */
  board_i2c0_bus_lock_init();
  board_i2c0_bus_quiet_ioexpander_serial_logs();
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
    } else {
      boot_log_step(BOOT_STEP_SD, "ERROR", "falha apos 3 tentativas");
    }
  }
  /** A partir daqui, todo o I/O SD em segundo plano passa pela fila sd_io. */
  sd_access_set_mounted(sd_ok);
  sd_access_register_tick(net_services_sd_worker_tick);
  sd_access_start_task();
  sd_hotplug_init();
  /* Registar callback de flush do buffer SPIFFS ao reinserir SD. */
  sd_hotplug_set_on_inserted(rs485_buffer_flush_to_sd);

  if (rtc_probe_with_retries(3U)) {
    net_time_bootstrap_from_rtc_early();
  } else {
    boot_log_step(BOOT_STEP_RTC, "ERROR", "falha apos 3 tentativas");
  }

  if (sd_ok) {
    cycles_rs485_init();
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

  boot_log_step(BOOT_STEP_WEB_PORTAL, "INFO", "%s",
                "portal HTTP (config, logs, ficheiros SD) na porta 80");

  net_monitor_init();

  boot_log_plain("INFO", "Boot concluido. A carregar interface principal...");
  boot_screen_set_footer("A carregar interface principal...");
  delay(300);

  /** Um único flush SPIFFS após toda a fase de boot em RAM (ver boot_journal_flush_to_spiffs). */
  (void)boot_journal_flush_to_spiffs();
  if (sd_ok) {
    const bool copied = boot_journal_copy_to_sd();
    Serial.printf("[BOOT][%s] copia boot.log p/ SD %s\n", copied ? "INFO" : "WARN", copied ? "OK" : "adiada");
    boot_screen_set_step(BOOT_STEP_SD, copied ? "INFO" : "WARN");
  }
  /**
   * Desativado: em runtime o `boot_journal_mirror_task` causou `abort()` dentro de `SPIFFS.open()`:
   * backtrace mostrou `lock_init_generic` → `fopen` → `VFSFileImpl::VFSFileImpl` → `boot_journal_mirror_task`.
   *
   * Como o journal de boot só é escrito durante o `setup()`, a cópia única acima é suficiente e
   * evita consumo extra de heap interno (stack da task) e ciclos periódicos de I/O.
   */
  // boot_journal_start_sd_mirror_task();

  /* Splash de boot: logo da empresa após o checklist, antes da UI principal.
   * splash_screen_show() faz lv_scr_load e destrói o ecrã do checklist. */
  const uint8_t splash_secs = app_settings_splash_seconds();
  if (splash_secs > 0) {
    splash_screen_show();
    Serial.printf("[BOOT] Splash screen %u s\n", (unsigned)splash_secs);
    vTaskDelay(pdMS_TO_TICKS((uint32_t)splash_secs * 1000U));
  }

  /* Fail-safe: nunca sair do setup sem carregar a UI principal.
   * Se o splash esteve ativo, ui_app_run destrói o splash em vez do boot screen. */
  (void)lvgl_port_lock(-1);
  ui_app_run(sd_ok, splash_secs > 0);
  (void)lvgl_port_unlock();
  /** O .txt do dia so abre na UI quando chegar linha RS485 (apos timer no explorador). */
  /* Ceder tempo à tarefa LVGL para o primeiro frame (evita lv_refr_now com mutex + RGB). */
  delay(10);

  web_portal_init();
  /**
   * `net_svc` consome heap interno (stack). Se ele iniciar antes do portal, pode impedir o AsyncTCP
   * de criar a task interna (erro: "AsyncTCP begin(): failed to start task"). Por isso o portal
   * é inicializado primeiro.
   */
  net_services_start_background_task();
  net_mqtt_init();
  net_mqtt_keywords_start();

  /* Telemetria de heap + watchdog: 30s, reboot graceful se int_free < 6 KB. */
  heap_monitor_start();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[NET] IP=%s mascara=%s gateway=%s\n", WiFi.localIP().toString().c_str(),
                  WiFi.subnetMask().toString().c_str(), WiFi.gatewayIP().toString().c_str());
    Serial.printf("[NET] Portal: http://%s/  (mesma rede que este PC; confirme o IP no monitor)\n",
                  WiFi.localIP().toString().c_str());
    if (MDNS.begin("fitadigital")) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("[NET] mDNS: http://fitadigital.local/ (se o SO resolver .local)");
    } else {
      Serial.println("[NET] mDNS indisponivel; use o IP acima.");
    }
  } else {
    Serial.println("[NET] Wi-Fi STA sem ligacao — portal HTTP so' responde apos configurar Wi-Fi no ecra ou por cabo.");
  }

  Serial.println(String(title) + " end");
}

void loop() {
  /** Rede: net_svc; FTP e acesso SD: tarefa sd_io (fila unica). */
  vTaskDelay(portMAX_DELAY);
}
