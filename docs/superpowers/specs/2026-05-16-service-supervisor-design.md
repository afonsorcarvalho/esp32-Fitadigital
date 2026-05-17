# Service Supervisor — Design

Date: 2026-05-16
Target firmware: v1.86 → v1.87
Branch: `worktree-feat-service-supervisor`
Author: brainstorming session

## Goal

Per-service watchdog framework for ESP32-S3 firmware (FitaDigital). Detects
service-level hangs and heap leaks in WiFi, WireGuard, FTP server, RS485
capture without rebooting the device. Restarts the offending service in
isolation; escalates to `esp_restart()` only if rate-limit exceeded.

Driven by user demand after WiFi stress test (v1.83-v1.86) exposed:
- WiFi + WG soft-reconnect race causing TASK_WDT reboots (fixed v1.86)
- FTP server dies under heap pressure (no recovery)
- xTaskCreate fails after heap fragmentation accumulates (v1.86 stress 17/20 fail post cycle 3)

## Background

v1.82 added `wifi_keepalive_tick` — service-internal layered recovery for WiFi.
v1.76 added timer-cego re-apply for WireGuard. v1.86 added panic_logger
breadcrumb diagnostic.

This design generalizes these patterns: every long-running service registers
with a central supervisor. Supervisor monitors stack hwm, task state, heap
delta, optional health check. On anomaly, schedules async restart via worker
task. Worker calls service-specific restart_cb that tears down and re-inits.

Goal: zero device reboots from service-level failures; visible per-service
metrics for production debugging.

## Architecture

### Modules

| File | Action | Responsibility |
|---|---|---|
| `src/service_supervisor.h` | NEW | Public API + types |
| `src/service_supervisor.cpp` | NEW | Registry, timer mgmt, worker task, hook |
| `src/net_services.cpp` | MODIFY | Add `wifi_supervisor_restart()` + register |
| `src/net_wireguard.cpp` | MODIFY | Add `wg_supervisor_restart()` + register |
| `src/net_services.cpp` (FTP block) | MODIFY | Add `ftp_supervisor_restart()` + register |
| `src/cycles_rs485.cpp` | MODIFY | Add `rs485_supervisor_restart()` + register |
| `src/web_portal/web_portal.cpp` | MODIFY | `GET /api/system/services` endpoint |
| `src/app.cpp` | MODIFY | Call `service_supervisor_init()` after services |
| `src/app_settings.{h,cpp}` | MODIFY | Add NVS counters: `supervisor_restart_<svc>`, `supervisor_escalations` |
| `platformio.ini` | MODIFY | Bump `FITADIGITAL_VERSION` 1.86 → 1.87 |
| `TODO.md` | MODIFY | Em curso → Feito |

### Public API (`service_supervisor.h`)

```c
typedef int (*service_restart_cb_t)(void);
typedef bool (*service_health_cb_t)(void);

typedef struct {
    const char *name;                  // short, e.g. "wifi", "wg", "ftp", "rs485"
    TaskHandle_t task;                 // handle of monitored task
    service_restart_cb_t restart_cb;   // sync teardown + re-init; <5s ideal
    service_health_cb_t  health_cb;    // optional, NULL = skip
    uint32_t quiet_max_ms;             // 0 = skip quiet check (FTP/RS485 idle)
    uint32_t heap_leak_threshold;      // bytes; 0 = use default 2048
} service_register_t;

void service_supervisor_init(void);
int  service_supervisor_register(const service_register_t *reg);
void service_supervisor_heartbeat(const char *name);  // called by service tick
void service_supervisor_set_heap_baseline(const char *name, uint32_t bytes);

/* Telemetry accessors for /api/system/services endpoint */
typedef struct {
    const char *name;
    uint32_t stack_hwm;
    uint32_t stack_hwm_min;
    uint32_t heap_owned_baseline;
    uint32_t restart_count;
    uint32_t last_restart_ms;
    const char *last_restart_reason;
} service_status_t;

size_t service_supervisor_get_all_status(service_status_t *out, size_t max);
uint32_t service_supervisor_get_escalations(void);
```

### Internal data flow

```
[per-service timer fires 10s] → check stack_hwm + state + heap delta + heartbeat
                              → if anomaly → xQueueSend(restart_q)
                                                ↓
[stack overflow hook (ISR)] → save breadcrumb + xQueueSendFromISR
                                                ↓
[worker task (core 1, prio 2)] ← restart_msg_t
                              → rate_limit_check → if exceeded: esp_restart
                              → log + breadcrumb
                              → restart_cb()
                              → measure reclaimed heap, log result
                              → increment counters (NVS)
```

