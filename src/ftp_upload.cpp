#include "ftp_upload.h"

#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <ESP32_FTPClient.h>

#include "app_settings.h"
#include "app_log.h"
#include "ftp_journal_core.h"
#include "sd_access.h"

static TaskHandle_t s_task = nullptr;

void ftp_upload_request_now(void) {
  if (s_task != nullptr) {
    xTaskNotifyGive(s_task);
  }
}

static void ftp_upload_task(void *arg);

void ftp_upload_init(void) {
  if (s_task != nullptr) {
    return;
  }
  // 6 KB stack: WiFiClient + buffers do ESP32_FTPClient + chunk de 2 KB.
  xTaskCreatePinnedToCore(ftp_upload_task, "ftp_up", 6144, nullptr, 1, &s_task, 1);
}

static void ftp_upload_task(void *arg) {
  (void)arg;
  for (;;) {
    uint32_t iv = app_settings_ftp_up_interval_s();
    // Acorda no intervalo OU quando ftp_upload_request_now() notifica.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS((uint32_t)iv * 1000U));
    if (!app_settings_ftp_up_enabled()) {
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      app_log_feature_write("INFO", "FTPUP", "Sem Wi-Fi; passagem adiada.");
      continue;
    }
    // sync_pass() preenchido na Task C5.
  }
}
