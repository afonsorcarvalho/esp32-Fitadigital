# TODO — FitaDigital v2.x.x (worktree v2)

Branch `v2` parte do main `3c1b14a` (v1.95 Hibrido WG removal).
Scope reduzido. Sem WG, sem MQTT, sem SCREENSHOT inicialmente.

## *** v2.0.0 = PRODUCTION-READY *** (validado 2026-05-18)

Firmware estavel para deploy producao. Archive:
`firmware_versions/FitaDigital_v2.00.bin` (2.4 MB).
Soak 8h overnight + RS485 stress 100% hit rate (958/958 OPERACAO).
Detalhes em "Feito".

## Em curso
(nada — aguardar decisao proxima feature)

## Pendente
### Proximas features (depois v2.0.0 producao estabilizada)
- **NTP fix** (herdado main, critico para timestamps cycles RS485): `net_time_sync_now_blocking` deve verificar `sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED` em vez de `getLocalTime` (RTC stale plausivel passa). Retry NTP a cada 1h em `net_time_loop`. Sera v2.0.1 ou v2.1.0.
- **RS485 logico revisao** — confirmar matching OPERACAO/CICLOS no firmware (parser, eventos) com nova base v2. Stress 8h ja' confirma pipeline captura/SD OK.
- **soak_run.py warmup fix** — adicionar grace period 30s antes 1o probe para eliminar false-fail startup race (`WinError 10061 connection refused` antes WiFi+HTTP up). 30min e 8h soaks reportaram FAIL por 1 sample falhado neste momento.

### FTP client (ULTIMA feature)
- **FTP client push para servidor remoto** — adicionar feature SO' depois das anteriores estaveis. Objectivo: sincronizar ficheiros nao actualizados no servidor (delta sync). Decidir lib (ESP32FtpClient, SimpleFTPClient ou implementar via lwip socket). Configuravel via UI (server IP, user, password, remote path, intervalo).

## Feito
- **2026-05-18 — v2.0.0 PRODUCTION-READY validacao soak 8h + RS485 stress**:
  - Soak 28800s real (8h) zero reboot zero crash
  - Heap int 42748 -> 45036 (+2.3KB liberado healthy)
  - Heap min seen: 36540 B (massive margin vs HEAP_GUARD 5K)
  - PSRAM drift -28KB em 8h (~3.5KB/h, nao critico)
  - FTP probe LIST 468/468 OK (100%)
  - Health probe HTTP 476/477 (1 false-fail startup race conhecido)
  - **RS485 stress end-to-end 100%**: PC TX 958 bursts (OPERACAO + 47ch random = 55 chars) a cada 30s via COM8 9600 baud; SD captou 958/958 OPERACAO em `/CICLOS/2026/05/20260518.txt` (81322 B). ZERO perda.
  - Live post-soak uptime 37464s = 10.4h continuous
  - Archive: `firmware_versions/FitaDigital_v2.00.bin` (2398784 B = 2.4 MB)
  - Git: branch v2 HEAD `4325738` (commit baseline 2.0.0)
  - Tool used: `tools/rs485_soak_gen.py` patched com `--keyword OPERACAO` (commit pendente v2)
- 2026-05-17 — Worktree v2 criado em `.claude/worktrees/v2`, branch `v2` a partir de `3c1b14a` (main HEAD v1.95 Hibrido). build_features.h: MQTT=0, SCREENSHOT=0 default (WG=0 herdado). Version bump 2.0.0.
