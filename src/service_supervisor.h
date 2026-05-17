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
