# TODO — FitaDigital v2.x.x

Main HEAD `<pending>` (v2.1.2). Worktree v2 merged + apagado.
Scope: WiFi + HTTP + RS485 + NTP + FTP server. Sem WG, sem MQTT, sem SCREENSHOT.

## *** v2.1.2 PRODUCTION-READY *** (validado 2026-05-19 soak 8h)

Soak 8h cycle+stress: 294 cycles, 1470/1470 list hits (100%), avg_t 1.50s,
max_t 4.21s, NDJSON linear 6.7KB->63KB, active_seen 293/294, idle_seen
294/294. Serial COM3 8h: zero panic, zero Guru. Device pos-soak:
enabled=true IDLE prev_reset=UNKNOWN(0).



Tag `v2.0.0` production. v2.0.1 NTP. v2.1.0 cycle_detector. v2.1.1 /list crash fix.
v2.1.2 SMP yield crash fix + single-flight mutex + watchdog RTC baseline.
Archive: `firmware_versions/FitaDigital_v2.12.bin` (2.4 MB).

### Soak 20min v2.1.2 PASS (2026-05-19 01:39-01:59)
- 21 cycles enviados, list_200 **105/105 (100%)**, errors=0
- avg_t **0.71s** (vs 2.23s v2.11), max_t **1.18s** (vs 8.06s)
- active_seen 21/21, idle_seen 21/21 (state machine 100%)
- NDJSON growth linear +192B/cycle, sem plateau
- prev_reset=UNKNOWN(0) — ZERO reboots, ZERO panics

### Root cause backtrace v2.1.1 (decodificado)
PC=0x403828fe EXCCAUSE=0x01 IllegalInstruction em xPortEnterCriticalTimeout.
Call stack: reader_task -> cycle_detector_process_line -> emit_cycle_close
(line 161 `app_log_feature_writef("CYCLE","fechado")`) -> app_log_write ->
log_notify_cb (web_portal.cpp:92 xQueueSend) -> xQueueGenericSend ->
xTaskRemoveFromEventList -> prvCheckForYieldUsingPrioritySMP ->
vPortYieldOtherCore -> esp_crosscore_int_send -> vPortEnterCritical -> CRASH.

SMP cross-core yield IPI hit FreeRTOS portMUX race quando /api/cycles/list
HTTP task em outro core competia simultaneamente. Stack OK (16KB), heap OK,
queue OK — bug FreeRTOS interno sob alta contencao.

### Fixes v2.1.2 aplicados
1. **Remove log enqueue de dentro de reader_task** (cycle_detector.cpp:
   emit_cycle_close, process_line, tick). NDJSON + /api/cycles/status sao
   suficientes para observabilidade — log redundante.
2. **Single-flight mutex `/api/cycles/list`** (handle_cycles_list_get):
   2o req concurrente -> 503 imediato. Onership do mutex passa ao
   onDisconnect lambda em sucesso, release explicito em error paths.
3. **Watchdog cycle_detector RTC baseline** (cycle_detector.cpp):
   `RTC_NOINIT_ATTR ConfigBaseline s_baseline` + magic 0xC0DE2026.
   cycle_detector_tick detecta `s_cfg.enabled==false` + magic OK e
   restaura baseline (auto-heal contra BSS corruption futura).
4. **Panic breadcrumb** em handle_cycles_list_get enter/exit
   (panic_breadcrumb_set/clear via panic_logger.h). Next-boot dump
   identifica crash dentro do handler.

## *** v2.2.0 SHIPPED *** (validado 2026-05-20 soak 30min)

Cycle detector configuravel via portal web + NVS persist. Tab "Ciclo"
no portal: start_pattern / end_pattern / idle_timeout_s editaveis.
Endpoint `/api/cycles/config` GET/POST (URL movido de /api/settings/cycle
por bug ESPAsyncWebServer GET prefix-match). Live reconfigure fecha
ciclo ACTIVE como INTERRUPTED antes de re-init.
Archive: `firmware_versions/FitaDigital_v2.20.bin` (2.4 MB).

### Soak 30min v2.2.0 PASS (2026-05-19 19:34-20:04)
- 23 cycles, list_200 115/115 (100%), errors=0
- active_seen 23/23, idle_seen 23/23
- NDJSON linear, COM3 serial zero panic
- prev_reset=UNKNOWN(0) — zero reboots
- Smoke custom patterns OK (INICIO_CICLO/FIM_CICLO detectados + NDJSON)
- NVS persistence cross-reboot validado
- tools/test_cycle_config.py 6 tests PASS

## Em curso
(nada — aguardar decisao proxima feature)

## Pendente

### Sugestoes v2.2.x+
- **UI events**: badge ciclo concluido, lista de ciclos no portal
- **Pub/sub formal cycle_detector**: API observadores para outros modulos
- **Watchdog edge case** (review v2.2.0): se config set a start vazio
  (enabled=false intencional), `watchdog_heal_locked` pode restaurar
  baseline antigo non-empty e re-activar silenciosamente. Low risk.

### FTP client (ULTIMA feature)
- Push delta para servidor remoto. So' depois resto estavel.
- Decidir lib (ESP32FtpClient, SimpleFTPClient, lwip socket directo)
- Configuravel via UI (server IP, user, password, remote path, intervalo)

### Mitigacao NTP DNS
- Device loga "falhou (NTP timeout 10s)" quando `pool.ntp.org` DNS partido
- Workaround: setar NTP IP literal (ex `200.160.0.8` a.ntp.br) na UI Configuracoes
- Long term: fallback chain pools/IPs hardcoded

## Feito
- **2026-05-18 — v2.1.0 cycle_detector + NDJSON index** (commit `3441208` push origin):
  - State machine ACTIVE/IDLE com timeout 900s default
  - `/api/cycles/status` + `/api/cycles/list`
  - NDJSON `/CICLOS/AAAA/MM/cycles.ndjson`
- **2026-05-18 — v2.0.1 NTP honest sync** (commit `42d6f75`):
  - `sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED` em vez de getLocalTime
  - Retry NTP a cada 1h em `net_time_loop`
- **2026-05-18 — `tools/soak_run.py --warmup 30` default** (commit `7477d04`):
  - Elimina false-fail startup race (WinError 10061 antes WiFi+HTTP up)
- **2026-05-18 — docs/manuais USUARIO+INSTALACAO+SERVICO** v2.0.0 (commit `f48d921`)
- **2026-05-18 — v2.0.0 PRODUCTION-READY validacao soak 8h + RS485 stress** (commit `644d0f3`):
  - Soak 28800s real (8h) zero reboot zero crash
  - Heap int 42748 -> 45036 (+2.3KB liberado healthy)
  - Heap min seen: 36540 B (massive margin vs HEAP_GUARD 5K)
  - PSRAM drift -28KB em 8h (~3.5KB/h, nao critico)
  - FTP probe LIST 468/468 OK (100%)
  - Health probe HTTP 476/477 (1 false-fail startup race conhecido)
  - **RS485 stress 100%**: PC TX 958 bursts (OPERACAO + 47ch random = 55 chars) cada 30s via COM8 9600; SD captou 958/958 OPERACAO em `/CICLOS/2026/05/20260518.txt` (81322 B)
  - Live post-soak uptime 37464s = 10.4h continuous
  - Archive: `firmware_versions/FitaDigital_v2.00.bin` (2398784 B)
  - Tag: `v2.0.0`
- **2026-05-17 — Worktree v2 baseline 2.0.0** (commit `4325738`):
  - build_features.h: MQTT=0, SCREENSHOT=0, WG=0 herdado
  - Worktree apagado pos-merge