## Detection mechanism

### Per-service timer (FreeRTOS Software Timer)

One `TimerHandle_t` per registered service. Period 10s. Auto-reload.

Timer callback (runs in timer-service task context):
```c
static void supervisor_timer_cb(TimerHandle_t t) {
    int idx = (int)(intptr_t)pvTimerGetTimerID(t);
    service_entry_t *e = &g_registry[idx];

    // 1. Task disappeared
    eTaskState st = eTaskGetState(e->task);
    if (st == eDeleted || st == eInvalid) {
        post_restart(idx, "task_gone");
        return;
    }

    // 2. Stack near overflow (< 64 words = 256B remaining)
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(e->task);
    if (hwm < 64U) {
        post_restart(idx, "stack_critical");
        return;
    }
    if (hwm < e->stack_hwm_min || e->stack_hwm_min == 0) e->stack_hwm_min = hwm;
    e->stack_hwm = hwm;

    // 3. Stack hwm shrinking rapidly = leak heuristic
    if (e->prev_hwm > 0 && e->prev_hwm > hwm && (e->prev_hwm - hwm) > 256U) {
        post_restart(idx, "stack_leak_rapid");
        return;
    }
    e->prev_hwm = hwm;

    // 4. Heartbeat quiet (only if quiet_max_ms > 0)
    if (e->quiet_max_ms > 0U) {
        uint32_t since = millis() - e->last_heartbeat_ms;
        if (since > e->quiet_max_ms) {
            post_restart(idx, "no_heartbeat");
            return;
        }
    }

    // 5. Optional custom health check
    if (e->health_cb && !e->health_cb()) {
        post_restart(idx, "health_fail");
        return;
    }
}
```

### Stack overflow hook

Override `vApplicationStackOverflowHook` (ESP-IDF panic path). NOT used to
soft-restart (stack already corrupted — unsafe). Used only to save
diagnostic breadcrumb before default panic abort + reboot:
```c
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    panic_breadcrumb_set(pcTaskName);  // already exists from v1.84
    // Fall through to default ESP-IDF panic = abort + reboot
}
```

### Per-service detection config

| Service | quiet_max_ms | health_cb | heap_leak_threshold | Notes |
|---|---|---|---|---|
| wifi_keepalive | 30000 | NULL | 2048 | tick 20s; 30s margin |
| wg_keepalive_task | 100000 | `s_wg.is_peer_up()` opcional | 2048 | re-apply 90s; quiet inclui margem |
| ftp_task | 0 (skip) | NULL | 2048 | idle válido — só heap check |
| cycles_rs485 | 0 (skip) | NULL | 2048 | idle válido horas (autoclave parado) |

Services chamam `service_supervisor_heartbeat("name")` no início de cada
iteration. Skip heartbeat = `quiet_max_ms=0`.

## Restart workflow

### Worker task

Created in `service_supervisor_init()`:
- Stack: 4096B static (`xTaskCreateStaticPinnedToCore`)
- Pinned core 1
- Priority 2 (above wifi_keepalive=1, mqtt=1)
- Reads from `restart_q` (queue capacity 4, static)

