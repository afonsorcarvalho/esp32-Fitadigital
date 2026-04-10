/**

 * @file net_services.cpp

 * @brief WiFi + FTP + NTP (net_time) + WireGuard; STORAGE_SD em platformio.

 */

#include "net_services.h"

#include "app_log.h"
#include "app_settings.h"

#include "net_time.h"

#include "net_wireguard.h"

#include <SD.h>

#include <WiFi.h>

#include <FtpServer.h>

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static FtpServer s_ftp;

static bool s_ftp_running = false;
static bool s_ftp_suspended = false;
static bool s_ftp_missing_sd_logged = false;
static uint32_t s_ftp_last_probe_ms = 0;
static uint8_t s_ftp_last_card_type = CARD_NONE;

/**
 * Evita consultar SD.cardType() em cada iteracao da task de rede (2 ms),
 * reduzindo carga no CH422G e timeouts que impactam latencia de rede.
 */
static constexpr uint32_t kFtpSdProbeIntervalMsReady = 2000U;
static constexpr uint32_t kFtpSdProbeIntervalMsMissing = 60000U;



/**

 * SimpleFTPServer guarda ponteiros para user/pass (FtpServer.cpp begin).

 * Nao usar String::c_str() temporario — o buffer e libertado e o login falha.

 */

static char s_ftp_user_buf[16];

static char s_ftp_pass_buf[16];

/**
 * Tempo minimo com Wi-Fi desligado antes de parar o FTP (histerese).
 * Evita ftp_stop() em falhas breves de associacao; ajuste em ms se necessario.
 */
static constexpr uint32_t kFtpStopWifiDownHysteresisMs = 30000U;

void net_wifi_begin(const char *ssid, const char *pass) {

  WiFi.mode(WIFI_STA);
  /* Mantem o Wi-Fi sem power save para reduzir latencia. */
  WiFi.setSleep(false);

  WiFi.disconnect(true, false);

  delay(100);

  WiFi.begin(ssid, pass);

}



void net_wifi_begin_saved(void) {

  String s = app_settings_wifi_ssid();

  String p = app_settings_wifi_pass();

  if (s.length() == 0) {

    return;

  }

  net_wifi_begin(s.c_str(), p.c_str());

}



static void ftp_try_start(void) {
  const uint32_t now_ms = millis();

  if (s_ftp_running) {
    return;

  }

  if (WiFi.status() != WL_CONNECTED) {
    return;

  }

  uint8_t card_type = s_ftp_last_card_type;
  const uint32_t probe_interval_ms =
      (s_ftp_last_card_type == CARD_NONE) ? kFtpSdProbeIntervalMsMissing : kFtpSdProbeIntervalMsReady;
  const bool should_probe_sd = ((now_ms - s_ftp_last_probe_ms) >= probe_interval_ms) || (card_type == CARD_NONE && s_ftp_last_probe_ms == 0U);
  if (should_probe_sd) {
    card_type = SD.cardType();
    s_ftp_last_probe_ms = now_ms;
    s_ftp_last_card_type = card_type;
  }

  if (card_type == CARD_NONE) {
    if (!s_ftp_missing_sd_logged) {
      s_ftp_missing_sd_logged = true;
      app_log_feature_write("WARN", "FTP", "Inicio adiado: cartao SD nao detectado.");
    }
    return;

  }
  s_ftp_missing_sd_logged = false;

  String u = app_settings_ftp_user();

  String p = app_settings_ftp_pass();

  strncpy(s_ftp_user_buf, u.c_str(), sizeof(s_ftp_user_buf) - 1U);

  s_ftp_user_buf[sizeof(s_ftp_user_buf) - 1U] = '\0';

  strncpy(s_ftp_pass_buf, p.c_str(), sizeof(s_ftp_pass_buf) - 1U);

  s_ftp_pass_buf[sizeof(s_ftp_pass_buf) - 1U] = '\0';

  s_ftp.begin(s_ftp_user_buf, s_ftp_pass_buf, "FitaDigital SD FTP");

  s_ftp_running = true;
  s_ftp_last_card_type = card_type;
  app_log_feature_writef("INFO", "FTP", "Servidor iniciado em ftp://%s/ (user=%s)",
                         WiFi.localIP().toString().c_str(), s_ftp_user_buf);

  Serial.printf("[FTP] ftp://%s/  utilizador: %s\n", WiFi.localIP().toString().c_str(), s_ftp_user_buf);
}



