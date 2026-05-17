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
    /* v1.88: removed stack_leak_rapid heuristic. uxTaskGetStackHighWaterMark
     * is monotonic-decreasing (tracks min observed), so any deep call stack
     * triggers a diff check. False positive observed v1.87 (WG hwm dropped
     * 7536->7232 = 1.2KB, triggered restart). Real leak detection requires
     * malloc-tag tracking which IDF doesn't support. Rely on stack_critical
     * absolute threshold (<64 words = ~256B left) instead. */
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