```c
static void supervisor_worker_task(void *arg) {
    restart_msg_t msg;
    for (;;) {
        if (xQueueReceive(restart_q, &msg, portMAX_DELAY) != pdTRUE) continue;
        service_entry_t *e = &g_registry[msg.idx];

        // Cool-down 30s
        if (e->last_restart_ms != 0 && (millis() - e->last_restart_ms) < 30000U) {
            app_log_feature_writef("WARN", "SUPERVISOR",
                "Skip restart %s — cooldown (%lums since last)",
                e->name, (unsigned long)(millis() - e->last_restart_ms));
            continue;
        }

        // Rate limit (Section 4)
        if (!rate_limit_allow(e)) {
            app_log_feature_writef("ERROR", "SUPERVISOR",
                "Service %s rate-limit exceeded (%u in 10min) — esp_restart escalation",
                e->name, (unsigned)e->window_restarts);
            app_settings_supervisor_escalations_increment();
            panic_breadcrumb_set("supervisor:escalate");
            vTaskDelay(pdMS_TO_TICKS(500));  // flush logs
            esp_restart();
        }

        // Diagnostic
        char tag[32];
        snprintf(tag, sizeof(tag), "restart:%s:%s", e->name, msg.reason);
        panic_breadcrumb_set(tag);

        app_log_feature_writef("WARN", "SUPERVISOR",
            "Restart svc=%s reason=%s hwm=%u quiet=%lums",
            e->name, msg.reason, (unsigned)e->prev_hwm,
            (unsigned long)(millis() - e->last_heartbeat_ms));

        // Measure heap before
        uint32_t heap_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

        // Invoke restart callback (synchronous)
        int rc = e->restart_cb ? e->restart_cb() : -1;

        // Measure reclaim
        uint32_t heap_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int32_t reclaimed = (int32_t)heap_after - (int32_t)heap_before;

        // Update state
        e->last_restart_ms = millis();
        e->last_heartbeat_ms = millis();
        e->prev_hwm = 0;
        e->restart_count++;
        e->last_restart_reason = msg.reason;
        app_settings_supervisor_restart_increment(e->name);

        app_log_feature_writef("INFO", "SUPERVISOR",
            "Restart done svc=%s rc=%d reclaimed=%ld count=%u",
            e->name, rc, (long)reclaimed, (unsigned)e->restart_count);

        panic_breadcrumb_clear();
    }
}
```

### Per-service restart callbacks

```c
// WiFi (em net_services.cpp ou net_wifi.cpp)
int wifi_supervisor_restart(void) {
    WiFi.disconnect();  // does NOT erase saved config
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_wifi_start();
    vTaskDelay(pdMS_TO_TICKS(200));
    net_wifi_begin_saved();
    return 0;
}

// WireGuard (em net_wireguard.cpp)
int wg_supervisor_restart(void) {
    if (s_apply_mtx) xSemaphoreTake(s_apply_mtx, portMAX_DELAY);
    s_wg.end();
    s_wg_active = false;
    if (s_apply_mtx) xSemaphoreGive(s_apply_mtx);
    vTaskDelay(pdMS_TO_TICKS(100));
    net_wireguard_apply();
    return 0;
}

// FTP (em net_services.cpp)
int ftp_supervisor_restart(void) {
    // SimpleFTPServer expose end()/begin()? Verify lib API
    // Fallback: kill server task if dedicated, recreate
    extern FtpServer ftpSrv;  // adjust to actual symbol
    ftpSrv.end();  // OR equivalent stop API
    vTaskDelay(pdMS_TO_TICKS(200));
    ftp_init();   // existing init
    return 0;
}

// RS485 (em cycles_rs485.cpp)
int rs485_supervisor_restart(void) {
    cycles_rs485_stop();   // new helper: kills task + closes Serial1
    vTaskDelay(pdMS_TO_TICKS(100));
    cycles_rs485_init();    // existing
    return 0;
}
```

`cycles_rs485_stop()` is a new helper:
```c
void cycles_rs485_stop(void) {
    if (s_rs485_task) { vTaskDelete(s_rs485_task); s_rs485_task = NULL; }
    Serial1.end();
}
```

## Safety: rate limiting + escalation

### Per-service rate limit

10-minute sliding window. Max 3 restarts per window. State per `service_entry_t`:
```c
uint32_t window_start_ms;
uint32_t window_restarts;
```

`rate_limit_allow(e)`:
```c
bool rate_limit_allow(service_entry_t *e) {
    uint32_t now = millis();
    if ((now - e->window_start_ms) > 600000U) {
        e->window_start_ms = now;
        e->window_restarts = 0;
    }
    e->window_restarts++;
    return e->window_restarts <= 3U;
}
```

### Cross-service escalation

Global counter: if `≥2` services hit rate-limit in same 10-min window, escalate
`esp_restart()` early (system likely rotten). Tracked in supervisor module
state.

### Stack overflow special path

Stack overflow = corruption already happened. NOT eligible for soft-restart.
Hook saves breadcrumb only; default panic continues to `esp_restart`.

## Telemetry

### NVS-persistent counters (`app_settings`)

- `supervisor_restart_<service>` — total restarts since last NVS clear
- `supervisor_escalations` — total times worker escalated to `esp_restart`

Visible in boot log after panic_logger_init().

### HTTP endpoint

`GET /api/system/services` (Basic Digest auth, admin:pin)