static void ftp_stop(void) {

  if (!s_ftp_running) {

    return;

  }

  s_ftp.end();

  s_ftp_running = false;
  app_log_feature_write("INFO", "FTP", "Servidor parado.");

}



void net_services_loop(void) {

  static bool s_modules_inited = false;

  static bool s_wifi_was_ok = false;

  /** millis() em que o Wi-Fi passou a nao estar ligado; 0 = nao a contar histerese. */
  static uint32_t s_ftp_stop_wifi_down_since_ms = 0;

  if (!s_modules_inited) {

    net_time_init();

    net_wireguard_init();

    s_modules_inited = true;

  }



  const bool wifi_ok = WiFi.status() == WL_CONNECTED;

  if (wifi_ok && !s_wifi_was_ok) {
    app_log_feature_writef("INFO", "WIFI", "Conectado. IP=%s RSSI=%d",
                           WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());

    net_time_on_wifi_connected();

    net_wireguard_apply();

  }

  if (!wifi_ok && s_wifi_was_ok) {
    app_log_feature_write("WARN", "WIFI", "Desconectado.");
    net_wireguard_init();

    s_ftp_stop_wifi_down_since_ms = millis();

  }

  if (wifi_ok) {

    s_ftp_stop_wifi_down_since_ms = 0;

  } else if (s_ftp_running && s_ftp_stop_wifi_down_since_ms != 0) {

    const uint32_t down_ms = millis() - s_ftp_stop_wifi_down_since_ms;

    if (down_ms >= kFtpStopWifiDownHysteresisMs) {

      ftp_stop();

    }

  }

  s_wifi_was_ok = wifi_ok;



  if (!wifi_ok) {

    return;

  }



  net_time_loop();

  if (s_ftp_suspended) {
    if (s_ftp_running) {
      ftp_stop();
    }
    return;
  }

  ftp_try_start();

  if (s_ftp_running) {
    s_ftp.handleFTP();
  }

}



void net_services_ftp_restart(void) {

  ftp_stop();

}

void net_services_set_ftp_suspended(bool suspended) {
  s_ftp_suspended = suspended;
  if (s_ftp_suspended) {
    ftp_stop();
  }
}

/** Prioridade semelhante à tarefa Arduino; stack generoso para FTP/callbacks Wi-Fi. */
static constexpr UBaseType_t kNetTaskPrio = 1U;
static constexpr uint32_t kNetTaskStackWords = 8192U / sizeof(StackType_t);
static constexpr uint32_t kNetTaskDelayMs = 2U;

/**
 * WiFi (Arduino/ESP-IDF) nao' e' seguro entre dois nucleos: net_svc + UI a chamar
 * WiFi.status() / WiFi.begin() em cores diferentes causa falhas (ex.: netstack cb reg 12308).
 * Por isso net_svc corre no mesmo core que o Arduino/LVGL; LVGL mantem prioridade mais alta (2 > 1).
 */
static constexpr BaseType_t kNetTaskCore = (BaseType_t)ARDUINO_RUNNING_CORE;

static TaskHandle_t s_net_task_handle = nullptr;

static void net_services_task(void * /*arg*/) {
  for (;;) {
    net_services_loop();
    vTaskDelay(pdMS_TO_TICKS(kNetTaskDelayMs));
  }
}

void net_services_start_background_task(void) {
  if (s_net_task_handle != nullptr) {
    return;
  }
  Serial.printf("[NET] Tarefa net_svc no core %d (igual ao Arduino/LVGL; WiFi so' neste core)\n",
                (int)kNetTaskCore);
  const BaseType_t ok =
      xTaskCreatePinnedToCore(net_services_task, "net_svc", kNetTaskStackWords, nullptr, kNetTaskPrio,
                              &s_net_task_handle, kNetTaskCore);
  if (ok != pdPASS) {
    Serial.println("[NET] ERRO: xTaskCreatePinnedToCore(net_svc) falhou");
    s_net_task_handle = nullptr;
  }
}
