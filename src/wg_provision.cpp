/**
 * @file wg_provision.cpp
 * @brief WireGuard enrollment state machine (FreeRTOS task).
 *
 * Flow: KEYGEN → ENROLLING → SHOWING_QR (polling) → APPLYING → ENROLLED
 *
 * Private key never leaves device: generated locally, saved to NVS, never
 * sent to any server. Only the public key is transmitted in POST /api/enroll.
 *
 * Key format: Curve25519 via mbedTLS, exported as little-endian 32 bytes then
 * standard base64 (same format as `wg genkey` / `wg pubkey`).
 * Note: mbedTLS MPI is big-endian internally → bytes are reversed on export.
 */
#include "wg_provision.h"
#include "app_log.h"
#include "app_settings.h"
#include "net_wireguard.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/ecp.h>
#include <mbedtls/platform_util.h>
#include <string.h>

static const char *TAG = "WG_PROV";

/* ── shared state ─────────────────────────────────────────────────────────── */

static SemaphoreHandle_t s_mutex   = nullptr;
static WgProvStatus      s_status  = {};
static TaskHandle_t      s_task    = nullptr;
static volatile bool     s_cancel  = false;

static char s_server_url[128] = {};
/* Private key survives until APPLYING so net_wireguard_apply can save it. */
static char s_priv_b64[48]   = {};
static char s_pub_b64[48]    = {};

/* ── helpers ──────────────────────────────────────────────────────────────── */

static void set_state(WgProvState st, const char *err = nullptr) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.state = st;
    if (err) {
        strncpy(s_status.error_msg, err, sizeof(s_status.error_msg) - 1);
        s_status.error_msg[sizeof(s_status.error_msg) - 1] = '\0';
    }
    xSemaphoreGive(s_mutex);
    Serial.printf("[%s] state -> %d%s%s\n", TAG, (int)st, err ? " err=" : "", err ? err : "");
}

static bool cancelled() { return s_cancel; }

static void device_id(char *out, size_t sz) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, sz, "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ── Curve25519 keygen ────────────────────────────────────────────────────── */

/*
 * Thin RNG wrapper around ESP32 hardware TRNG.
 * Avoids mbedtls_entropy_context + mbedtls_ctr_drbg_context (~4 KB combined)
 * on the task stack, which was overflowing into the heap and corrupting Wi-Fi
 * internal buffers. esp_fill_random() uses the same hardware source as mbedTLS
 * entropy would, without the overhead.
 */
static int esp32_trng(void * /*ctx*/, uint8_t *buf, size_t len) {
    esp_fill_random(buf, len);
    return 0;
}

static bool keygen(char *priv_out, size_t priv_sz, char *pub_out, size_t pub_sz) {
    mbedtls_ecp_group grp;
    mbedtls_mpi       d;
    mbedtls_ecp_point Q;

    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&Q);

    uint8_t priv_be[32] = {};
    uint8_t pub_be[32]  = {};
    uint8_t priv_le[32] = {};
    uint8_t pub_le[32]  = {};
    bool ok = false;

    do {
        if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) != 0) break;
        /* mbedtls_ecp_gen_keypair applies RFC-7748 clamping for Curve25519. */
        if (mbedtls_ecp_gen_keypair(&grp, &d, &Q, esp32_trng, nullptr) != 0) break;

        /* mbedTLS MPI is big-endian; WireGuard/RFC-7748 keys are little-endian. */
        if (mbedtls_mpi_write_binary(&d, priv_be, 32) != 0) break;
        if (mbedtls_mpi_write_binary(&Q.MBEDTLS_PRIVATE(X), pub_be, 32) != 0) break;
        for (int i = 0; i < 32; i++) {
            priv_le[i] = priv_be[31 - i];
            pub_le[i]  = pub_be[31 - i];
        }

        size_t olen;
        if (mbedtls_base64_encode((uint8_t *)priv_out, priv_sz, &olen, priv_le, 32) != 0) break;
        priv_out[olen] = '\0';
        if (mbedtls_base64_encode((uint8_t *)pub_out, pub_sz, &olen, pub_le, 32) != 0) break;
        pub_out[olen] = '\0';
        ok = true;
    } while (false);

    mbedtls_platform_zeroize(priv_be, 32);
    mbedtls_platform_zeroize(priv_le, 32);
    mbedtls_ecp_group_free(&grp);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&Q);
    return ok;
}

