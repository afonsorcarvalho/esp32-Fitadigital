# TODO — FitaDigital (ESP32-S3-Touch-LCD-4.3B)

## Em curso

## Pendente
- **WireGuard regressões v1.73** — investigar próx sessão:
  - SD log silencia pos-preemptive #1 (hipótese: `app_log_feature_writef` dentro `wg_apply_locked` causa deadlock SD mutex). Fix: mover log p/ depois soltar `s_apply_mtx`.
  - Server-side handshake fixou no #1, preemptive #2+ não refresca. Task `wg_keepalive` provavelmente bloqueada no log SD. Mesmo root cause.
  - RS485 captura inalterada (writes diretos `/CICLOS`).
- **WireGuard root cause lib** — bug noise-handshake state corruption pos-1o-handshake em `smartalock/wireguard-lwip` upstream. Diff trombik/ciniml: cosmético. Forensic byte-diff INIT #1 vs #2 + patch lib OU swap embeddable-wg-library (lwIP adapter custom). Aceitar bypass app-layer pode ser suficiente.
- **MQTT — Fase 3: cliente real** — adicionar `bertmelis/espMqttClient` lib_deps, implementar task `mqtt_svc` (core 0, prio 1, 4KB stack), LWT, backoff exponencial, telemetria JSON periódica.
- **MQTT — Fase 4: keyword detector** — implementar task `mqtt_kw` (core 1, prio 1, 3KB stack), tick 5s, leitura offset SD, match `strcasestr`, publish `/keyword`.
- **MQTT — Fase 5: testes** — soak 30 min Mosquitto local, validar `boot_count`/`heap_guard_reboots` no JSON, LWT, heap drain <50 B/min.
- Organizar `SoftwareQualification_*.docx` (3 versoes untracked na raiz): mover para pasta dedicada ou adicionar ao `.gitignore`.

## Feito
- 2026-05-14 — **WireGuard v1.73 — bypass app-layer + zero-reboot**: Diagnóstico Fase 0 confirmou bug lib `smartalock/wireguard-lwip` upstream (server rejeita TODA INIT pos-1o-handshake como `Invalid handshake initiation`, MAC1/static-decrypt fail). Mitigação:
  - REMOVIDO `esp_restart()` escalation `wg_keepalive_task` — constraint produção (dispositivo registrador ciclos esterilização RS485, reboot = perda mensagem catastrófica clinicamente).
  - ADICIONADO preemptive re-apply a cada 100s (`s_last_apply_ms` tracker + `preemptive_reapply_ms` const). Força `end()+begin()` ANTES lib disparar broken rekey path (t≥180s `REJECT_AFTER_TIME`).
  - Bump `FITADIGITAL_VERSION` 1.72→1.73. Build+flash COM3 sucesso (170.37s): RAM 38.3% (125588 B), Flash 37.7% (2471409 B). Firmware archived `FitaDigital_v1.73.bin`.
  - Validação: boot_count 378 estável 16min+ uptime. ZERO reboots. Preemptive #1 fired+confirmed SD log. Regressões conhecidas: SD log silencia pos-preemptive #1, server handshake fixou #1 (deadlock SD mutex hipótese). RS485 captura inalterada.
- 2026-05-13 — **WireGuard race condition fix + boot banner v1.70**: lib WireGuard.cpp split end() into end_phase1 + 100ms drain + end_phase2 (fix ciniml/WireGuard-ESP32-Arduino issue #51); net_wireguard.cpp keepalive task logs mirror to Serial.printf "[WG-KA]" for COM3 observability; app.cpp boot banner "[BOOT] FitaDigital firmware v%s"; platformio.ini FITADIGITAL_VERSION 1.69→1.70. Build+flash COM3 sucesso (173.54s): RAM 38.3% (125572 B), Flash 37.7% (2473601 B). Firmware archived FitaDigital_v1.70.bin. Zero warnings from WireGuard.cpp/app.cpp (netif functions resolution clean via wireguardif.h). Pronto p/ crash-analyzer soak.
- 2026-05-12 — **WireGuard watchdog interval fix v1.62**: v1.60/v1.61 watchdog re-apply task interval 300s → stale handshake 16min (server endpoint mapping expired before next re-apply window). v1.62: reduce interval 300s→90s (constraint: WG REJECT_AFTER_TIME=180s, need fresh handshake before timeout). Build+flash COM3 sucesso (172.33s): RAM 38.3% (125588 B), Flash 37.7% (2473289 B). Firmware archived FitaDigital_v1.62.bin. Pronto p/ soak validação 17min+ zero reboots.
- 2026-05-12 — **WireGuard watchdog heap leak fix v1.60**: `wg_watchdog_task` v1.59 criava nova sessão `esp_ping` a cada 30s sem `esp_ping_delete_session` (callbacks vazia, `on_ping_end` nunca disparava) → dreno ~3KB/ciclo → HEAP_GUARD `esp_restart()` ~100s uptime. Refactor v1.60: UMA sessão `esp_ping` persistente (count=0 loop infinito, interval_ms=30000) criada once e reutilizada; parada/recriada só quando IP muda ou WiFi cai. Build+flash COM3 sucesso (167.66s): RAM 38.3% (125588 B), Flash 37.7% (2473289 B). Firmware archived FitaDigital_v1.60.bin. Pronto p/ soak.
- 2026-05-09 — **Soak v1.45 rotation desactivada**: v1.44 stack-only (3K stack) travou sd_io task sob snapshot loop. Bypass: comentar `rotate_dir_keep_last()` em screenshot_worker. Root cause pendente — hipóteses: stack overflow sd_io worker, SD.remove block, qsort recursion. Solução provável: PSRAM-allocated buffer no screenshot_init.
