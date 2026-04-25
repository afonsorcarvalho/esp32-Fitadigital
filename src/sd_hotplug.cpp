/**
 * @file sd_hotplug.cpp
 * @brief Detecao de insercao/remocao do SD card por polling (sem pino CD).
 *
 * Executa como ticker no contexto da tarefa sd_io (exclusivo para I/O SD).
 * Intervalo efectivo: kProbeIntervalMs (2000 ms) — verificado a cada tick
 * (~2 ms) via comparacao de millis().
 *
 * Fluxo de deteccao de insercao (s_mounted == false):
 *   - Tenta mount_sd_fallback() a 400 kHz (timeout rapido em cartao ausente).
 *   - Se bem-sucedido: seta mounted=true, invoca callback on_inserted (se registado),
 *     notifica changed, loga.
 *
 * Fluxo de deteccao de remocao (s_mounted == true):
 *   - Probe ativo (f_opendir/f_readdir) falha N vezes consecutivas.
 *   - seta mounted=false PRIMEIRO (protege leitores concorrentes), depois SD.end().
 *
 * Nota: o ticker corre no contexto sd_io, portanto pode chamar SD.* directamente
 * (mutex FatFs ja garantido pela fila exclusiva da tarefa).
 */

#include "sd_hotplug.h"
#include "sd_access.h"
#include "sd_mount.h"
#include "app_log.h"

#include <Arduino.h>
#include <SD.h>
#include "ff.h"
#include <string.h>

namespace {

static constexpr uint32_t kProbeIntervalMs = 1000U;
/** Debounce: numero de probes-fail consecutivos para considerar remocao real.
 * Filtra glitches de SPI/FatFs ocasionais (1 falha pontual nao desmonta).
 * 3 × 1000ms = 3 s ate detectar remocao real. */
static constexpr uint8_t kFailsToConfirmRemove = 3U;
static uint32_t s_last_probe_ms = 0U;
static uint8_t s_consecutive_fails = 0U;

/** Callback opcional invocado apos mount sucesso na insercao. */
static sd_hotplug_event_fn s_on_inserted_fn = nullptr;

/**
 * Probe ATIVO da presenca do cartao: f_opendir + f_readdir(1 entry) na raiz forca
 * I/O real via SPI ate ao cartao (nao confia no estado cacheado de SD.cardType,
 * que so e' actualizado em SD.begin). Retorna true se cartao responde a I/O.
 *
 * f_stat("0:/") nao serve: raiz nao e' uma entry indexavel.
 * f_opendir + f_readdir e' valido em qualquer FAT volume montado.
 */
static bool sd_probe_alive(void) {
  FF_DIR dir;
  memset(&dir, 0, sizeof(dir));
  FRESULT fr = f_opendir(&dir, "0:/");
  if (fr != FR_OK) {
    Serial.printf("[sd_hotplug] probe opendir fail fr=%d\n", (int)fr);
    return false;
  }
  /* Le 1 entry (forca I/O ao cartao). Aceita FR_OK mesmo que dir fique vazia
   * (fno.fname[0] == 0): isto significa volume montado mas sem ficheiros. */
  FILINFO fno;
  memset(&fno, 0, sizeof(fno));
  fr = f_readdir(&dir, &fno);
  f_closedir(&dir);
  if (fr != FR_OK) {
    Serial.printf("[sd_hotplug] probe readdir fail fr=%d\n", (int)fr);
    return false;
  }
  return true;
}

static void sd_hotplug_tick(void) {
  const uint32_t now = millis();
  if ((now - s_last_probe_ms) < kProbeIntervalMs) {
    return;
  }
  s_last_probe_ms = now;

  const bool currently_mounted = sd_access_is_mounted();

  if (currently_mounted) {
    /* Probe ativo: I/O real via FatFs. SD.cardType() nao deteta remocao fisica. */
    if (sd_probe_alive()) {
      s_consecutive_fails = 0U;
    } else {
      if (s_consecutive_fails < 0xFFU) {
        s_consecutive_fails++;
      }
      if (s_consecutive_fails >= kFailsToConfirmRemove) {
        /* Flipar flag ANTES de SD.end(): leitores concorrentes que verificam
         * is_mounted() ficam imediatamente protegidos sem esperar SD.end(). */
        sd_access_set_mounted(false);
        SD.end();
        sd_access_notify_changed();
        s_consecutive_fails = 0U;
        Serial.println("[sd_hotplug] removed");
        app_log_write("WARN", "[sd_hotplug] removed");
      }
    }
  } else {
    s_consecutive_fails = 0U;
    /* Sem cartao montado — tenta montar (400 kHz para fail rapido).
     * Nao chamar SD.end() aqui: mount_sd_fallback ja o faz internamente entre
     * tentativas; chamar em loop sem cartao cria mutex churn desnecessario. */
    if (mount_sd_fallback()) {
      sd_access_set_mounted(true);
      /* Invocar callback antes do log "inserted" (ainda em contexto sd_io). */
      if (s_on_inserted_fn != nullptr) {
        s_on_inserted_fn();
      }
      sd_access_notify_changed();
      Serial.println("[sd_hotplug] inserted");
      app_log_write("INFO", "[sd_hotplug] inserted");
    }
  }
}

} // namespace

void sd_hotplug_set_on_inserted(sd_hotplug_event_fn fn) {
  s_on_inserted_fn = fn;
}

void sd_hotplug_init(void) {
  s_last_probe_ms = millis();
  sd_access_register_tick(sd_hotplug_tick);
  Serial.println("[sd_hotplug] init ok — polling a cada 1000 ms (probe ativo via f_stat)");
}
