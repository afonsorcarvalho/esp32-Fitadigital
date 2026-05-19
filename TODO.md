# TODO — FitaDigital v2.x.x

Main HEAD `3441208` (v2.1.0). Worktree v2 merged + apagado.
Scope: WiFi + HTTP + RS485 + NTP + FTP server. Sem WG, sem MQTT, sem SCREENSHOT.

## *** v2.1.0 SHIPPED *** (push origin 2026-05-18)

Tag `v2.0.0` production-ready. v2.0.1 NTP fix, v2.1.0 cycle_detector.
Archive: `firmware_versions/FitaDigital_v2.00.bin` (2.4 MB).

## Em curso
(nada — aguardar decisao proxima feature)

## Pendente

### Smoke E2E cycle_detector (BLOCKED — COM8 USB-RS485 caido)
- Replug fisico COM8 primeiro
- TX via COM8 9600: `OPERACAO ...` + linhas + `FIM CICLO ...`
- Verificar `/api/cycles/status` flicka ACTIVE -> IDLE
- GET `/api/cycles/list?year=2026&month=05` confirma NDJSON entry status=DONE

### Sugestoes v2.2.0+
- **UI events**: badge ciclo concluido, lista de ciclos no portal
- **Parser robustez**: NVS patterns configuraveis via UI (start/end/idle_timeout)
  - Defaults hardcoded actuais: start="OPERACAO", end="FIM CICLO", idle=900s
- **Pub/sub formal cycle_detector**: API observadores para outros modulos

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
