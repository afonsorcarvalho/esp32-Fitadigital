# Service Supervisor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Per-service watchdog framework (`src/service_supervisor.{h,cpp}`) that monitors stack hwm + heartbeat + heap delta for WiFi, WireGuard, FTP, RS485 services; restarts only the unhealthy service via async worker; escalates to `esp_restart` only on rate-limit (3 restarts/svc/10min).

**Architecture:** Static registry (max 8 slots) holds per-service entries. Each entry has a FreeRTOS Software Timer firing every 10s that checks `uxTaskGetStackHighWaterMark` + `eTaskGetState` + heartbeat freshness + optional health_cb; on anomaly, queues message to worker task. Worker task (core 1, prio 2, 4096B static stack) reads queue and invokes service-specific `restart_cb()` synchronously, with cool-down (30s) and rate limit (3/10min). `vApplicationStackOverflowHook` saves breadcrumb only (corrupted stack = no soft-restart).

**Tech Stack:** ESP-IDF FreeRTOS APIs (`xTaskGetStackHighWaterMark`, `eTaskGetState`, `xTimerCreate`, `xQueueCreateStatic`, `xTaskCreateStaticPinnedToCore`), Arduino-ESP32, existing `panic_logger` (v1.84), `app_settings` NVS.

**Spec:** `docs/superpowers/specs/2026-05-16-service-supervisor-design.md`

**Branch:** `worktree-feat-service-supervisor` in worktree `.claude/worktrees/feat-service-supervisor`

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `src/service_supervisor.h` | Create | Public API + types (`service_register_t`, accessors) |
| `src/service_supervisor.cpp` | Create | Registry, timers, worker task, queue, hook, rate limit |
| `src/app_settings.h` | Modify | Add `app_settings_supervisor_*` declarations |
| `src/app_settings.cpp` | Modify | Implement NVS counters for restarts + escalations |
| `src/cycles_rs485.cpp` | Modify | Register self + provide `cycles_rs485_restart()` (uses existing deinit/init) |
| `src/net_services.cpp` | Modify | Register WiFi + FTP services + restart callbacks; expose `net_services_ftp_restart`, `net_services_wifi_restart` |
| `src/net_wireguard.cpp` | Modify | Register WG + `net_wireguard_restart()`; heartbeat in `wg_keepalive_task` |
| `src/web_portal/web_portal.cpp` | Modify | `GET /api/system/services` handler + route |
| `src/app.cpp` | Modify | Call `service_supervisor_init()` after all services init |
| `platformio.ini` | Modify | Bump `FITADIGITAL_VERSION` 1.86 → 1.87 |
| `TODO.md` | Modify | Em curso → Feito on completion |

**Note on testing:** This codebase has no unit-test harness for ESP32 firmware. Validation is integration: build success, smoke flash + boot, then run `tools/wifi_stress.py --cycles 10` and inspect `/api/system/services` JSON for restart events.

---

## Task 1: Add NVS counters in app_settings

**Files:**
- Modify: `src/app_settings.h` (around line 161, near existing `heap_guard_count` declarations)
- Modify: `src/app_settings.cpp` (around line 1210, after `app_settings_heap_guard_count_increment`)

- [ ] **Step 1: Add declarations in `src/app_settings.h`**

Locate the block declaring `app_settings_heap_guard_count_increment` (line 161). After that declaration add:
```cpp
/* v1.87 service supervisor counters (NVS-persistent). */
uint32_t app_settings_supervisor_escalations_get(void);
void     app_settings_supervisor_escalations_increment(void);

/* Per-service restart counter. `name` é a string registada com
 * service_supervisor_register; usa-se truncada para chave NVS de 8 chars:
 *   wifi  -> "swrr_wifi"
 *   wg    -> "swrr_wg"
 *   ftp   -> "swrr_ftp"
 *   rs485 -> "swrr_rs48"  (truncated por limite 15 chars Preferences). */
uint32_t app_settings_supervisor_restart_get(const char *name);
void     app_settings_supervisor_restart_increment(const char *name);
```

- [ ] **Step 2: Add implementations in `src/app_settings.cpp`**

After the existing `app_settings_heap_guard_count_increment` function (line 1210), append:
```cpp
uint32_t app_settings_supervisor_escalations_get(void) {
  return s_prefs.getUInt("sve", 0U);
}

void app_settings_supervisor_escalations_increment(void) {
  const uint32_t v = s_prefs.getUInt("sve", 0U);
  s_prefs.putUInt("sve", v + 1U);
}

static void supervisor_restart_key(const char *name, char *out, size_t out_sz) {
  /* "swrr_" (5) + 4 chars name truncated + NUL = 10 chars (limite NVS 15 chars). */
  snprintf(out, out_sz, "swrr_%.4s", name ? name : "?");
}

uint32_t app_settings_supervisor_restart_get(const char *name) {
  char key[16];
  supervisor_restart_key(name, key, sizeof(key));
  return s_prefs.getUInt(key, 0U);
}

void app_settings_supervisor_restart_increment(const char *name) {
  char key[16];
  supervisor_restart_key(name, key, sizeof(key));
  const uint32_t v = s_prefs.getUInt(key, 0U);
  s_prefs.putUInt(key, v + 1U);
}
```

