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

#include <esp_wifi.h>

#include <FtpServer.h>

#include <string.h>

#include "sd_access.h"

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

static void net_wifi_register_events(void);

void net_wifi_begin(const char *ssid, const char *pass) {

  /* Eventos antes de begin: a primeira ligacao STA pode ocorrer antes de net_services_start_background_task(). */
  net_wifi_register_events();

  /* Antes de WiFi.mode/begin — nome no router e pedidos DHCP (doc Arduino ESP32 Wi-Fi API). */
  WiFi.setHostname("fitadigital");

  WiFi.mode(WIFI_STA);

  /* Mantem o Wi-Fi sem power save: latencia e menos quedas de TCP/HTTP intermitentes. */
  WiFi.setSleep(false);
  (void)esp_wifi_set_ps(WIFI_PS_NONE);

  WiFi.setAutoReconnect(true);

  /*
   * disconnect(true) desliga a interface STA de forma assincrona (transicao para STOP).
   * WiFi.begin() imediato pode coincidir com esse estado e falhar com
   * ESP_ERR_WIFI_STOP_STATE (12308): "netstack cb reg failed".
   * So' desassociar da rede (false): o radio mantem-se pronto para o novo begin().
   */
  WiFi.disconnect(false /* wifioff */, false /* enableAP */);

  delay(150);

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

  s_ftp.setCallback([](FtpOperation op, unsigned int, unsigned int) {
    if (op == FTP_FREE_SPACE_CHANGE) {
      sd_access_notify_changed();
    }
  });
  s_ftp.setTransferCallback([](FtpTransferOperation op, const char *, unsigned int) {
    if (op == FTP_TRANSFER_STOP || op == FTP_UPLOAD_STOP) {
      sd_access_notify_changed();
    }
  });

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

/**
 * Intervalo de polling quando nenhum cliente FTP esta conectado.
 * Apenas verifica ftpServer.accept(); custo desprezivel, mas reduz chamadas de ~500/s para ~10/s.
 */
static constexpr uint32_t kFtpIdlePollIntervalMs = 100U;

/** Mascara para extrair cmdStage dos 3 bits inferiores do retorno de handleFTP(). */
static constexpr uint8_t kFtpCmdStageMask = 0x07U;

void net_services_sd_worker_tick(void) {

  if (WiFi.status() != WL_CONNECTED || s_ftp_suspended) {
    return;
  }

  ftp_try_start();

  if (!s_ftp_running) {
    return;
  }

  /*
   * Polling adaptativo: handleFTP() retorna cmdStage | (transferStage << 3) | (dataConn << 6).
   * FTP_Client (2) = idle, aguardando conexao TCP. Nesse estado so' faz accept() e retorna.
   * Qualquer valor > FTP_Client indica cliente conectado ou transferencia ativa.
   */
  static uint32_t s_ftp_last_idle_poll_ms = 0;
  static bool s_ftp_client_active = false;

  if (!s_ftp_client_active) {
    const uint32_t now = millis();
    if ((now - s_ftp_last_idle_poll_ms) < kFtpIdlePollIntervalMs) {
      return;
    }
    s_ftp_last_idle_poll_ms = now;
  }

  const uint8_t state = s_ftp.handleFTP();
  s_ftp_client_active = (state & kFtpCmdStageMask) > FTP_Client;

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

      sd_access_sync([] { ftp_stop(); });

    }

  }

  s_wifi_was_ok = wifi_ok;



  if (!wifi_ok) {

    return;

  }



  net_time_loop();

  if (s_ftp_suspended) {
    if (s_ftp_running) {
      sd_access_sync([] { ftp_stop(); });
    }
    return;
  }

}



void net_services_ftp_restart(void) {

  sd_access_sync([] { ftp_stop(); });

}

void net_services_set_ftp_suspended(bool suspended) {
  s_ftp_suspended = suspended;
  if (s_ftp_suspended) {
    sd_access_sync([] { ftp_stop(); });
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

static bool s_wifi_event_registered = false;

/** Uma vez por boot: mantem referencia extra ao radio (RF activo); ver esp_wifi_force_wakeup_acquire no IDF. */
static bool s_wifi_force_wakeup_acquired = false;

/** Regista uma vez: motivo de desligacao STA; forca radio activo (equipamento em rede continua). */
static void net_wifi_register_events(void)
{
  if (s_wifi_event_registered) {
    return;
  }
  s_wifi_event_registered = true;
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
      if (!s_wifi_force_wakeup_acquired) {
        /* IDF: Wi-Fi keep active state (RF opened). Complementa WIFI_PS_NONE para nao adormecer o modem. */
        if (esp_wifi_force_wakeup_acquire() == ESP_OK) {
          s_wifi_force_wakeup_acquired = true;
        }
      }
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("[WIFI] STA desligado reason=%d (ver wifi_err_reason_t no IDF)\n",
                    (int)info.wifi_sta_disconnected.reason);
    }
  });
}

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
  net_wifi_register_events();
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
