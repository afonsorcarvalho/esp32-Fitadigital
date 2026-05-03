/**
 * @file web_portal.cpp
 * @brief Servidor HTTP: / (UI), /api/settings, /api/logs, /api/fs/*, WebSocket /ws/logs.
 *        Fase A: auth Basic em todos os handlers excepto /api/health.
 *        Fase B: endpoints novos /api/settings/{rs485,mqtt,ui,net,pin},
 *                /api/system/{reboot,export,import,status}, /api/health.
 */
#include "web_portal.h"
#include "web_portal_html.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <Update.h>
#include <WiFi.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>

#include "app_log.h"
#include "app_settings.h"
#include "net_mqtt.h"
#include "net_services.h"
#include "ota_manager.h"
#include "sd_access.h"

static const char *TAG = "web_portal";

static constexpr uint16_t kHttpPort = 80;
static constexpr size_t kLogLineQ = 24;
static constexpr size_t kLogLineMax = 384;
/** Ficheiros ate este tamanho: leitura unica para RAM + String (como antes). */
static constexpr size_t kPortalFsMaxFileBytes = 512U * 1024U;
/** Download HTTP acima de kPortalFsMaxFileBytes: streaming por callback (sem bufferizar o ficheiro inteiro). */
static constexpr size_t kPortalFsStreamMaxFileBytes = 32U * 1024U * 1024U;

static AsyncWebServer *s_srv = nullptr;
static AsyncWebSocket *s_ws_logs = nullptr;

struct LogLineMsg {
    char line[kLogLineMax];
};

static QueueHandle_t s_log_queue = nullptr;
static TaskHandle_t s_log_task = nullptr;

/* ------------------------------------------------------------------ */
/* Auth                                                                  */
/* ------------------------------------------------------------------ */

/**
 * Verifica Basic Auth (admin:<PIN do device>).
 * @return true se autenticado, false se rejeitado (resposta 401 ja enviada).
 */