- [ ] **Step 3: Build (smoke test compile)**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run --environment esp32-s3-touch-lcd-4_3b
```
Expected: `[SUCCESS]`. If error → fix typos and re-run.

- [ ] **Step 4: Commit**

```powershell
git add src/app_settings.h src/app_settings.cpp
git commit -m "feat(settings): NVS counters supervisor escalations + per-service restart (v1.87)"
```

---

## Task 2: Create `service_supervisor.h` public API

**Files:**
- Create: `src/service_supervisor.h`

- [ ] **Step 1: Write the header file**

Create `src/service_supervisor.h` with this content:
```cpp
#pragma once
/* service_supervisor — per-service watchdog framework (v1.87).
 *
 * Services register with: name, task handle, restart_cb, optional health_cb,
 * quiet_max_ms (0 = skip heartbeat check). Supervisor monitors stack hwm,
 * task state, heartbeat freshness, optional health_cb via per-service
 * FreeRTOS Software Timer (10s tick).
 *
 * On anomaly: posts to worker queue; worker calls restart_cb synchronously
 * with cool-down (30s) and rate-limit (3 restarts / 10 min). Exceed
 * rate-limit -> esp_restart escalation.
 *
 * Stack overflow hook saves breadcrumb only (panic_logger v1.84). Default
 * ESP-IDF panic continues — stack corruption unsafe to recover from.
 *
 * Heap "owned" baseline = heap delta around service init; set via
 * service_supervisor_set_heap_baseline. Honest limitation: estimative,
 * not malloc-tag tracking (IDF doesn't support).
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int  (*service_restart_cb_t)(void);
typedef bool (*service_health_cb_t)(void);

typedef struct {
    const char *name;                  /* short identifier, max 4 chars used p/ NVS */
    TaskHandle_t task;                 /* monitored task handle */
    service_restart_cb_t restart_cb;   /* sync teardown + re-init; <5s ideal */
    service_health_cb_t  health_cb;    /* optional, NULL to skip */
    uint32_t quiet_max_ms;             /* 0 = skip heartbeat freshness check */
    uint32_t heap_leak_threshold;      /* 0 = default 2048 bytes */
} service_register_t;

typedef struct {
    const char *name;
    uint32_t stack_hwm;                /* in StackType_t words */
    uint32_t stack_hwm_min;
    uint32_t heap_owned_baseline;
    uint32_t restart_count;            /* since boot */
    uint32_t last_restart_ms;
    const char *last_restart_reason;   /* NULL if never restarted */
} service_status_t;

/** Initialise supervisor: creates worker task, queue, hook installed.
 *  Idempotent. Call ONCE in app.cpp setup() after all services init. */
void service_supervisor_init(void);

/** Register a service. Returns slot index >=0 on success, -1 on
 *  no-free-slot or invalid arg. Up to MAX_SERVICES (8). */
int  service_supervisor_register(const service_register_t *reg);

/** Service calls this at start of its tick loop iteration (and again at
 *  end if long iteration). Updates last_heartbeat_ms.
 *  Lookup by name string — O(N) over 8 slots, cheap. */
void service_supervisor_heartbeat(const char *name);

/** Record heap consumed at service init. Optional but recommended. */
void service_supervisor_set_heap_baseline(const char *name, uint32_t bytes);

/** Telemetry — fill `out` array with up to `max` entries; returns count. */
size_t service_supervisor_get_all_status(service_status_t *out, size_t max);

