# TODO — FitaDigital (ESP32-S3-Touch-LCD-4.3B)

## Em curso

## Pendente
- **WireGuard soak v1.76** — validar 8h+ overnight: zero-reboot + heap flat (heap_min caiu ~10KB em 6min monitor, confirmar não é drift). Stop server capture `~/wg-diag/` (tcpdump + wgshow loop a correr desde Fase 0).
- **WireGuard root cause lib** (low priority — workaround v1.76 suficiente) — bug noise-handshake state corruption pos-1o-handshake em `smartalock/wireguard-lwip` upstream. Forensic byte-diff INIT #1 vs #2 informa se vale fix lib. `/api/wg/ping` reformular (esp_ping crash → lwip raw_pcb ou remover).
- **MQTT — Fase 3: cliente real** — adicionar `bertmelis/espMqttClient` lib_deps, implementar task `mqtt_svc` (core 0, prio 1, 4KB stack), LWT, backoff exponencial, telemetria JSON periódica.
- **MQTT — Fase 4: keyword detector** — implementar task `mqtt_kw` (core 1, prio 1, 3KB stack), tick 5s, leitura offset SD, match `strcasestr`, publish `/keyword`.
- **MQTT — Fase 5: testes** — soak 30 min Mosquitto local, validar `boot_count`/`heap_guard_reboots` no JSON, LWT, heap drain <50 B/min.
- Organizar `SoftwareQualification_*.docx` (3 versoes untracked na raiz): mover para pasta dedicada ou adicionar ao `.gitignore`.

## Feito
- 2026-05-14 — **WireGuard arco v1.73→v1.76 — FIX FINAL v1.76 timer cego**: Diagnóstico Fase 0 (pcap server vps51163 + kernel `wireguard +p`) confirmou bug lib `smartalock/wireguard-lwip` upstream — server rejeita TODA INIT pos-1o-handshake como `Invalid handshake initiation`. Solução: bypass app-layer. Arco:
  - **v1.73**: removido `esp_restart()` escalation (constraint registrador esterilização) + preemptive re-apply 100s. MAS gated `peer_up=true` → zombie pós-#1 (server handshake fixou 2h41min, RX 276 B).
  - **v1.74**: tentou remover gates `s_ever_up`/`s_last_up_ms` → reboot loop ~3min (boot_count +3 em 15min). Revertido.
  - **v1.75**: revert v1.74 = comportamento v1.73 zombie estável.
  - **v1.76 (FIX)**: simplificação radical — substituída lógica preemptive+reactive+observe+consec_fail por TIMER CEGO. `wg_keepalive_task` chama `wg_apply_locked()` incondicional cada 90s, independente de peer state. `is_peer_up()` só telemetria. Removidas vars `s_consec_fail`/`s_preemptive_count`.
  - Validação v1.76 (12 samples/6min): boot_count 384 estável ZERO reboots; SD log escreve (re-apply #1→#4 consistente); server last_handshake 16s atrás (vs 2h41min zombie); ESP RX 9092 B (33× vs v1.73). Build+flash COM3 OK (177s). Archive `FitaDigital_v1.76.bin`.
  - Lição: timer cego simples > state machine frágil. Constraint zero-reboot mantido todas versões; RS485 `/CICLOS` sempre intacta.
  - Commits: `3a20d00` (v1.73), `f8e3901` (v1.75 revert), `d01f9c6` (v1.76 FIX).
- 2026-05-13 — **WireGuard race condition fix + boot banner v1.70**: lib WireGuard.cpp split end() into end_phase1 + 100ms drain + end_phase2 (fix ciniml/WireGuard-ESP32-Arduino issue #51); net_wireguard.cpp keepalive task logs mirror to Serial.printf "[WG-KA]" for COM3 observability; app.cpp boot banner "[BOOT] FitaDigital firmware v%s"; platformio.ini FITADIGITAL_VERSION 1.69→1.70. Build+flash COM3 sucesso (173.54s): RAM 38.3% (125572 B), Flash 37.7% (2473601 B). Firmware archived FitaDigital_v1.70.bin. Zero warnings from WireGuard.cpp/app.cpp (netif functions resolution clean via wireguardif.h). Pronto p/ crash-analyzer soak.
- 2026-05-12 — **WireGuard watchdog interval fix v1.62**: v1.60/v1.61 watchdog re-apply task interval 300s → stale handshake 16min (server endpoint mapping expired before next re-apply window). v1.62: reduce interval 300s→90s (constraint: WG REJECT_AFTER_TIME=180s, need fresh handshake before timeout). Build+flash COM3 sucesso (172.33s): RAM 38.3% (125588 B), Flash 37.7% (2473289 B). Firmware archived FitaDigital_v1.62.bin. Pronto p/ soak validação 17min+ zero reboots.
- 2026-05-12 — **WireGuard watchdog heap leak fix v1.60**: `wg_watchdog_task` v1.59 criava nova sessão `esp_ping` a cada 30s sem `esp_ping_delete_session` (callbacks vazia, `on_ping_end` nunca disparava) → dreno ~3KB/ciclo → HEAP_GUARD `esp_restart()` ~100s uptime. Refactor v1.60: UMA sessão `esp_ping` persistente (count=0 loop infinito, interval_ms=30000) criada once e reutilizada; parada/recriada só quando IP muda ou WiFi cai. Build+flash COM3 sucesso (167.66s): RAM 38.3% (125588 B), Flash 37.7% (2473289 B). Firmware archived FitaDigital_v1.60.bin. Pronto p/ soak.
- 2026-05-09 — **Soak v1.45 rotation desactivada**: v1.44 stack-only (3K stack) travou sd_io task sob snapshot loop. Bypass: comentar `rotate_dir_keep_last()` em screenshot_worker. Root cause pendente — hipóteses: stack overflow sd_io worker, SD.remove block, qsort recursion. Solução provável: PSRAM-allocated buffer no screenshot_init.
