# TODO — FitaDigital v2.x.x (worktree v2)

Branch `v2` parte do main `3c1b14a` (v1.95 Hibrido WG removal).
Scope reduzido. Sem WG, sem MQTT, sem SCREENSHOT inicialmente.

## Em curso
- **2.0.0 baseline** — bump version, build_features.h ajustado (WG=0, MQTT=0, SCREENSHOT=0). Aguardar build + smoke test antes de comecar features novas.

## Pendente
### Estabilizar core primeiro
- **Smoke test 2.0.0** — build, flash, soak 30min minimo. Confirmar zero reboot, heap flat, WiFi+RS485+NTP+HTTP+FTP server OK.
- **NTP fix** (herdado main, agora critico para timestamps cycles RS485): `net_time_sync_now_blocking` deve verificar `sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED` em vez de `getLocalTime` (RTC stale plausivel passa). Retry NTP a cada 1h em `net_time_loop`.
- **RS485 logico — revisao** — confirmar captura ciclos + escrita SD .txt + matching OPERACAO/CICLOS funcionam end-to-end no scope reduzido.

### FTP client (ULTIMA feature)
- **FTP client push para servidor remoto** — adicionar feature SO' depois das anteriores estaveis. Objectivo: sincronizar ficheiros nao actualizados no servidor (delta sync). Decidir lib (ESP32FtpClient, SimpleFTPClient ou implementar via lwip socket). Configuravel via UI (server IP, user, password, remote path, intervalo).

## Feito
- 2026-05-17 — Worktree v2 criado em `.claude/worktrees/v2`, branch `v2` a partir de `3c1b14a` (main HEAD v1.95 Hibrido). build_features.h: MQTT=0, SCREENSHOT=0 default (WG=0 herdado). Version bump 2.0.0.
