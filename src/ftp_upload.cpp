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

static const size_t kChunk = 2048;
static uint8_t s_chunk[kChunk];

// Le `vfs_path` em chunks via sd_io e envia por FTP. Devolve bytes enviados,
// ou -1 em erro de leitura. O File e' aberto/lido/fechado SEMPRE no contexto sd_io;
// o WriteData (rede) corre na task ftp_up entre chunks, sem bloquear o sd_io.
static long ftp_stream_file(ESP32_FTPClient &ftp, const char *vfs_path, const char *remote_path) {
  File f;
  bool open_ok = false;
  sd_access_sync([&]() {
    f = SD.open(vfs_path, FILE_READ);
    open_ok = (bool)f;
  });
  if (!open_ok) {
    return -1;
  }

  ftp.InitFile("Type I");
  ftp.NewFile(remote_path);  // STOR

  long sent = 0;
  for (;;) {
    int n = 0;
    sd_access_sync([&]() {
      n = (int)f.read(s_chunk, kChunk);
    });
    if (n <= 0) {
      break;
    }
    ftp.WriteData(s_chunk, n);
    sent += n;
  }
  ftp.CloseFile();

  sd_access_sync([&]() {
    f.close();
  });
  return sent;
}

// Emite "SIZE <path>" no socket de controlo e devolve o tamanho remoto, ou -1.
// O servidor responde "213 <n>". Em binario (Type I) o SIZE e' fiavel.
static long ftp_remote_size(ESP32_FTPClient &ftp, const char *remote_path) {
  char cmd[160];
  snprintf(cmd, sizeof(cmd), "SIZE %s\r\n", remote_path);
  ftp.Write(cmd);

  char resp[64] = {0};
  ftp.GetFTPAnswer(resp, 0);
  // Procura "213" e o numero que se segue.
  char *p = strstr(resp, "213");
  if (p == nullptr) {
    return -1;
  }
  p += 3;
  while (*p == ' ') {
    p++;
  }
  if (*p < '0' || *p > '9') {
    return -1;
  }
  return strtol(p, nullptr, 10);
}

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