/* ── HTTP helpers ─────────────────────────────────────────────────────────── */

static int http_post(const char *url, const char *body, String &resp_out) {
    HTTPClient http;
    WiFiClient client;
    if (!http.begin(client, url)) return -1;
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);
    int code = http.POST((uint8_t *)body, strlen(body));
    if (code > 0) resp_out = http.getString();
    http.end();
    return code;
}

static int http_get(const char *url, String &resp_out) {
    HTTPClient http;
    WiFiClient client;
    if (!http.begin(client, url)) return -1;
    http.setTimeout(10000);
    int code = http.GET();
    if (code > 0) resp_out = http.getString();
    http.end();
    return code;
}

/* ── enrollment steps ─────────────────────────────────────────────────────── */

static bool do_enroll(const char *pub_b64) {
    char dev_id[13];
    device_id(dev_id, sizeof(dev_id));

    JsonDocument req;
    req["device_id"]  = dev_id;
    req["public_key"] = pub_b64;
    String body;
    serializeJson(req, body);

    char url[160];
    snprintf(url, sizeof(url), "%s/api/enroll", s_server_url);

    String resp;
    const int code = http_post(url, body.c_str(), resp);
    if (code != 200) {
        Serial.printf("[%s] enroll HTTP %d\n", TAG, code);
        return false;
    }

    JsonDocument rdoc;
    if (deserializeJson(rdoc, resp) != DeserializationError::Ok) return false;
    const char *act_code = rdoc["activation_code"] | "";
    const char *act_url  = rdoc["activation_url"]  | "";
    if (!act_code[0] || !act_url[0]) return false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(s_status.activation_code, act_code, sizeof(s_status.activation_code) - 1);
    strncpy(s_status.activation_url,  act_url,  sizeof(s_status.activation_url)  - 1);
    s_status.expires_at_ms = millis() + 600000UL; /* 10 min */
    xSemaphoreGive(s_mutex);

    Serial.printf("[%s] enrolled code=%s\n", TAG, act_code);
    return true;
}

/* Returns: 1=got config (written to NVS), 0=pending, -1=expired/-2=error */
static int do_poll(const char *code) {
    char url[200];
    snprintf(url, sizeof(url), "%s/api/enroll/status/%s", s_server_url, code);

    String resp;
    const int http_code = http_get(url, resp);

    if (http_code == 204) return 0; /* pending */

    if (http_code == 410) {
        Serial.printf("[%s] poll 410 expired\n", TAG);
        return -1;
    }

    if (http_code != 200) {
        Serial.printf("[%s] poll HTTP %d\n", TAG, http_code);
        return -2;
    }

    /* 200: received WireGuard config */
    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return -2;

    const char *addr   = doc["address"]           | "";
    const char *srv_pk = doc["server_public_key"]  | "";
    const char *ep     = doc["server_endpoint"]    | "";
    const char *dns    = doc["dns"]                | "";
    const char *aips   = doc["allowed_ips"]        | "";

    if (!addr[0] || !srv_pk[0] || !ep[0]) return -2;

    /* Strip CIDR suffix from address for NVS (net_wireguard_apply uses IPAddress::fromString). */
    char ip_only[24] = {};
    const char *slash = strchr(addr, '/');
    if (slash) {
        size_t n = (size_t)(slash - addr);
        if (n < sizeof(ip_only)) {
            memcpy(ip_only, addr, n);
            ip_only[n] = '\0';
        }
    } else {
        strncpy(ip_only, addr, sizeof(ip_only) - 1);
    }

    /* Parse endpoint "host:port". */
    char ep_host[100] = {};
    uint16_t ep_port  = 51820;
    const char *colon = strrchr(ep, ':');
    if (colon) {
        size_t n = (size_t)(colon - ep);
        if (n < sizeof(ep_host)) {
            memcpy(ep_host, ep, n);
            ep_host[n] = '\0';
        }
        ep_port = (uint16_t)strtoul(colon + 1, nullptr, 10);
        if (ep_port == 0) ep_port = 51820;
    } else {
        strncpy(ep_host, ep, sizeof(ep_host) - 1);
    }

    app_settings_set_wg_local_ip(ip_only);
    app_settings_set_wg_private_key(s_priv_b64);
    app_settings_set_wg_peer_public_key(srv_pk);
    app_settings_set_wg_endpoint(ep_host);
    app_settings_set_wg_port(ep_port);
    app_settings_set_wireguard_enabled(true);

    Serial.printf("[%s] config saved ip=%s ep=%s:%u\n", TAG, ip_only, ep_host, ep_port);
    (void)dns; (void)aips; /* stored in WG config server-side */
    return 1;
}