static bool web_auth_check(AsyncWebServerRequest *req)
{
    String pin = app_settings_settings_pin();
    if (!req->authenticate("admin", pin.c_str())) {
        req->requestAuthentication("FitaDigital");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Infraestrutura de log / WS                                            */
/* ------------------------------------------------------------------ */

static void log_notify_cb(const char *line, void * /*user*/)
{
    if (s_log_queue == nullptr || line == nullptr) {
        return;
    }
    LogLineMsg msg;
    strncpy(msg.line, line, sizeof(msg.line) - 1U);
    msg.line[sizeof(msg.line) - 1U] = '\0';
    (void)xQueueSend(s_log_queue, &msg, 0);
}

static void log_forward_task(void * /*arg*/)
{
    LogLineMsg msg;
    for (;;) {
        if (xQueueReceive(s_log_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (s_ws_logs != nullptr && s_ws_logs->count() > 0) {
            s_ws_logs->textAll(msg.line);
        }
    }
}

static bool fs_path_ok(const char *path)
{
    if (path == nullptr || path[0] != '/') {
        return false;
    }
    if (strstr(path, "..") != nullptr) {
        return false;
    }
    return true;
}

/**
 * Argumento ?path= na query string: em alguns builds do AsyncWebServer `getParam("path")`
 * sem `post=false` nao encontra o parametro e a lista de ficheiros falha no browser.
 */
static String path_param_from_request(AsyncWebServerRequest *request)
{
    if (request == nullptr) {
        return String();
    }
    if (request->hasParam("path", false)) {
        const AsyncWebParameter *pp = request->getParam("path", false);
        if (pp != nullptr) {
            return pp->value();
        }
    }
    if (request->hasParam("path", true)) {
        const AsyncWebParameter *pp = request->getParam("path", true);
        if (pp != nullptr) {
            return pp->value();
        }
    }
    const size_t n = request->params();
    for (size_t i = 0; i < n; i++) {
        const AsyncWebParameter *pp = request->getParam(i);
        if (pp != nullptr && pp->name() == "path") {
            return pp->value();
        }
    }
    return String();
}

/* ------------------------------------------------------------------ */
/* Helper body accumulator para POST com body JSON                       */
/* ------------------------------------------------------------------ */

/**
 * Acumula body de um POST; retorna true quando completo e devolve JSON em out_doc.
 * Usa static local — nao e' reentrante mas AsyncWebServer serializa por handler.
 */
static bool accumulate_body(uint8_t *data, size_t len, size_t index, size_t total, JsonDocument &out_doc)
{
    static String s_body;
    if (index == 0) {
        s_body = "";
        s_body.reserve(total < 2048U ? total : 2048U);
    }
    for (size_t i = 0; i < len; i++) {
        s_body += (char)data[i];
    }
    if (s_body.length() < total) {
        return false; /* incompleto */
    }
    const DeserializationError err = deserializeJson(out_doc, s_body);
    s_body = "";
    return !err;
}

/* ------------------------------------------------------------------ */
/* Handlers legacy /api/settings                                         */
/* ------------------------------------------------------------------ */

static void handle_settings_get(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["wifi"]["ssid"] = app_settings_wifi_ssid();
    doc["wifi"]["configured"] = app_settings_wifi_configured();
    doc["wifi"]["hasPassword"] = (app_settings_wifi_pass().length() > 0);
    doc["fontIndex"] = app_settings_font_index();
    doc["ftp"]["user"] = app_settings_ftp_user();
    doc["ftp"]["hasPassword"] = (app_settings_ftp_pass().length() > 0);
    doc["ntp"]["enabled"] = app_settings_ntp_enabled();
    doc["ntp"]["server"] = app_settings_ntp_server();
    doc["tzOffsetSec"] = app_settings_tz_offset_sec();
    doc["wireguard"]["enabled"] = app_settings_wireguard_enabled();
    doc["wireguard"]["localIp"] = app_settings_wg_local_ip();
    doc["wireguard"]["privateKey"] = app_settings_wg_private_key();
    doc["wireguard"]["peerPublicKey"] = app_settings_wg_peer_public_key();
    doc["wireguard"]["endpoint"] = app_settings_wg_endpoint();
    doc["wireguard"]["port"] = app_settings_wg_port();
    doc["status"]["ip"] = WiFi.localIP().toString();
    doc["status"]["sd"] = sd_access_is_mounted();

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

static void handle_settings_body(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                                 size_t total)
{
    static String body;
    if (index == 0) {
        body = "";
        body.reserve(total);
    }
    for (size_t i = 0; i < len; i++) {
        body += (char)data[i];
    }
    if (body.length() < total) {
        return;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, body);
    if (err) {
        request->send(400, "application/json", "{\"error\":\"json\"}");
        body = "";
        return;
    }

    if (!doc["wifi"].isNull()) {
        const char *ssid = doc["wifi"]["ssid"] | "";
        const char *pw = doc["wifi"]["password"] | "";
        const String olds = app_settings_wifi_ssid();
        const String oldp = app_settings_wifi_pass();
        if (strlen(pw) > 0) {
            app_settings_set_wifi(ssid, pw);
            net_wifi_begin(app_settings_wifi_ssid().c_str(), app_settings_wifi_pass().c_str());
        } else if (strcmp(ssid, olds.c_str()) != 0) {
            app_settings_set_wifi(ssid, oldp.c_str());
            net_wifi_begin(app_settings_wifi_ssid().c_str(), app_settings_wifi_pass().c_str());
        }
    }

    if (!doc["fontIndex"].isNull()) {
        app_settings_set_font_index((uint8_t)(int)doc["fontIndex"]);
    }

    if (!doc["ftp"].isNull()) {
        const char *fu = doc["ftp"]["user"] | "";
        const char *fp = doc["ftp"]["password"] | "";
        String cu = app_settings_ftp_user();
        String cp = app_settings_ftp_pass();
        if (strlen(fp) > 0) {
            app_settings_set_ftp(fu, fp);
        } else {
            app_settings_set_ftp(strlen(fu) > 0 ? fu : cu.c_str(), cp.c_str());
        }
        net_services_ftp_restart();
    }

    if (!doc["ntp"].isNull()) {
        app_settings_set_ntp_enabled((bool)doc["ntp"]["enabled"]);
        const char *srv = doc["ntp"]["server"] | "";
        if (strlen(srv) > 0) {
            app_settings_set_ntp_server(srv);
        }
    }

    if (!doc["tzOffsetSec"].isNull()) {
        app_settings_set_tz_offset_sec((int32_t)(int)doc["tzOffsetSec"]);
    }

    if (!doc["wireguard"].isNull()) {
        JsonObject wg = doc["wireguard"];
        app_settings_set_wireguard_enabled((bool)wg["enabled"]);
        const char *ip = wg["localIp"] | "";
        if (strlen(ip) > 0) {
            app_settings_set_wg_local_ip(ip);
        }
        const char *pk = wg["privateKey"] | "";
        if (strlen(pk) > 0) {
            app_settings_set_wg_private_key(pk);
        }
        const char *pub = wg["peerPublicKey"] | "";
        if (strlen(pub) > 0) {
            app_settings_set_wg_peer_public_key(pub);
        }
        const char *ep = wg["endpoint"] | "";
        if (strlen(ep) > 0) {
            app_settings_set_wg_endpoint(ep);
        }
        if (!wg["port"].isNull()) {
            app_settings_set_wg_port((uint16_t)(int)wg["port"]);
        }
    }

    body = "";
    request->send(200, "application/json", "{\"ok\":true}");
}

/* ------------------------------------------------------------------ */
/* Handlers de log                                                        */
/* ------------------------------------------------------------------ */

/** Leitura do tail do log: buffer grande nao pode ficar na pilha da tarefa async_tcp (~8 KiB). */
static constexpr size_t kLogTailHeapBytes = 16384U;

static void handle_logs_tail(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    char *buf = static_cast<char *>(heap_caps_malloc(kLogTailHeapBytes, MALLOC_CAP_8BIT));
    if (buf == nullptr) {
        doc["text"] = "";
        doc["truncated"] = false;
        doc["fileSize"] = 0;
        doc["note"] = "sem memoria para buffer de log.";
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
        return;
    }
    size_t fsz = 0;
    bool trunc = false;
    bool ok_read = false;
    sd_access_sync_front([&]() { ok_read = app_log_read_tail(buf, kLogTailHeapBytes, &fsz, &trunc); });
    if (ok_read) {
        doc["text"] = buf;
        doc["truncated"] = trunc;
        doc["fileSize"] = fsz;
    } else {
        doc["text"] = "";
        doc["truncated"] = false;
        doc["fileSize"] = 0;
        doc["note"] = "SD indisponivel ou sem log.";
    }
    heap_caps_free(buf);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

static void handle_logs_delete(AsyncWebServerRequest *request)
{
    bool ok = false;
    sd_access_sync_front([&]() { ok = app_log_clear(); });
    request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

/* ------------------------------------------------------------------ */
/* Handlers de filesystem                                                 */
/* ------------------------------------------------------------------ */

static void handle_fs_list(AsyncWebServerRequest *request)
{
    const String p = path_param_from_request(request);
    if (p.length() == 0) {
        request->send(400, "application/json", "{\"error\":\"path\"}");
        return;
    }
    if (!fs_path_ok(p.c_str())) {
        request->send(400, "application/json", "{\"error\":\"invalid path\"}");
        return;
    }

    String out;
    int list_http = 200;
    sd_access_sync_front([&]() {
        if (SD.cardType() == CARD_NONE) {
            list_http = 503;
            out = "{\"entries\":[]}";
            return;
        }

        JsonDocument doc;
        JsonArray arr = doc["entries"].to<JsonArray>();

        File dir = SD.open(p.c_str());
        if (!dir || !dir.isDirectory()) {
            if (dir) {
                dir.close();
            }
            out = "{\"entries\":[]}";
            return;
        }
        for (;;) {
            File f = dir.openNextFile();
            if (!f) {
                break;
            }
            JsonObject e = arr.add<JsonObject>();
            e["name"] = f.name();
            e["dir"] = f.isDirectory();
            e["size"] = f.isDirectory() ? 0 : (int)f.size();
            f.close();
        }
        dir.close();

        serializeJson(doc, out);
    });

    request->send(list_http, "application/json", out);
}

static bool portal_parse_bytes_range(AsyncWebServerRequest *request, size_t file_sz, size_t *out_lo, size_t *out_hi,
                                     bool *client_sent_range)
{
    *client_sent_range = false;
    *out_lo = 0;
    if (file_sz == 0U) {
        *out_hi = 0;
        return true;
    }
    *out_hi = file_sz - 1U;
    if (request == nullptr) {
        return true;
    }
    const AsyncWebHeader *hr = request->getHeader("Range");
    if (hr == nullptr) {
        return true;
    }
    String spec = hr->value();
    spec.trim();
    if (!spec.startsWith("bytes=")) {
        return true;
    }
    spec = spec.substring(6);
    spec.trim();
    {
        const int comma = spec.indexOf(',');
        if (comma >= 0) {
            spec = spec.substring(0, comma);
            spec.trim();
        }
    }
    if (spec.length() == 0U || spec.charAt(0) == '-') {
        return false;
    }
    const int dash = spec.indexOf('-');
    if (dash < 0) {
        return false;
    }
    String s_lo = spec.substring(0, dash);
    String s_hi = spec.substring(dash + 1);
    s_lo.trim();
    s_hi.trim();
    if (s_lo.length() == 0U) {
        return false;
    }
    const size_t lo = static_cast<size_t>(s_lo.toInt());
    size_t hi = 0;
    if (s_hi.length() == 0U) {
        hi = file_sz - 1U;
    } else {
        hi = static_cast<size_t>(s_hi.toInt());
    }
    if (lo >= file_sz || hi >= file_sz || hi < lo) {
        return false;
    }
    *client_sent_range = true;
    *out_lo = lo;
    *out_hi = hi;
    return true;
}

static void portal_fs_set_download_name(AsyncWebServerResponse *resp, const String &vfs_path)
{
    if (resp == nullptr) {
        return;
    }
    const int slash = vfs_path.lastIndexOf('/');
    const char *fn = vfs_path.c_str();
    if (slash >= 0) {
        fn = vfs_path.c_str() + slash + 1;
    }
    const String cd = String("attachment; filename=\"") + fn + '"';
    resp->addHeader("Content-Disposition", cd.c_str(), false);
}

static void handle_fs_file(AsyncWebServerRequest *request)
{
    const String p = path_param_from_request(request);
    if (p.length() == 0) {
        request->send(400, "text/plain", "path");
        return;
    }
    if (!fs_path_ok(p.c_str())) {
        request->send(403, "text/plain", "forbidden");
        return;
    }

    uint8_t *heap_buf = nullptr;
    size_t heap_len = 0;
    int err = 0;
    bool use_stream = false;
    size_t stream_total = 0;

    sd_access_sync_front([&]() {
        if (SD.cardType() == CARD_NONE) {
            err = 1;
            return;
        }
        File test = SD.open(p.c_str(), FILE_READ);
        if (!test || test.isDirectory()) {
            if (test) {
                test.close();
            }
            err = 2;
            return;
        }
        const size_t sz = test.size();
        if (sz > kPortalFsStreamMaxFileBytes) {
            test.close();
            err = 3;
            return;
        }
        if (sz > kPortalFsMaxFileBytes) {
            test.close();
            stream_total = sz;
            use_stream = true;
            return;
        }
        heap_buf = static_cast<uint8_t *>(heap_caps_malloc(sz, MALLOC_CAP_8BIT));
        if (heap_buf == nullptr) {
            test.close();
            err = 2;
            return;
        }
        const size_t r = test.read(heap_buf, sz);
        test.close();
        if (r != sz) {
            heap_caps_free(heap_buf);
            heap_buf = nullptr;
            err = 2;
            return;
        }
        heap_len = sz;
    });

    if (err == 1) {
        request->send(503, "text/plain", "no sd");
        return;
    }
    if (err == 3) {
        request->send(413, "text/plain", "file too large");
        return;
    }
    if (use_stream) {
        size_t range_lo = 0;
        size_t range_hi = 0;
        bool client_range = false;
        if (!portal_parse_bytes_range(request, stream_total, &range_lo, &range_hi, &client_range)) {
            AsyncWebServerResponse *r416 = request->beginResponse(416, "text/plain", "range not satisfiable");
            if (r416 == nullptr) {
                request->send(416, "text/plain", "range not satisfiable");
                return;
            }
            {
                const String cr = String("bytes */") + String(static_cast<unsigned long>(stream_total));
                r416->addHeader("Content-Range", cr.c_str(), false);
            }
            request->send(r416);
            return;
        }
        const size_t out_len = range_hi - range_lo + 1U;
        const int http_code = client_range ? 206 : 200;
        const String path_copy = p;
        const size_t body_off = range_lo;
        const size_t body_len = out_len;
        AsyncWebServerResponse *resp = request->beginResponse(
            "application/octet-stream", body_len,
            [path_copy, body_off, body_len](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                if (index >= body_len || maxLen == 0U) {
                    return 0;
                }
                size_t want = body_len - index;
                if (want > maxLen) {
                    want = maxLen;
                }
                size_t read_once = 0;
                sd_access_sync_front([&]() {
                    File f = SD.open(path_copy.c_str(), FILE_READ);
                    if (!f || f.isDirectory()) {
                        return;
                    }
                    const size_t file_pos = body_off + index;
                    if (!f.seek(static_cast<uint32_t>(file_pos))) {
                        f.close();
                        return;
                    }
                    read_once = f.read(buffer, want);
                    f.close();
                });
                return read_once;
            });
        if (resp == nullptr) {
            request->send(500, "text/plain", "oom");
            return;
        }
        resp->setCode(http_code);
        if (http_code == 206) {
            const String cr = String("bytes ") + String(static_cast<unsigned long>(range_lo)) + '-' +
                              String(static_cast<unsigned long>(range_hi)) + '/' +
                              String(static_cast<unsigned long>(stream_total));
            resp->addHeader("Content-Range", cr.c_str(), false);
        }
        (void)resp->addHeader("Accept-Ranges", "bytes", true);
        portal_fs_set_download_name(resp, p);
        request->send(resp);
        return;
    }
    if (heap_buf == nullptr) {
        request->send(404, "text/plain", "not found");
        return;
    }

    String body;
    body.reserve(heap_len);
    body.concat(reinterpret_cast<const char *>(heap_buf), heap_len);
    heap_caps_free(heap_buf);
    request->send(200, "application/octet-stream", body);
}

/* ------------------------------------------------------------------ */
/* WebSocket de logs                                                      */
/* ------------------------------------------------------------------ */

static void on_ws_logs_event(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg,
                             uint8_t *data, size_t len)
{
    (void)server;
    (void)arg;
    (void)data;
    (void)len;
    if (type == WS_EVT_CONNECT) {
        char *buf = static_cast<char *>(heap_caps_malloc(kLogTailHeapBytes, MALLOC_CAP_8BIT));
        if (buf != nullptr) {
            size_t fsz = 0;
            bool trunc = false;
            bool ok_read = false;
            sd_access_sync_front(
                [&]() { ok_read = app_log_read_tail(buf, kLogTailHeapBytes, &fsz, &trunc); });
            if (ok_read) {
                client->text(buf);
            }
            heap_caps_free(buf);
        }
    }
}

/* ------------------------------------------------------------------ */
/* OTA                                                                    */
/* ------------------------------------------------------------------ */

static void handle_ota_upload_request(AsyncWebServerRequest *request)
{
    if (Update.hasError()) {
        String err = Update.errorString();
        app_log_writef("ERROR", "OTA HTTP: resposta erro: %s", err.c_str());
        ota_http_set_error(err.c_str());
        request->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"" + err + "\"}");
    } else {
        app_log_writef("INFO", "OTA HTTP: flash concluido, reboot agendado no LVGL timer");
        ota_http_set_done();
        request->send(200, "application/json", "{\"ok\":true}");
    }
}

static void handle_ota_upload_data(AsyncWebServerRequest *request, const String &filename,
                                   size_t index, uint8_t *data, size_t len, bool final)
{
    if (index == 0) {
        app_log_writef("INFO", "OTA HTTP: inicio '%s'", filename.c_str());
        ota_http_begin();
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            app_log_writef("ERROR", "OTA HTTP: begin falhou: %s", Update.errorString());
            ota_http_set_error(Update.errorString());
            return;
        }
    }
    if (Update.hasError()) {
        return;
    }
    if (Update.write(data, len) != len) {
        app_log_writef("ERROR", "OTA HTTP: write falhou: %s", Update.errorString());
        ota_http_set_error(Update.errorString());
        return;
    }
    if (final) {
        if (Update.end(true)) {
            app_log_writef("INFO", "OTA HTTP: concluido %u bytes", (unsigned)(index + len));
            ota_http_set_done();
        } else {
            app_log_writef("ERROR", "OTA HTTP: end falhou: %s", Update.errorString());
            ota_http_set_error(Update.errorString());
        }
    }
}

/* ------------------------------------------------------------------ */
/* Health (sem auth)                                                      */
/* ------------------------------------------------------------------ */

static void handle_health_get(AsyncWebServerRequest *request)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"online\":true,\"uptime_s\":%lu}", (unsigned long)(millis() / 1000UL));
    request->send(200, "application/json", buf);
}

/* ================================================================== */
/* FASE B — endpoints novos                                              */
/* ================================================================== */

/* --- /api/settings/rs485 --- */

static void handle_rs485_get(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["baud"] = app_settings_rs485_baud();
    doc["frameProfile"] = app_settings_rs485_frame_profile();
    /* lista de bauds pre-definidos para o HTML popular o <select> */
    JsonArray arr = doc["bauds"].to<JsonArray>();
    const size_t n = app_settings_rs485_std_baud_count();
    for (size_t i = 0; i < n; i++) {
        arr.add(app_settings_rs485_std_baud(i));
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

static void handle_rs485_body(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    JsonDocument doc;
    if (!accumulate_body(data, len, index, total, doc)) {
        if (index + len < total) return; /* incompleto */
        request->send(400, "application/json", "{\"error\":\"json\"}");
        return;
    }
    uint32_t baud = app_settings_rs485_baud();
    uint8_t fp = app_settings_rs485_frame_profile();
    if (!doc["baud"].isNull()) baud = (uint32_t)(unsigned long)doc["baud"];
    if (!doc["frameProfile"].isNull()) fp = (uint8_t)(int)doc["frameProfile"];
    app_settings_set_rs485(baud, fp);
    request->send(200, "application/json", "{\"ok\":true}");
}

/* --- /api/settings/mqtt --- */

static void handle_mqtt_get(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["enabled"] = app_settings_mqtt_enabled();
    doc["host"] = app_settings_mqtt_host();
    doc["port"] = app_settings_mqtt_port();
    doc["user"] = app_settings_mqtt_user();
    doc["hasPassword"] = (app_settings_mqtt_pass().length() > 0);
    doc["baseTopic"] = app_settings_mqtt_base_topic();
    doc["intervalS"] = app_settings_mqtt_telemetry_interval_s();
    doc["keywords"] = app_settings_mqtt_keywords();
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

static void handle_mqtt_body(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    JsonDocument doc;
    if (!accumulate_body(data, len, index, total, doc)) {
        if (index + len < total) return;
        request->send(400, "application/json", "{\"error\":\"json\"}");
        return;
    }
    if (!doc["enabled"].isNull()) app_settings_set_mqtt_enabled((bool)doc["enabled"]);
    const char *host = doc["host"] | "";
    if (strlen(host) > 0) app_settings_set_mqtt_host(host);
    if (!doc["port"].isNull()) app_settings_set_mqtt_port((uint16_t)(int)doc["port"]);
    const char *user = doc["user"] | "";
    const char *pass = doc["password"] | "";
    if (strlen(user) > 0 || strlen(pass) > 0) {
        String cu = app_settings_mqtt_user();
        String cp = app_settings_mqtt_pass();
        app_settings_set_mqtt_creds(
            strlen(user) > 0 ? user : cu.c_str(),
            strlen(pass) > 0 ? pass : cp.c_str());
    }
    const char *bt = doc["baseTopic"] | "";
    if (strlen(bt) > 0) app_settings_set_mqtt_base_topic(bt);
    if (!doc["intervalS"].isNull()) app_settings_set_mqtt_telemetry_interval_s((uint16_t)(int)doc["intervalS"]);
    const char *kw = doc["keywords"] | "";
    if (!doc["keywords"].isNull()) app_settings_set_mqtt_keywords(kw);
    net_mqtt_apply_settings();
    request->send(200, "application/json", "{\"ok\":true}");
}

/* --- /api/settings/ui --- */

static void handle_ui_get(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["fontIndex"] = app_settings_font_index();
    doc["splashSeconds"] = app_settings_splash_seconds();
    doc["darkMode"] = app_settings_dark_mode();
    doc["screensaverEnabled"] = app_settings_screensaver_enabled();
    doc["screensaverTimeout"] = app_settings_screensaver_timeout();
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

static void handle_ui_body(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    JsonDocument doc;
    if (!accumulate_body(data, len, index, total, doc)) {
        if (index + len < total) return;
        request->send(400, "application/json", "{\"error\":\"json\"}");
        return;
    }
    if (!doc["fontIndex"].isNull()) app_settings_set_font_index((uint8_t)(int)doc["fontIndex"]);
    if (!doc["splashSeconds"].isNull()) app_settings_set_splash_seconds((uint8_t)(int)doc["splashSeconds"]);
    if (!doc["darkMode"].isNull()) app_settings_set_dark_mode((bool)doc["darkMode"]);
    if (!doc["screensaverEnabled"].isNull()) app_settings_set_screensaver_enabled((bool)doc["screensaverEnabled"]);
    if (!doc["screensaverTimeout"].isNull()) app_settings_set_screensaver_timeout((uint16_t)(int)doc["screensaverTimeout"]);
    request->send(200, "application/json", "{\"ok\":true}");
}

/* --- /api/settings/net --- */

static void handle_net_get(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["monitorIp"] = app_settings_monitor_ip();
    doc["downloadUrl"] = app_settings_download_url();
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

static void handle_net_body(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    JsonDocument doc;
    if (!accumulate_body(data, len, index, total, doc)) {
        if (index + len < total) return;
        request->send(400, "application/json", "{\"error\":\"json\"}");
        return;
    }
    const char *ip = doc["monitorIp"] | "";
    if (!doc["monitorIp"].isNull()) app_settings_set_monitor_ip(ip);
    const char *url = doc["downloadUrl"] | "";
    if (!doc["downloadUrl"].isNull()) app_settings_set_download_url(url);
    request->send(200, "application/json", "{\"ok\":true}");
}

/* --- /api/settings/pin --- */

static void handle_pin_body(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    JsonDocument doc;
    if (!accumulate_body(data, len, index, total, doc)) {
        if (index + len < total) return;
        request->send(400, "application/json", "{\"error\":\"json\"}");
        return;
    }
    const char *cur = doc["current"] | "";
    const char *nw  = doc["new"] | "";
    if (strlen(cur) == 0 || strlen(nw) == 0) {
        request->send(400, "application/json", "{\"error\":\"campos current e new obrigatorios\"}");
        return;
    }
    String stored = app_settings_settings_pin();
    if (strcmp(cur, stored.c_str()) != 0) {
        request->send(401, "application/json", "{\"error\":\"PIN atual incorreto\"}");
        return;
    }
    if (!app_settings_set_settings_pin(nw)) {
        request->send(400, "application/json", "{\"error\":\"PIN invalido (4-16 chars)\"}");
        return;
    }
    request->send(200, "application/json", "{\"ok\":true}");
    /* Nota: proximas requests vao exigir novo PIN — browser faz re-auth automaticamente */
}

/* --- /api/system/status --- */

static void handle_system_status(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["uptime_s"] = (unsigned long)(millis() / 1000UL);
    doc["heap_free"] = (unsigned long)esp_get_free_heap_size();
    doc["heap_min"] = (unsigned long)esp_get_minimum_free_heap_size();
    doc["sd_mounted"] = sd_access_is_mounted();
    doc["wifi_rssi"] = (int)WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["fw_ver"] = "1.36";
    /* MQTT status */
    const MqttStatus ms = net_mqtt_status();
    const char *ms_str = "disabled";
    if (ms == MqttStatus::Connecting) ms_str = "connecting";
    else if (ms == MqttStatus::Connected) ms_str = "connected";
    else if (ms == MqttStatus::Error) ms_str = "error";
    doc["mqtt_status"] = ms_str;
    doc["mqtt_last_error"] = net_mqtt_last_error();
    doc["boot_count"] = app_settings_boot_count_get();
    doc["heap_guard_reboots"] = app_settings_heap_guard_count_get();
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

/* --- /api/system/reboot --- */

static void handle_system_reboot(AsyncWebServerRequest *request)
{
    request->send(200, "application/json", "{\"ok\":true,\"message\":\"reiniciando em 2s\"}");
    /* Agendar restart via one-shot timer FreeRTOS para dar tempo a resposta sair */
    static TimerHandle_t s_reboot_timer = nullptr;
    if (s_reboot_timer != nullptr) {
        xTimerDelete(s_reboot_timer, 0);
        s_reboot_timer = nullptr;
    }
    s_reboot_timer = xTimerCreate("web_reboot", pdMS_TO_TICKS(2000), pdFALSE, nullptr,
                                  [](TimerHandle_t /*t*/) { esp_restart(); });
    if (s_reboot_timer != nullptr) {
        xTimerStart(s_reboot_timer, 0);
    } else {
        /* fallback — esperar 2s e reiniciar */
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
}

/* --- /api/system/export --- */

static void handle_system_export(AsyncWebServerRequest *request)
{
    /* Exportar settings para SD: usa funcao existente de export/import se disponivel,
       caso contrario retorna 501. Aqui chamamos app_settings_export_to_sd se existir. */
    /* Verifica se SD esta montado */
    if (!sd_access_is_mounted()) {
        request->send(503, "application/json", "{\"ok\":false,\"error\":\"SD nao montado\"}");
        return;
    }
    /* Escreve um marcador simples — export completo via funcao dedicada se existir */
    bool ok = false;
    sd_access_sync_front([&]() {
        File f = SD.open("/fdigi.cfg", FILE_WRITE);
        if (!f) { return; }
        /* JSON com todos os settings exportaveis */
        JsonDocument doc;
        doc["wifi"]["ssid"] = app_settings_wifi_ssid();
        doc["fontIndex"] = app_settings_font_index();
        doc["ftp"]["user"] = app_settings_ftp_user();
        doc["ntp"]["enabled"] = app_settings_ntp_enabled();
        doc["ntp"]["server"] = app_settings_ntp_server();
        doc["tzOffsetSec"] = app_settings_tz_offset_sec();
        doc["wireguard"]["enabled"] = app_settings_wireguard_enabled();
        doc["wireguard"]["localIp"] = app_settings_wg_local_ip();
        doc["wireguard"]["peerPublicKey"] = app_settings_wg_peer_public_key();
        doc["wireguard"]["endpoint"] = app_settings_wg_endpoint();
        doc["wireguard"]["port"] = app_settings_wg_port();
        doc["rs485"]["baud"] = app_settings_rs485_baud();
        doc["rs485"]["frameProfile"] = app_settings_rs485_frame_profile();
        doc["mqtt"]["enabled"] = app_settings_mqtt_enabled();
        doc["mqtt"]["host"] = app_settings_mqtt_host();
        doc["mqtt"]["port"] = app_settings_mqtt_port();
        doc["mqtt"]["user"] = app_settings_mqtt_user();
        doc["mqtt"]["baseTopic"] = app_settings_mqtt_base_topic();
        doc["mqtt"]["intervalS"] = app_settings_mqtt_telemetry_interval_s();
        doc["mqtt"]["keywords"] = app_settings_mqtt_keywords();
        doc["ui"]["fontIndex"] = app_settings_font_index();
        doc["ui"]["splashSeconds"] = app_settings_splash_seconds();
        doc["ui"]["darkMode"] = app_settings_dark_mode();
        doc["ui"]["screensaverEnabled"] = app_settings_screensaver_enabled();
        doc["ui"]["screensaverTimeout"] = app_settings_screensaver_timeout();
        doc["net"]["monitorIp"] = app_settings_monitor_ip();
        doc["net"]["downloadUrl"] = app_settings_download_url();
        String out;
        serializeJson(doc, out);
        f.print(out);
        f.close();
        ok = true;
    });
    request->send(ok ? 200 : 500, "application/json",
                  ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"escrita SD falhou\"}");
}

/* --- /api/system/import --- */

static void handle_system_import(AsyncWebServerRequest *request)
{
    if (!sd_access_is_mounted()) {
        request->send(503, "application/json", "{\"ok\":false,\"error\":\"SD nao montado\"}");
        return;
    }
    bool ok = false;
    String err_msg;
    sd_access_sync_front([&]() {
        File f = SD.open("/fdigi.cfg", FILE_READ);
        if (!f) { err_msg = "fdigi.cfg nao encontrado"; return; }
        const size_t sz = f.size();
        if (sz > 4096U) { f.close(); err_msg = "ficheiro demasiado grande"; return; }
        String buf;
        buf.reserve(sz);
        while (f.available()) buf += (char)f.read();
        f.close();
        JsonDocument doc;
        if (deserializeJson(doc, buf)) { err_msg = "json invalido"; return; }
        /* Aplicar campos presentes */
        if (!doc["fontIndex"].isNull()) app_settings_set_font_index((uint8_t)(int)doc["fontIndex"]);
        if (!doc["tzOffsetSec"].isNull()) app_settings_set_tz_offset_sec((int32_t)(int)doc["tzOffsetSec"]);
        if (!doc["ntp"].isNull()) {
            if (!doc["ntp"]["enabled"].isNull()) app_settings_set_ntp_enabled((bool)doc["ntp"]["enabled"]);
            const char *srv = doc["ntp"]["server"] | "";
            if (strlen(srv) > 0) app_settings_set_ntp_server(srv);
        }
        if (!doc["rs485"].isNull()) {
            uint32_t b = app_settings_rs485_baud();
            uint8_t fp = app_settings_rs485_frame_profile();
            if (!doc["rs485"]["baud"].isNull()) b = (uint32_t)(unsigned long)doc["rs485"]["baud"];
            if (!doc["rs485"]["frameProfile"].isNull()) fp = (uint8_t)(int)doc["rs485"]["frameProfile"];
            app_settings_set_rs485(b, fp);
        }
        if (!doc["mqtt"].isNull()) {
            if (!doc["mqtt"]["enabled"].isNull()) app_settings_set_mqtt_enabled((bool)doc["mqtt"]["enabled"]);
            const char *h = doc["mqtt"]["host"] | "";
            if (strlen(h) > 0) app_settings_set_mqtt_host(h);
            if (!doc["mqtt"]["port"].isNull()) app_settings_set_mqtt_port((uint16_t)(int)doc["mqtt"]["port"]);
            const char *u = doc["mqtt"]["user"] | "";
            if (strlen(u) > 0) app_settings_set_mqtt_creds(u, app_settings_mqtt_pass().c_str());
            const char *bt = doc["mqtt"]["baseTopic"] | "";
            if (strlen(bt) > 0) app_settings_set_mqtt_base_topic(bt);
            if (!doc["mqtt"]["intervalS"].isNull()) app_settings_set_mqtt_telemetry_interval_s((uint16_t)(int)doc["mqtt"]["intervalS"]);
            const char *kw = doc["mqtt"]["keywords"] | "";
            if (!doc["mqtt"]["keywords"].isNull()) app_settings_set_mqtt_keywords(kw);
        }
        if (!doc["ui"].isNull()) {
            if (!doc["ui"]["splashSeconds"].isNull()) app_settings_set_splash_seconds((uint8_t)(int)doc["ui"]["splashSeconds"]);
            if (!doc["ui"]["darkMode"].isNull()) app_settings_set_dark_mode((bool)doc["ui"]["darkMode"]);
            if (!doc["ui"]["screensaverEnabled"].isNull()) app_settings_set_screensaver_enabled((bool)doc["ui"]["screensaverEnabled"]);
            if (!doc["ui"]["screensaverTimeout"].isNull()) app_settings_set_screensaver_timeout((uint16_t)(int)doc["ui"]["screensaverTimeout"]);
        }
        if (!doc["net"].isNull()) {
            const char *mi = doc["net"]["monitorIp"] | "";
            if (!doc["net"]["monitorIp"].isNull()) app_settings_set_monitor_ip(mi);
            const char *du = doc["net"]["downloadUrl"] | "";
            if (!doc["net"]["downloadUrl"].isNull()) app_settings_set_download_url(du);
        }
        ok = true;
    });
    if (ok) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(500, "application/json",
                      String("{\"ok\":false,\"error\":\"") + err_msg + "\"}");
    }
}

/* ================================================================== */
/* Inicializacao                                                          */
/* ================================================================== */

void web_portal_init(void)
{
    if (s_srv != nullptr) {
        return;
    }

    s_log_queue = xQueueCreate(kLogLineQ, sizeof(LogLineMsg));
    if (s_log_queue == nullptr) {
        ESP_LOGE(TAG, "fila de logs");
    } else if (xTaskCreatePinnedToCore(log_forward_task, "web_log_fwd", 4096, nullptr, 1, &s_log_task, 0) != pdPASS) {
        ESP_LOGE(TAG, "task log_fwd");
    }

    app_log_set_line_notify(log_notify_cb, nullptr);

    s_ws_logs = new AsyncWebSocket("/ws/logs");
    s_ws_logs->onEvent(on_ws_logs_event);

    s_srv = new AsyncWebServer(kHttpPort);
    s_srv->addHandler(s_ws_logs);

    /* --- / (UI principal) --- */
    s_srv->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        request->send(200, "text/html", WEB_PORTAL_HTML);
    });

    /* --- /api/health (sem auth) --- */
    s_srv->on("/api/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        handle_health_get(request);
    });

    /* --- /api/settings (legacy, retrocompativel) --- */
    s_srv->on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_settings_get(request);
    });

    s_srv->on(
        "/api/settings", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!web_auth_check(request)) return;
            (void)request;
        },
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!web_auth_check(request)) return;
            handle_settings_body(request, data, len, index, total);
        });

    /* --- /api/settings/rs485 --- */
    s_srv->on("/api/settings/rs485", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_rs485_get(request);
    });

    s_srv->on(
        "/api/settings/rs485", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!web_auth_check(request)) return;
            (void)request;
        },
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!web_auth_check(request)) return;
            handle_rs485_body(request, data, len, index, total);
        });

    /* --- /api/settings/mqtt --- */
    s_srv->on("/api/settings/mqtt", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_mqtt_get(request);
    });

    s_srv->on(
        "/api/settings/mqtt", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!web_auth_check(request)) return;
            (void)request;
        },
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!web_auth_check(request)) return;
            handle_mqtt_body(request, data, len, index, total);
        });

    /* --- /api/settings/ui --- */
    s_srv->on("/api/settings/ui", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_ui_get(request);
    });

    s_srv->on(
        "/api/settings/ui", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!web_auth_check(request)) return;
            (void)request;
        },
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!web_auth_check(request)) return;
            handle_ui_body(request, data, len, index, total);
        });

    /* --- /api/settings/net --- */
    s_srv->on("/api/settings/net", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_net_get(request);
    });

    s_srv->on(
        "/api/settings/net", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!web_auth_check(request)) return;
            (void)request;
        },
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!web_auth_check(request)) return;
            handle_net_body(request, data, len, index, total);
        });

    /* --- /api/settings/pin --- */
    s_srv->on(
        "/api/settings/pin", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!web_auth_check(request)) return;
            (void)request;
        },
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!web_auth_check(request)) return;
            handle_pin_body(request, data, len, index, total);
        });

    /* --- /api/system/status --- */
    s_srv->on("/api/system/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_system_status(request);
    });

    /* --- /api/system/reboot --- */
    s_srv->on("/api/system/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_system_reboot(request);
    });

    /* --- /api/system/export --- */
    s_srv->on("/api/system/export", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_system_export(request);
    });

    /* --- /api/system/import --- */
    s_srv->on("/api/system/import", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_system_import(request);
    });

    /* --- /api/logs --- */
    s_srv->on("/api/logs/tail", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_logs_tail(request);
    });

    s_srv->on("/api/logs", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_logs_delete(request);
    });

    /* --- /api/fs --- */
    s_srv->on("/api/fs/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_fs_list(request);
    });

    s_srv->on("/api/fs/file", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!web_auth_check(request)) return;
        handle_fs_file(request);
    });

    /* --- /api/ota/upload --- */
    s_srv->on(
        "/api/ota/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!web_auth_check(request)) return;
            handle_ota_upload_request(request);
        },
        [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!web_auth_check(request)) return;
            handle_ota_upload_data(request, filename, index, data, len, final);
        },
        NULL);

    s_srv->begin();
    if (WiFi.status() == WL_CONNECTED) {
        ESP_LOGI(TAG, "HTTP %u em http://%s/ (logs WS /ws/logs)", (unsigned)kHttpPort,
                 WiFi.localIP().toString().c_str());
    } else {
        ESP_LOGW(TAG, "HTTP %u iniciado sem Wi-Fi STA ligado — portal inacessivel na rede ate associar.",
                 (unsigned)kHttpPort);
    }
}
