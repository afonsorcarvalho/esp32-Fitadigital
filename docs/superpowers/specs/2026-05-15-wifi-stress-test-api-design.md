# WiFi Stress Test API — Design

Date: 2026-05-15
Target firmware: v1.82 → v1.83
Author: brainstorming session

## Goal

Provide remote stress test of WiFi self-healing layered escalation introduced
in v1.82 (`wg_keepalive_task` SOFT/HARD path). Driven by external script via
HTTP API, no router intervention.

## Background

v1.82 added `wifi_keepalive_tick` in `src/net_wireguard.cpp`:

- T1 = 30s sem `WL_CONNECTED` → SOFT (1x): `net_wifi_begin_saved`
- T2 = 5min sem `WL_CONNECTED` → HARD (repetível): `esp_wifi_stop` + `esp_wifi_start` + `net_wifi_begin_saved`

Path só dispara quando `WiFi.setAutoReconnect(true)` estagna (heap pressure).
Para validar end-to-end sem aguardar bug natural, simular estagnação:
disable autoReconnect + `esp_wifi_disconnect()` + sleep N seconds.

## Endpoint

```
POST /api/wifi/stress?down_s=<N>
Auth: Basic (mesma de outros endpoints settings)
Sanity: 5 <= N <= 600
```

Response 200:
```json
{ "scheduled_down_s": 35, "task_started": true }
```

Errors:
- N fora range → 400 `{"error":"down_s out of range (5..600)"}`
- Task creation falha → 500 `{"error":"task spawn failed"}`

## Implementation

### Handler (`src/web_portal/web_portal.cpp`)

`handle_wifi_stress_post(request)`:
1. Parse query `down_s`, validar range
2. `xTaskCreatePinnedToCore(wifi_stress_task, "wifi_stress", 4096, ...)` passing `down_s` via heap-allocated struct
3. Reply 200 imediatamente

### Stress task (file own escopo, linkado em `src/web_portal/web_portal.cpp`)

```c
static void wifi_stress_task(void *arg) {
    uint32_t down_s = *(uint32_t*)arg;
    free(arg);
    app_log_feature_writef("WARN", "WIFI",
        "Stress disconnect down_s=%u (auto-reconnect off)", down_s);
    WiFi.setAutoReconnect(false);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(down_s * 1000U));
    WiFi.setAutoReconnect(true);
    app_log_feature_writef("INFO", "WIFI",
        "Stress window done — self-healing tomara conta");
    vTaskDelete(NULL);
}
```

NÃO chamar `WiFi.begin()` no fim — deixa `wifi_keepalive_tick` actuar
(SOFT em ~30s, HARD em ~300s, conforme N).

### Route registration

Em `web_portal_start()`, ao lado de outras rotas POST settings:
```c
s_srv->on("/api/wifi/stress", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!web_auth_check(request)) return;
    handle_wifi_stress_post(request);
});
```

### Orchestrator script (`tools/wifi_stress.py`)

- Imports: `requests`, `time`, `csv`, `argparse`
- Args: `--ip 192.168.0.197 --user esp32 --pwd esp32 --cycles 3 --out logs/wifi_stress_<ts>.csv`
- Sequence: `[(35,"SOFT"),(305,"HARD")] * cycles`
- Per cycle:
  1. Read `/api/health` → record `uptime_before`
  2. POST `/api/wifi/stress?down_s=N` (Basic auth)
  3. Sleep `N + 5s` (window + small slack)
  4. Poll `/api/health` cada 5s, max 300s extra
  5. First 200 response → record `recovery_s = (poll_t - disconnect_at)`,
     `uptime_after`. Compute `reboot_detected = uptime_after < uptime_before + (recovery_s)`
  6. Sleep 60s gap
- CSV columns: `cycle,label,down_s,disconnect_at_iso,recovery_s,uptime_before,uptime_after,reboot_detected,wg_handshake_seen`
- `wg_handshake_seen`: opcional, parsing fdigi.log via FTP grep "Peer up" timestamp pos-recovery
- Print summary: total cycles, PASS/FAIL count, max recovery, reboot count

### Files modified

- `src/web_portal/web_portal.cpp` — handler + task + route
- `platformio.ini` build_flags (`FITADIGITAL_VERSION="1.82"`) — bump 1.82 → 1.83
- `tools/wifi_stress.py` — novo
- `TODO.md` — entry "Em curso" mover Feito quando concluído
- `firmware_versions/FitaDigital_v1.83.bin` — gerado pelo post-script PIO

## Success metrics

PASS critérios:
- 0 reboots (uptime monotónico através ciclos)
- SOFT cycles (down_s=35): recovery <60s pós-window end
- HARD cycles (down_s=305): recovery <60s pós-window end
- fdigi.log contém para cada ciclo:
  - `WARN | WIFI | Stress disconnect down_s=N`
  - `WARN | WIFI | WiFi DOWN detectado`
  - `WARN | WIFI | Soft reconnect apos 30 s` (SOFT) OU `Hard reset stack apos 300 s` (HARD)
  - `INFO | WIFI | Voltou apos X s`
- WG handshake renovado pós-recovery (Peer up dentro de 90s pós-WiFi up)

FAIL critérios:
- Qualquer reboot durante ciclo (uptime_after < expected)
- Recovery >60s pós-window
- Self-healing log entry ausente

## Risks

- `WiFi.setAutoReconnect(false)` aplicado depois de `esp_wifi_disconnect()` pode race — invertir ordem no design (set primeiro, disconnect depois) — feito acima.
- Stack `wifi_stress` task: 4096 deve chegar (chamadas leves). Bump se canary trip.
- HARD cycles 305s + 60s gap × 3 = ~18min. Total run com 3 ciclos completos: ~25min. OK.
- Device unresponsive durante window: HTTP timeouts esperados, script handle.

## Out of scope

- Roaming / multi-AP testing
- Heap pressure simulation (path natural — fica para teste futuro)
- Stress concurrente com RS485/MQTT load