Response (JSON, ~512B):
```json
{
  "uptime_s": 12345,
  "heap": {
    "internal_free": 22176,
    "internal_min": 17288,
    "psram_free": 3551852
  },
  "services": [
    {
      "name": "wifi",
      "stack_hwm": 1248,
      "stack_hwm_min": 1024,
      "heap_owned_baseline": 8192,
      "restart_count": 0,
      "last_restart_ms": 0,
      "last_restart_reason": null
    },
    { "name": "wg", ... },
    { "name": "ftp", ... },
    { "name": "rs485", ... }
  ],
  "supervisor": {
    "escalations": 0,
    "active_services": 4
  }
}
```

### Log entries

- Restart: `WARN | SUPERVISOR | Restart svc=<name> reason=<reason> ...`
- Restart done: `INFO | SUPERVISOR | Restart done svc=<name> rc=N reclaimed=N count=N`
- Escalation: `ERROR | SUPERVISOR | Service <name> rate-limit exceeded ...`
- Cooldown skip: `WARN | SUPERVISOR | Skip restart <name> — cooldown ...`

### Per-service heap baseline measurement

Each service registers AFTER its init completes:
```c
// Em init de cada service:
uint32_t pre = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
... actual init code ...
uint32_t post = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
service_register_t reg = { ... };
service_supervisor_register(&reg);
service_supervisor_set_heap_baseline(reg.name, pre - post);
```

Honest limitation: ESP-IDF heap não suporta tagged allocations. `heap_owned_baseline` é estimativa (heap consumed durante init window), não tracking preciso. Suficiente para detectar deriva grave.

## Success metrics

PASS critérios:
- Re-run `tools/wifi_stress.py --cycles 10` → **0 device reboots** (mantém v1.86)
- 20/20 ciclos PASS (sem `task_spawn_failed`)
- `GET /api/system/services` retorna JSON válido em qualquer ponto durante stress
- Boot log mostra `supervisor_restart_<service>` counters NVS-persistentes
- Se cycle X causar service hang → log mostra `Restart svc=X reason=... rc=0 reclaimed=N`
- Zero `esp_restart escalation` para stress 20 ciclos (rate-limit não atingido)

FAIL critérios:
- Qualquer device reboot durante stress 20 ciclos
- `service_supervisor_init()` crash boot
- Restart_cb hang >5s (worker WDT)
- Rate-limit hit em <20 cycles run

## Risks

- **FTP lib API**: `SimpleFTPServer.end()` may not exist. Need to check lib source or use alternative (recreate server object). Mitigation noted in plan.
- **WG restart mid-handshake**: `s_wg.end()` while handshake in flight may leak lwip pbufs. Mitigated by `s_apply_mtx` take.
- **RS485 task delete**: vTaskDelete from another task is safe in FreeRTOS but if RS485 task holds SD mutex when killed, mutex stays locked = deadlock. Mitigation: rs485 task should release mutex before yield points; verify in code.
- **Stack 4096B for worker**: worker calls restart_cb which may use deep stack (e.g., esp_wifi_stop). Bump to 6144 if HWM <512 after first stress run.
- **NVS write storm**: increment counter per restart → NVS wear. Acceptable: <100 writes/year expected production.
- **Timer service stack**: ESP-IDF default `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=2048` may be tight with 4 supervisor timers callback. Bump via sdkconfig if needed (validate post-build).
- **Race timer_cb vs restart**: timer may fire while worker restarting same service. Mitigation: cool-down 30s + worker holds e->restarting flag.

## Out of scope

- Per-allocation heap tagging (would need IDF heap trace, heavy)
- LVGL service supervision (LVGL has its own task; integration deferred)
- MQTT service supervision (already has reconnect logic; future iteration)
- Remote restart trigger via HTTP POST (could add but YAGNI for now)
- Watchdog telemetry over MQTT (out of scope; HTTP endpoint suffices)

## Implementation order (preview for writing-plans skill)

1. `service_supervisor.{h,cpp}` core (registry, timer, worker, queue, hook) — testable in isolation
2. NVS counter additions to `app_settings`
3. Register WiFi service + restart_cb
4. Register WireGuard service + restart_cb
5. Register FTP service + restart_cb (verify SimpleFTPServer API)
6. Register RS485 service + restart_cb (add `cycles_rs485_stop`)
7. `GET /api/system/services` endpoint in web_portal
8. Bump version + integration test (stress 20 cycles)
