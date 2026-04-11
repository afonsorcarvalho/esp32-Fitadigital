/**
 * @file web_portal.cpp
 * @brief Servidor HTTP: / (UI), /api/settings, /api/logs, /api/fs/*, WebSocket /ws/logs.
 */
#include "web_portal.h"
#include "web_portal_html.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <WiFi.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "app_log.h"
#include "app_settings.h"
#include "net_services.h"
#include "sd_access.h"

static const char *TAG = "web_portal";

static constexpr uint16_t kHttpPort = 80;
static constexpr size_t kLogLineQ = 24;
static constexpr size_t kLogLineMax = 384;
/** Limite de leitura para /api/fs/file (resposta copiada para String no heap). */
static constexpr size_t kPortalFsMaxFileBytes = 512U * 1024U;

static AsyncWebServer *s_srv = nullptr;
static AsyncWebSocket *s_ws_logs = nullptr;

struct LogLineMsg {
    char line[kLogLineMax];
};

static QueueHandle_t s_log_queue = nullptr;
static TaskHandle_t s_log_task = nullptr;

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
    /* Leitura do ficheiro de log no mesmo contexto que o resto do SD (sd_io). */
    sd_access_sync([&]() { ok_read = app_log_read_tail(buf, kLogTailHeapBytes, &fsz, &trunc); });
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
    sd_access_sync([&]() { ok = app_log_clear(); });
    request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

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
    sd_access_sync([&]() {
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
    int err = 0; /* 0 ok, 1 no sd, 2 not found, 3 too big */

    sd_access_sync([&]() {
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
        if (sz > kPortalFsMaxFileBytes) {
            test.close();
            err = 3;
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
            sd_access_sync(
                [&]() { ok_read = app_log_read_tail(buf, kLogTailHeapBytes, &fsz, &trunc); });
            if (ok_read) {
                client->text(buf);
            }
            heap_caps_free(buf);
        }
    }
}

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

    s_srv->on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(200, "text/html", WEB_PORTAL_HTML); });

    s_srv->on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) { handle_settings_get(request); });

    s_srv->on(
        "/api/settings", HTTP_POST,
        [](AsyncWebServerRequest *request) { (void)request; },
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handle_settings_body(request, data, len, index, total);
        });

    s_srv->on("/api/logs/tail", HTTP_GET, [](AsyncWebServerRequest *request) { handle_logs_tail(request); });

    s_srv->on("/api/logs", HTTP_DELETE, [](AsyncWebServerRequest *request) { handle_logs_delete(request); });

    s_srv->on("/api/fs/list", HTTP_GET, [](AsyncWebServerRequest *request) { handle_fs_list(request); });

    s_srv->on("/api/fs/file", HTTP_GET, [](AsyncWebServerRequest *request) { handle_fs_file(request); });

    s_srv->begin();
    if (WiFi.status() == WL_CONNECTED) {
        ESP_LOGI(TAG, "HTTP %u em http://%s/ (logs WS /ws/logs)", (unsigned)kHttpPort,
                 WiFi.localIP().toString().c_str());
    } else {
        ESP_LOGW(TAG, "HTTP %u iniciado sem Wi-Fi STA ligado — portal inacessivel na rede ate associar.",
                 (unsigned)kHttpPort);
    }
}