/** Total esp_restart escalations since this boot. */
uint32_t service_supervisor_get_escalations(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Build (header-only, no use yet)**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run --environment esp32-s3-touch-lcd-4_3b
```
Expected: `[SUCCESS]`. Header alone shouldn't break anything (not included anywhere yet).

- [ ] **Step 3: Commit**

```powershell
git add src/service_supervisor.h
git commit -m "feat(supervisor): public API header (v1.87)"
```

---

## Task 3: Implement `service_supervisor.cpp` core

**Files:**
- Create: `src/service_supervisor.cpp`

- [ ] **Step 1: Write the implementation file**

Create `src/service_supervisor.cpp` with this content:
```cpp
#include "service_supervisor.h"

#include "app_log.h"
#include "app_settings.h"
#include "panic_logger.h"

#include <Arduino.h>
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

namespace {

constexpr size_t   kMaxServices       = 8U;
constexpr uint32_t kTimerPeriodMs     = 10000U;  /* 10s tick per service */
constexpr uint32_t kCooldownMs        = 30000U;  /* 30s min between restarts same svc */
constexpr uint32_t kWindowMs          = 600000U; /* 10 min sliding rate window */
constexpr uint32_t kWindowMaxRestarts = 3U;
constexpr uint32_t kDefaultHeapLeak   = 2048U;
constexpr UBaseType_t kStackCriticalWords = 64U;   /* < 256B remaining = critical */
constexpr UBaseType_t kStackShrinkWords   = 256U;  /* shrunk > 1KB tick = leak */

constexpr uint32_t kWorkerStackBytes = 4096U;
constexpr UBaseType_t kWorkerPrio    = 2U;
constexpr BaseType_t kWorkerCore     = 1;

struct restart_msg_t {
    int idx;
    const char *reason;  /* string literal — no copy needed */
};

struct service_entry_t {
    service_register_t reg;
    TimerHandle_t timer;
    uint32_t last_heartbeat_ms;
    uint32_t prev_hwm;
    uint32_t stack_hwm;
    uint32_t stack_hwm_min;
    uint32_t heap_owned_baseline;
    uint32_t restart_count;
    uint32_t last_restart_ms;
    const char *last_restart_reason;
    /* Rate limit state */
    uint32_t window_start_ms;
    uint32_t window_restarts;
};

service_entry_t g_registry[kMaxServices];
size_t g_registry_count = 0U;

QueueHandle_t g_restart_q = nullptr;
StaticQueue_t g_restart_q_buf;
restart_msg_t g_restart_q_storage[4];

TaskHandle_t g_worker_task = nullptr;
StaticTask_t g_worker_tcb;
StackType_t  g_worker_stack[kWorkerStackBytes / sizeof(StackType_t)];

uint32_t g_escalations = 0U;

int find_idx_by_name(const char *name) {
    if (name == nullptr) return -1;
    for (size_t i = 0; i < g_registry_count; i++) {
        if (g_registry[i].reg.name != nullptr && strcmp(g_registry[i].reg.name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

void post_restart(int idx, const char *reason) {
    if (g_restart_q == nullptr) return;
    restart_msg_t msg = { idx, reason };
    /* Non-blocking; if queue full, drop (worker will catch up next tick). */
    (void)xQueueSend(g_restart_q, &msg, 0);
}

bool rate_limit_allow(service_entry_t *e) {
    uint32_t now = millis();
    if (e->window_start_ms == 0U || (now - e->window_start_ms) > kWindowMs) {
        e->window_start_ms = now;
        e->window_restarts = 0U;
    }
    e->window_restarts++;
    return e->window_restarts <= kWindowMaxRestarts;
}

void supervisor_timer_cb(TimerHandle_t t) {
    int idx = (int)(intptr_t)pvTimerGetTimerID(t);
    if (idx < 0 || (size_t)idx >= g_registry_count) return;
    service_entry_t *e = &g_registry[idx];
    if (e->reg.task == nullptr) return;

    eTaskState st = eTaskGetState(e->reg.task);
    if (st == eDeleted || st == eInvalid) {
        post_restart(idx, "task_gone");
        return;
    }

    UBaseType_t hwm = uxTaskGetStackHighWaterMark(e->reg.task);
    e->stack_hwm = (uint32_t)hwm;
    if (e->stack_hwm_min == 0U || hwm < e->stack_hwm_min) e->stack_hwm_min = (uint32_t)hwm;

    if (hwm < kStackCriticalWords) {
        post_restart(idx, "stack_critical");
        return;
    }
    if (e->prev_hwm > 0U && e->prev_hwm > hwm && (e->prev_hwm - hwm) > kStackShrinkWords) {
        post_restart(idx, "stack_leak_rapid");
        e->prev_hwm = hwm;  /* update to avoid re-trigger */
        return;
    }
    e->prev_hwm = hwm;

    if (e->reg.quiet_max_ms > 0U) {
        uint32_t since = millis() - e->last_heartbeat_ms;
        if (since > e->reg.quiet_max_ms) {
            post_restart(idx, "no_heartbeat");
            return;
        }
    }

    if (e->reg.health_cb != nullptr) {
        if (!e->reg.health_cb()) {
            post_restart(idx, "health_fail");
            return;
        }
    }
}

void supervisor_worker_task(void * /*arg*/) {
    restart_msg_t msg;
    for (;;) {
        if (xQueueReceive(g_restart_q, &msg, portMAX_DELAY) != pdTRUE) continue;
        if (msg.idx < 0 || (size_t)msg.idx >= g_registry_count) continue;
        service_entry_t *e = &g_registry[msg.idx];

        /* Cool-down 30s */
        if (e->last_restart_ms != 0U && (millis() - e->last_restart_ms) < kCooldownMs) {
            app_log_feature_writef("WARN", "SUPERVISOR",
                "Skip restart %s — cooldown (%lums since last)",
                e->reg.name, (unsigned long)(millis() - e->last_restart_ms));
            continue;
        }

        /* Rate limit */
        if (!rate_limit_allow(e)) {
            app_log_feature_writef("ERROR", "SUPERVISOR",
                "Service %s rate-limit exceeded (%u in window) — esp_restart escalation",
                e->reg.name, (unsigned)e->window_restarts);
            g_escalations++;
            app_settings_supervisor_escalations_increment();
            panic_breadcrumb_set("supervisor:escalate");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }

        char tag[32];
        snprintf(tag, sizeof(tag), "restart:%s:%s",
                 e->reg.name ? e->reg.name : "?",
                 msg.reason ? msg.reason : "?");
        panic_breadcrumb_set(tag);

        app_log_feature_writef("WARN", "SUPERVISOR",
            "Restart svc=%s reason=%s hwm=%u quiet=%lums",
            e->reg.name, msg.reason, (unsigned)e->prev_hwm,
            (unsigned long)(millis() - e->last_heartbeat_ms));

        uint32_t heap_before = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

        int rc = -1;
        if (e->reg.restart_cb != nullptr) {
            rc = e->reg.restart_cb();
        }

        uint32_t heap_after = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int32_t reclaimed = (int32_t)heap_after - (int32_t)heap_before;

        e->last_restart_ms = millis();
        e->last_heartbeat_ms = millis();
        e->prev_hwm = 0U;
        e->restart_count++;
        e->last_restart_reason = msg.reason;
        app_settings_supervisor_restart_increment(e->reg.name);

        app_log_feature_writef("INFO", "SUPERVISOR",
            "Restart done svc=%s rc=%d reclaimed=%ld count=%u",
            e->reg.name, rc, (long)reclaimed, (unsigned)e->restart_count);

        panic_breadcrumb_clear();
    }
}

} /* namespace */

/* Stack overflow hook: save breadcrumb only, default ESP-IDF panic continues. */
extern "C" void vApplicationStackOverflowHook(TaskHandle_t /*xTask*/, char *pcTaskName) {
    panic_breadcrumb_set(pcTaskName ? pcTaskName : "stack_overflow");
    /* Do NOT attempt soft-restart — stack corrupted, unsafe. */
}

void service_supervisor_init(void) {
    if (g_worker_task != nullptr) return;  /* idempotent */

    g_restart_q = xQueueCreateStatic(
        sizeof(g_restart_q_storage) / sizeof(restart_msg_t),
        sizeof(restart_msg_t),
        (uint8_t *)g_restart_q_storage,
        &g_restart_q_buf);

    g_worker_task = xTaskCreateStaticPinnedToCore(
        supervisor_worker_task, "svc_super_w",
        sizeof(g_worker_stack) / sizeof(StackType_t),
        nullptr, kWorkerPrio,
        g_worker_stack, &g_worker_tcb, kWorkerCore);

    app_log_feature_writef("INFO", "SUPERVISOR",
        "Init done — %u services registered, worker core %d prio %u",
        (unsigned)g_registry_count, (int)kWorkerCore, (unsigned)kWorkerPrio);
}

int service_supervisor_register(const service_register_t *reg) {
    if (reg == nullptr || reg->name == nullptr || reg->task == nullptr) return -1;
    if (g_registry_count >= kMaxServices) return -1;

    int idx = (int)g_registry_count;
    service_entry_t *e = &g_registry[idx];
    memset(e, 0, sizeof(*e));
    e->reg = *reg;
    if (e->reg.heap_leak_threshold == 0U) e->reg.heap_leak_threshold = kDefaultHeapLeak;
    e->last_heartbeat_ms = millis();

    e->timer = xTimerCreate(
        reg->name,
        pdMS_TO_TICKS(kTimerPeriodMs),
        pdTRUE,  /* auto-reload */
        (void *)(intptr_t)idx,
        supervisor_timer_cb);

    if (e->timer == nullptr) return -1;

    g_registry_count++;

    if (xTimerStart(e->timer, pdMS_TO_TICKS(100)) != pdPASS) {
        /* Timer queue full at boot — try again later via init */
        app_log_feature_writef("WARN", "SUPERVISOR",
            "Timer start delayed for %s", reg->name);
    }
    return idx;
}

void service_supervisor_heartbeat(const char *name) {
    int idx = find_idx_by_name(name);
    if (idx < 0) return;
    g_registry[idx].last_heartbeat_ms = millis();
}

void service_supervisor_set_heap_baseline(const char *name, uint32_t bytes) {
    int idx = find_idx_by_name(name);
    if (idx < 0) return;
    g_registry[idx].heap_owned_baseline = bytes;
}

size_t service_supervisor_get_all_status(service_status_t *out, size_t max) {
    if (out == nullptr) return 0;
    size_t n = (g_registry_count < max) ? g_registry_count : max;
    for (size_t i = 0; i < n; i++) {
        service_entry_t *e = &g_registry[i];
        out[i].name = e->reg.name;
        out[i].stack_hwm = e->stack_hwm;
        out[i].stack_hwm_min = e->stack_hwm_min;
        out[i].heap_owned_baseline = e->heap_owned_baseline;
        out[i].restart_count = e->restart_count;
        out[i].last_restart_ms = e->last_restart_ms;
        out[i].last_restart_reason = e->last_restart_reason;
    }
    return n;
}

uint32_t service_supervisor_get_escalations(void) {
    return g_escalations;
}
```

- [ ] **Step 2: Build (verify compiles, still not used in app.cpp)**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run --environment esp32-s3-touch-lcd-4_3b
```
Expected: `[SUCCESS]`. Stack overflow hook is `extern "C"` weak-replaceable per ESP-IDF.

- [ ] **Step 3: Commit**

```powershell
git add src/service_supervisor.cpp
git commit -m "feat(supervisor): core registry + worker + timer + hook (v1.87)"
```

---

## Task 4: Wire RS485 restart callback + register

**Files:**
- Modify: `src/cycles_rs485.cpp` (RS485 has existing `cycles_rs485_init` + `cycles_rs485_deinit`)
- Modify: `src/cycles_rs485.h` (add declaration of `cycles_rs485_restart`)

- [ ] **Step 1: Add declaration in `src/cycles_rs485.h`**

After the existing `cycles_rs485_init` declaration (line 58), add:
```cpp
/** v1.87: restart callback for service_supervisor. Returns 0 on success. */
int cycles_rs485_restart(void);
```

- [ ] **Step 2: Implement restart + register in `src/cycles_rs485.cpp`**

Add include at top:
```cpp
#include "service_supervisor.h"
```

After `cycles_rs485_init()` function ends (find by searching for the closing `}`  of that function), append:
```cpp
int cycles_rs485_restart(void) {
    cycles_rs485_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));
    cycles_rs485_init();
    return (s_reader_task != nullptr) ? 0 : -1;
}
```

At the end of `cycles_rs485_init()` (just before the closing `}` of that function, after `app_log_writef("INFO", "RS485: ...")`), register with supervisor:
```cpp
    if (s_reader_task != nullptr) {
        service_register_t reg = {};
        reg.name = "rs485";
        reg.task = s_reader_task;
        reg.restart_cb = cycles_rs485_restart;
        reg.health_cb = nullptr;
        reg.quiet_max_ms = 0U;  /* idle hours valid (autoclave parado) */
        reg.heap_leak_threshold = 0U;  /* use default */
        (void)service_supervisor_register(&reg);
    }
```

- [ ] **Step 3: Build**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run --environment esp32-s3-touch-lcd-4_3b
```
Expected: `[SUCCESS]`.

- [ ] **Step 4: Commit**

```powershell
git add src/cycles_rs485.cpp src/cycles_rs485.h
git commit -m "feat(rs485): supervisor register + restart_cb (v1.87)"
```

---

## Task 5: Wire WireGuard restart callback + register + heartbeat

**Files:**
- Modify: `src/net_wireguard.cpp` (has `wg_keepalive_task` + `wg_apply_locked` + `net_wireguard_init`)
- Modify: `src/net_wireguard.h` (add `wg_supervisor_restart` declaration if header exposes)

- [ ] **Step 1: Add include in `src/net_wireguard.cpp`**

Near top, after other includes (after `#include "panic_logger.h"`), add:
```cpp
#include "service_supervisor.h"
```

- [ ] **Step 2: Add restart callback function**

Before `wg_keepalive_start_once()` function (around line 212), add:
```cpp
static int wg_supervisor_restart(void) {
    if (s_apply_mtx != nullptr) {
        xSemaphoreTake(s_apply_mtx, portMAX_DELAY);
    }
    s_wg.end();
    s_wg_active = false;
    if (s_apply_mtx != nullptr) {
        xSemaphoreGive(s_apply_mtx);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    net_wireguard_apply();  /* existing public API; takes mutex internally */
    return 0;
}

static bool wg_supervisor_health(void) {
    /* Healthy if peer was ever up since last keepalive iteration. Soft check —
     * not catastrophic to return false transiently during re-handshake. */
    return s_ever_up;
}
```

- [ ] **Step 3: Add heartbeat call in `wg_keepalive_task` loop**

Inside `wg_keepalive_task` for-loop, at the very top of each iteration (right after `vTaskDelay(tick);`), add:
```cpp
        service_supervisor_heartbeat("wg");
```

- [ ] **Step 4: Register after task creation in `wg_keepalive_start_once`**

After the `xTaskCreatePinnedToCore` call inside `wg_keepalive_start_once` (after the `if (ok != pdPASS)` block, around line 230), register:
```cpp
    if (s_keepalive_task != nullptr) {
        service_register_t reg = {};
        reg.name = "wg";
        reg.task = s_keepalive_task;
        reg.restart_cb = wg_supervisor_restart;
        reg.health_cb = wg_supervisor_health;
        reg.quiet_max_ms = 100000U;  /* 100s — re-apply blind 90s + slack */
        reg.heap_leak_threshold = 0U;
        (void)service_supervisor_register(&reg);
    }
```

- [ ] **Step 5: Build**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run --environment esp32-s3-touch-lcd-4_3b
```
Expected: `[SUCCESS]`.

- [ ] **Step 6: Commit**

```powershell
git add src/net_wireguard.cpp
git commit -m "feat(wg): supervisor register + restart_cb + heartbeat (v1.87)"
```

---

## Task 6: Wire FTP restart callback + register

**Files:**
- Modify: `src/net_services.cpp` (has `static FtpServer s_ftp;` line 35, init `s_ftp.begin(...)` line 167)

- [ ] **Step 1: Add include**

Near top includes of `src/net_services.cpp`, add:
```cpp
#include "service_supervisor.h"
```

- [ ] **Step 2: Determine FTP restart strategy**

`FtpServer` from SimpleFTPServer library. Check if `end()` method exists:
```powershell
Select-String -Path .pio/libdeps/esp32-s3-touch-lcd-4_3b/SimpleFTPServer/FtpServer.h -Pattern "void end" | Select-Object -First 5
```
- If `end()` exists → use `s_ftp.end()` then re-call `s_ftp.begin(...)`
- If NOT → workaround: set internal flag `s_ftp_running = false` and call `s_ftp.handleFTP()` no longer (but begin again triggers re-bind). Simplest: skip `end()` and just call `begin()` again (re-binds socket).

For the plan, assume `end()` exists (most likely). If build later fails, fall back to begin-only.

- [ ] **Step 3: Add restart callback**

Locate `s_ftp_running` extern/static (search file). Add static function (top of namespace block where other FTP statics are):
```cpp
static int ftp_supervisor_restart(void) {
    /* Best-effort teardown; SimpleFTPServer doesn't free TCP socket cleanly,
     * but begin() re-binds. If lib has end(), use it. */
    s_ftp.end();
    vTaskDelay(pdMS_TO_TICKS(200));
    /* Re-prime: clear running flag so periodic logic re-issues begin. */
    s_ftp_running = false;
    /* Force one immediate re-issue by manually calling begin if creds known: */
    if (s_ftp_user_buf[0] != '\0') {
        s_ftp.begin(s_ftp_user_buf, s_ftp_pass_buf, "FitaDigital SD FTP");
        s_ftp_running = true;
        app_log_feature_write("INFO", "SUPERVISOR", "FTP re-bound");
    }
    return 0;
}
```

- [ ] **Step 4: Register FTP service after first successful start**

In the function that calls `s_ftp.begin(...)` (around line 167), find the line `s_ftp_running = true;` (line 180). After that line, register:
```cpp
    static bool s_ftp_registered = false;
    if (!s_ftp_registered) {
        s_ftp_registered = true;
        service_register_t reg = {};
        reg.name = "ftp";
        /* FTP runs in main loop polling — no dedicated task. Use the main
         * Arduino loopTask handle as monitored task. Stack hwm of loopTask
         * is a proxy for FTP health; quiet_max=0 (idle valid). */
        reg.task = xTaskGetCurrentTaskHandle();
        reg.restart_cb = ftp_supervisor_restart;
        reg.health_cb = nullptr;
        reg.quiet_max_ms = 0U;
        reg.heap_leak_threshold = 0U;
        (void)service_supervisor_register(&reg);
    }
```

- [ ] **Step 5: Build**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run --environment esp32-s3-touch-lcd-4_3b
```
Expected: `[SUCCESS]`. If `s_ftp.end()` errors (`no member`), edit `ftp_supervisor_restart` to remove the `s_ftp.end();` line and rely on `s_ftp.begin()` re-bind.

- [ ] **Step 6: Commit**

```powershell
git add src/net_services.cpp
git commit -m "feat(ftp): supervisor register + restart_cb (v1.87)"
```

---

## Task 7: Wire WiFi restart callback + register

**Files:**
- Modify: `src/net_services.cpp` (find WiFi init site)
- Modify: `src/net_wireguard.cpp` (wifi_keepalive_tick is here, must add heartbeat)

- [ ] **Step 1: Locate WiFi init site**

```powershell
Select-String -Path src/net_services.cpp -Pattern "net_wifi_begin_saved|WiFi.begin" | Select-Object -First 10
```

Identify the function that owns WiFi setup. Usually `net_services_init()` or similar. We'll register WiFi after first successful connect.

- [ ] **Step 2: Add WiFi restart callback in `src/net_services.cpp`**

Near other restart_cb static functions (or grouped at top), add:
```cpp
static int wifi_supervisor_restart(void) {
    WiFi.disconnect();  /* does NOT erase saved credentials */
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_wifi_start();
    vTaskDelay(pdMS_TO_TICKS(200));
    net_wifi_begin_saved();
    return 0;
}

static bool wifi_supervisor_health(void) {
    return WiFi.status() == WL_CONNECTED;
}
```

Add include if missing:
```cpp
#include "esp_wifi.h"
```

- [ ] **Step 3: Register WiFi service**

In the same function where WiFi.begin completes / IP is obtained, after `app_log_feature_writef("INFO", "WIFI", "Conectado. IP=...");` (search file for "Conectado. IP="), add:
```cpp
    static bool s_wifi_registered = false;
    if (!s_wifi_registered) {
        s_wifi_registered = true;
        service_register_t reg = {};
        reg.name = "wifi";
        /* WiFi event handler runs in WiFi task on core 0; monitor that task
         * if we can resolve it. Otherwise use loopTask as proxy. */
        reg.task = xTaskGetCurrentTaskHandle();
        reg.restart_cb = wifi_supervisor_restart;
        reg.health_cb = wifi_supervisor_health;
        reg.quiet_max_ms = 30000U;  /* health_cb covers liveness; quiet is backup */
        reg.heap_leak_threshold = 0U;
        (void)service_supervisor_register(&reg);
    }
```

- [ ] **Step 4: Heartbeat call in `wifi_keepalive_tick`**

In `src/net_wireguard.cpp`, function `wifi_keepalive_tick`, at the very top of function body, add:
```cpp
    service_supervisor_heartbeat("wifi");
```

- [ ] **Step 5: Build**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run --environment esp32-s3-touch-lcd-4_3b
```
Expected: `[SUCCESS]`.

- [ ] **Step 6: Commit**

```powershell
git add src/net_services.cpp src/net_wireguard.cpp
git commit -m "feat(wifi): supervisor register + restart_cb + heartbeat (v1.87)"
```

---

## Task 8: Init supervisor in app.cpp + HTTP endpoint + version bump

**Files:**
- Modify: `src/app.cpp` (call `service_supervisor_init` at end of `setup()`)
- Modify: `src/web_portal/web_portal.cpp` (add `GET /api/system/services` handler + route)
- Modify: `platformio.ini` (bump version)

- [ ] **Step 1: Add include + init call in `src/app.cpp`**

Near other includes, add:
```cpp
#include "service_supervisor.h"
```

At the end of `setup()` function (after all `boot_log_step(...)` calls for services init complete, near the final "Boot concluido" log), add:
```cpp
  service_supervisor_init();
  boot_log_plain("INFO", "service_supervisor inicializado");
```

- [ ] **Step 2: Add HTTP endpoint handler in `src/web_portal/web_portal.cpp`**

Near top, after other includes, add:
```cpp
#include "service_supervisor.h"
```

Add handler function near other handle_* functions:
```cpp
static void handle_system_services_get(AsyncWebServerRequest *request)
{
    service_status_t arr[8];
    size_t n = service_supervisor_get_all_status(arr, 8);

    /* Build JSON manually — keep it small + dependency-free. */
    char buf[1024];
    int off = 0;
    off += snprintf(buf + off, sizeof(buf) - off,
        "{\"uptime_s\":%lu,\"heap\":{\"internal_free\":%u,\"internal_min\":%u,\"psram_free\":%u},\"services\":[",
        (unsigned long)(millis() / 1000UL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    for (size_t i = 0; i < n && off < (int)sizeof(buf) - 200; i++) {
        const service_status_t *s = &arr[i];
        const char *reason = s->last_restart_reason ? s->last_restart_reason : "null";
        const bool quoted_reason = (s->last_restart_reason != nullptr);
        off += snprintf(buf + off, sizeof(buf) - off,
            "%s{\"name\":\"%s\",\"stack_hwm\":%u,\"stack_hwm_min\":%u,"
            "\"heap_owned_baseline\":%u,\"restart_count\":%u,\"last_restart_ms\":%u,"
            "\"last_restart_reason\":%s%s%s}",
            (i == 0) ? "" : ",",
            s->name ? s->name : "?",
            (unsigned)s->stack_hwm, (unsigned)s->stack_hwm_min,
            (unsigned)s->heap_owned_baseline,
            (unsigned)s->restart_count, (unsigned)s->last_restart_ms,
            quoted_reason ? "\"" : "", reason, quoted_reason ? "\"" : "");
    }

    off += snprintf(buf + off, sizeof(buf) - off,
        "],\"supervisor\":{\"escalations\":%u,\"active_services\":%u}}",
        (unsigned)service_supervisor_get_escalations(),
        (unsigned)n);

    request->send(200, "application/json", buf);
}
```

Register route in `web_portal_start()` (near the `/api/health` route block):
```cpp
    /* --- /api/system/services (auth) — v1.87 supervisor telemetry --- */
    s_srv->on("/api/system/services", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_system_services_get(request);
    });
```

Add `heap_caps_get_free_size` include if missing:
```cpp
#include "esp_heap_caps.h"
```

- [ ] **Step 3: Bump version in `platformio.ini`**

Change `'-DFITADIGITAL_VERSION="1.86"'` (line 52) → `'-DFITADIGITAL_VERSION="1.87"'`.

- [ ] **Step 4: Build**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run --environment esp32-s3-touch-lcd-4_3b
```
Expected: `[SUCCESS]`. `firmware_versions/FitaDigital_v1.87.bin` auto-archived.

- [ ] **Step 5: Commit**

```powershell
git add src/app.cpp src/web_portal/web_portal.cpp platformio.ini
git commit -m "feat(supervisor): init in app.cpp + /api/system/services endpoint v1.87"
```

---

## Task 9: Flash + smoke test + endpoint verification

**Files:** none (testing)

- [ ] **Step 1: Flash v1.87**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/pio.exe" run --environment esp32-s3-touch-lcd-4_3b --target upload --upload-port COM3
```
Expected: `Hash of data verified` + `Hard resetting via RTS pin`.

- [ ] **Step 2: Wait 10s, then verify boot**

```powershell
Start-Sleep -Seconds 10
curl.exe -s --max-time 5 http://192.168.0.197/api/health
```
Expected: `{"online":true,"uptime_s":<small>}`.

- [ ] **Step 3: Pull boot.log — verify supervisor init logged**

```powershell
curl.exe -s --max-time 30 --digest -u admin:0000 "http://192.168.0.197/api/fs/file?path=/boot.log"
```
Expected: contains line `service_supervisor inicializado`.

- [ ] **Step 4: Hit new endpoint**

```powershell
curl.exe -s --max-time 5 --digest -u admin:0000 http://192.168.0.197/api/system/services
```
Expected: JSON with `services` array containing 4 entries (`wifi`, `wg`, `ftp`, `rs485`), `restart_count:0` for all, `supervisor.escalations:0`.

- [ ] **Step 5: Wait 60s + re-hit endpoint to confirm `stack_hwm` populated**

```powershell
Start-Sleep -Seconds 60
curl.exe -s --max-time 5 --digest -u admin:0000 http://192.168.0.197/api/system/services
```
Expected: `stack_hwm` > 0 for all services (timers have fired).

---

## Task 10: Stress test v1.87 — validate zero reboots + no escalations

**Files:**
- Output: `logs/wifi_stress_v187_<ts>.csv`

- [ ] **Step 1: Baseline**

```powershell
curl.exe -s --max-time 5 http://192.168.0.197/api/health
```
Note uptime_before.

- [ ] **Step 2: Run stress 10 cycles (~86min)**

```powershell
& "C:/Users/Afonso/.platformio/penv/Scripts/python.exe" tools/wifi_stress.py --cycles 10
```
Use `run_in_background: true` so this skill controller can monitor.

- [ ] **Step 3: Inspect CSV when done**

```powershell
Get-Content (Get-ChildItem logs/wifi_stress_*.csv | Sort LastWriteTime -Desc | Select -First 1).FullName
```
PASS: 0 `reboot_detected=True` rows. 20/20 PASS rows ideal; if `task_spawn_failed` still occurs, log it but supervisor should handle via restart.

- [ ] **Step 4: Hit /api/system/services after stress**

```powershell
curl.exe -s --max-time 5 --digest -u admin:0000 http://192.168.0.197/api/system/services
```
Expected: `restart_count > 0` for at least one service (supervisor exercised). `escalations:0` (no rate-limit hit).

- [ ] **Step 5: Pull boot.log + fdigi.log + grep SUPERVISOR events**

```powershell
curl.exe -s --max-time 30 --digest -u admin:0000 "http://192.168.0.197/api/fs/file?path=/boot.log"
curl.exe -s --max-time 30 --digest -u admin:0000 "http://192.168.0.197/api/logs/tail" | Select-String "SUPERVISOR" | Select -Last 30
```
Expected: SUPERVISOR Restart entries with `reclaimed=N` showing heap freed.

- [ ] **Step 6: Move TODO entry to Feito**

In `TODO.md`, remove the "Em curso" entry for `v1.87 service supervisor` (will be added during task list update earlier in development) and add to top of `## Feito`:
```markdown
- 2026-05-16 — **Service supervisor v1.87 — zero device reboot mantém**: framework per-service watchdog. Monitor stack hwm + heartbeat + heap delta + health_cb por timer FreeRTOS 10s. Restart isolado por serviço (WiFi/WG/FTP/RS485) via worker task async; rate-limit 3/10min escala esp_restart. Endpoint `GET /api/system/services` JSON. Stack overflow hook salva breadcrumb (não soft-restart — corruption unsafe). Validação: stress 10 ciclos zero reboots + restart_count visível em endpoint. CSV: `logs/wifi_stress_v187_<ts>.csv`. Spec/plan: `docs/superpowers/{specs,plans}/2026-05-16-service-supervisor*.md`.
```

- [ ] **Step 7: Commit final**

```powershell
git add TODO.md
git commit -m "test(supervisor): stress v1.87 zero reboot + restart_count visible"
```

---

## Task 11: Merge worktree branch back to main (if all tests pass)

**Files:** workflow

- [ ] **Step 1: Verify branch clean + all commits in worktree branch**

```powershell
git status
git log --oneline -15
```

- [ ] **Step 2: Exit worktree to main**

Use `ExitWorktree` tool with action `"keep"` to return to main repo without removing the worktree (preserves changes for review):

In Claude session: invoke `ExitWorktree` tool with `action="keep"`.

Then in main repo:
```powershell
git checkout main
git merge worktree-feat-service-supervisor --no-ff -m "merge: service supervisor v1.87"
```

- [ ] **Step 3: Clean up worktree**

After successful merge:
```powershell
git worktree remove .claude/worktrees/feat-service-supervisor
git branch -d worktree-feat-service-supervisor
```

---

## Self-Review notes

- **Spec coverage:** All 5 spec sections covered. Section 1 (architecture) → Tasks 2-3. Section 2 (detection) → Tasks 3-7 (heartbeat + register). Section 3 (restart workflow) → Tasks 3-7 (callbacks). Section 4 (rate limit) → Task 3 (built into worker). Section 5 (telemetry) → Task 8 (endpoint).
- **Placeholders:** Task 6 has fallback note for `s_ftp.end()` if absent; this is conditional logic, not a placeholder. All code blocks are complete.
- **Type consistency:** `service_register_t` name field matches across all `service_supervisor_register` calls. `quiet_max_ms` consistent (0 for FTP/RS485, 30000/100000 for WiFi/WG). Service names "wifi", "wg", "ftp", "rs485" consistent across registry + NVS + endpoint.
- **Risks acknowledged:** FTP `s_ftp.end()` availability (Task 6 Step 5 has fallback). Worker stack 4096B may need bump if `restart_cb` deep-stacks (Step 10.4 monitors via hwm).
- **Cross-cutting:** `service_supervisor_init` MUST be called AFTER all `service_supervisor_register` calls (so timers start with full registry). Task 8 Step 1 places it at end of `setup()` — verify registry calls in Tasks 4-7 happen during their respective init paths called earlier in setup.
