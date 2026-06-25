#include "ftp_upload.h"

#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <ESP32_FTPClient.h>
#include <string>
#include <vector>

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

static const char *kCiclosRoot = "/CICLOS";
static const char *kJournalPath = "/CICLOS/.ftpjournal";

// Le o journal do SD para um std::string (vazio se nao existir).
static std::string journal_read(void) {
  std::string out;
  sd_access_sync([&]() {
    File f = SD.open(kJournalPath, FILE_READ);
    if (!f) {
      return;
    }
    while (f.available()) {
      out += (char)f.read();
    }
    f.close();
  });
  return out;
}

// Escreve o journal inteiro (temp + rename = atomico).
static void journal_write(const ftpj::JournalMap &m) {
  std::string text = ftpj::serialize_journal(m);
  sd_access_sync([&]() {
    const char *tmp = "/CICLOS/.ftpjournal.tmp";
    SD.remove(tmp);
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) {
      return;
    }
    f.write((const uint8_t *)text.data(), text.size());
    f.close();
    SD.remove(kJournalPath);
    SD.rename(tmp, kJournalPath);
  });
  sd_access_notify_changed();
}

struct ScanEntry {
  std::string relpath;  // relativo a /CICLOS, com '/'
  long size;
};

// Lista recursiva de /CICLOS. relbase comeca "" e cresce "2026", "2026/06".
static void scan_dir(const std::string &relbase, std::vector<ScanEntry> &out) {
  std::string vfs = std::string(kCiclosRoot);
  if (!relbase.empty()) {
    vfs += "/" + relbase;
  }
  std::vector<ScanEntry> files;       // ficheiros neste nivel
  std::vector<std::string> subdirs;   // subdirectorios a descer
  sd_access_sync([&]() {
    File dir = SD.open(vfs.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) {
        dir.close();
      }
      return;
    }
    File e = dir.openNextFile();
    while (e) {
      const char *name = e.name();  // pode vir como basename ou caminho completo
      std::string base = name ? name : "";
      size_t slash = base.find_last_of('/');
      if (slash != std::string::npos) {
        base = base.substr(slash + 1);
      }
      std::string rel = relbase.empty() ? base : (relbase + "/" + base);
      if (e.isDirectory()) {
        subdirs.push_back(rel);
      } else if (rel != ".ftpjournal" && rel != ".ftpjournal.tmp") {
        ScanEntry se;
        se.relpath = rel;
        se.size = (long)e.size();
        files.push_back(se);
      }
      e = dir.openNextFile();
    }
    dir.close();
  });
  for (auto &f : files) {
    out.push_back(f);
  }
  for (auto &d : subdirs) {
    scan_dir(d, out);  // a recursao reentra em sd_access_sync (inline se ja em sd_io)
  }
}

static void ftp_sync_pass(void) {
  String host = app_settings_ftp_up_host();
  if (host.length() == 0) {
    app_log_feature_write("WARN", "FTPUP", "Host nao configurado.");
    return;
  }
  String user = app_settings_ftp_up_user();
  String pass = app_settings_ftp_up_pass();
  String rdir = app_settings_ftp_up_remote_dir();
  uint16_t port = app_settings_ftp_up_port();

  // Buffers mutaveis (a lib pede char*).
  char hbuf[128], ubuf[32], pbuf[32];
  strncpy(hbuf, host.c_str(), sizeof(hbuf) - 1); hbuf[sizeof(hbuf) - 1] = 0;
  strncpy(ubuf, user.c_str(), sizeof(ubuf) - 1); ubuf[sizeof(ubuf) - 1] = 0;
  strncpy(pbuf, pass.c_str(), sizeof(pbuf) - 1); pbuf[sizeof(pbuf) - 1] = 0;

  ESP32_FTPClient ftp(hbuf, port, ubuf, pbuf, 15000, 1);
  ftp.OpenConnection();
  if (!ftp.isConnected()) {
    app_log_feature_write("WARN", "FTPUP", "Ligacao FTP falhou.");
    return;
  }

  // 1) Carrega journal.
  ftpj::JournalMap journal = ftpj::parse_journal(journal_read());

  // 2) Varre /CICLOS.
  std::vector<ScanEntry> entries;
  scan_dir("", entries);

  std::string base = rdir.c_str();
  std::vector<std::string> made_dirs;  // cache de MKD nesta passagem
  bool journal_dirty = false;
  int uploaded = 0, verified = 0, failed = 0;

  // 3) Upload dos pendentes.
  for (auto &e : entries) {
    if (!ftpj::needs_upload(journal, e.relpath, e.size)) {
      continue;
    }
    // Garante directorios remotos.
    auto dirs = ftpj::remote_dir_components(base, e.relpath);
    for (auto &d : dirs) {
      bool already = false;
      for (auto &m : made_dirs) {
        if (m == d) { already = true; break; }
      }
      if (!already) {
        ftp.MakeDir(d.c_str());  // ignora erro "ja existe"
        made_dirs.push_back(d);
      }
    }

    std::string rp = ftpj::remote_path(base, e.relpath);
    std::string vfs = std::string(kCiclosRoot) + "/" + e.relpath;

    long sent = ftp_stream_file(ftp, vfs.c_str(), rp.c_str());
    if (sent < 0) {
      app_log_feature_write("WARN", "FTPUP", "Erro a ler ficheiro do SD.");
      failed++;
      continue;
    }
    uploaded++;

    // 4) Verificacao: SIZE remoto == bytes enviados == tamanho local.
    long rsize = ftp_remote_size(ftp, rp.c_str());
    if (sent == e.size && rsize == sent) {
      journal[e.relpath] = e.size;  // commit (em RAM)
      journal_dirty = true;
      verified++;
    } else {
      char msg[160];
      snprintf(msg, sizeof(msg),
               "Verif. falhou %s: enviado=%ld local=%ld remoto=%ld",
               e.relpath.c_str(), sent, e.size, rsize);
      app_log_feature_write("WARN", "FTPUP", msg);
      failed++;
    }
  }

  ftp.CloseConnection();

  // 5) Flush do journal uma vez por passagem.
  if (journal_dirty) {
    journal_write(journal);
  }

  char done[128];
  snprintf(done, sizeof(done), "Passagem: %d enviados, %d verificados, %d falhas.",
           uploaded, verified, failed);
  app_log_feature_write("INFO", "FTPUP", done);
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
    ftp_sync_pass();
  }
}
