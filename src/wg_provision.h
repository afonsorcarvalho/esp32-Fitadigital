/**
 * @file wg_provision.h
 * @brief WireGuard enrollment: keygen Curve25519 + POST /api/enroll + poll + apply.
 */
#pragma once
#include <stdint.h>

enum class WgProvState : uint8_t {
    IDLE,
    KEYGEN,
    ENROLLING,
    SHOWING_QR,
    APPLYING,
    ENROLLED,
    ERROR,
};

struct WgProvStatus {
    WgProvState state;
    char activation_code[16];
    char activation_url[256];
    uint32_t expires_at_ms;  /**< millis() deadline for QR; 0 if not set. */
    char error_msg[80];
};

/**
 * Start enrollment in a background FreeRTOS task.
 * server_url: base URL of the enrollment backend, e.g. "http://192.168.1.10:5000".
 * Safe to call again after a previous run finished (ENROLLED or ERROR).
 */
void wg_provision_start(const char *server_url);

/**
 * Signal the provision task to stop. Returns immediately; task may still be running
 * briefly. State transitions to IDLE once the task exits.
 */
void wg_provision_cancel(void);

/** Thread-safe snapshot of the current provision status. */
void wg_provision_get_status(WgProvStatus *out);