/* ── task ─────────────────────────────────────────────────────────────────── */

static const uint32_t kBackoffMs[] = { 3000, 6000, 12000, 30000 };
static const size_t   kBackoffLen  = sizeof(kBackoffMs) / sizeof(kBackoffMs[0]);

static void provision_task(void * /*arg*/) {
    Serial.printf("[%s] task started\n", TAG);

    /* KEYGEN */
    set_state(WgProvState::KEYGEN);
    if (!keygen(s_priv_b64, sizeof(s_priv_b64), s_pub_b64, sizeof(s_pub_b64))) {
        set_state(WgProvState::ERROR, "Falha na geracao de chaves");
        goto done;
    }
    if (cancelled()) goto done;

    /* ENROLLING */
    set_state(WgProvState::ENROLLING);
    if (!do_enroll(s_pub_b64)) {
        set_state(WgProvState::ERROR, "Erro ao registar no servidor");
        goto done;
    }
    if (cancelled()) goto done;

    /* SHOWING_QR — poll with backoff until activated/expired/cancelled */
    {
        set_state(WgProvState::SHOWING_QR);

        char code[16];
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        strncpy(code, s_status.activation_code, sizeof(code) - 1);
        const uint32_t deadline = s_status.expires_at_ms;
        xSemaphoreGive(s_mutex);

        size_t bi = 0;
        bool done_poll = false;
        while (!cancelled() && millis() < deadline && !done_poll) {
            const int r = do_poll(code);
            if (r == 1) {
                done_poll = true;
                break;
            } else if (r == -1) {
                set_state(WgProvState::ERROR, "Codigo expirado, tente novamente");
                goto done;
            } else if (r == -2) {
                set_state(WgProvState::ERROR, "Erro de comunicacao com servidor");
                goto done;
            }
            /* 0 = pending: small yield for Wi-Fi stack, then backoff */
            vTaskDelay(pdMS_TO_TICKS(200));
            const uint32_t wait = kBackoffMs[bi < kBackoffLen - 1 ? bi++ : kBackoffLen - 1];
            const uint32_t t0   = millis();
            while (millis() - t0 < wait && !cancelled()) {
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }

        if (cancelled()) goto done;
        if (!done_poll) {
            set_state(WgProvState::ERROR, "Tempo de espera esgotado");
            goto done;
        }
    }

    /* APPLYING */
    set_state(WgProvState::APPLYING);
    if (cancelled()) goto done;
    net_wireguard_apply();

    /* ENROLLED */
    set_state(WgProvState::ENROLLED);
    mbedtls_platform_zeroize(s_priv_b64, sizeof(s_priv_b64));

done:
    if (cancelled() && s_status.state != WgProvState::ENROLLED) {
        mbedtls_platform_zeroize(s_priv_b64, sizeof(s_priv_b64));
        set_state(WgProvState::IDLE);
    }
    s_task   = nullptr;
    s_cancel = false;
    Serial.printf("[%s] task done, final state=%d\n", TAG, (int)s_status.state);
    vTaskDelete(nullptr);
}

/* ── public API ───────────────────────────────────────────────────────────── */

void wg_provision_start(const char *server_url) {
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
    }
    /* Cancel any previous task. */
    if (s_task != nullptr) {
        s_cancel = true;
        uint8_t retries = 20;
        while (s_task != nullptr && retries-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    s_cancel = false;
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = WgProvState::IDLE;
    strncpy(s_server_url, server_url ? server_url : "", sizeof(s_server_url) - 1);

    /* 16 KB: mbedTLS entropy+DRBG+ECP contexts together exceed 8 KB on ESP32. */
    xTaskCreatePinnedToCore(provision_task, "wg_prov", 16384, nullptr, 5, &s_task, 0);
}

void wg_provision_cancel(void) {
    s_cancel = true;
}

void wg_provision_get_status(WgProvStatus *out) {
    if (out == nullptr) return;
    if (s_mutex == nullptr) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_mutex);
}
