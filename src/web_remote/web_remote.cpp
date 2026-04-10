/**
 * @file web_remote.cpp
 * @brief Acesso remoto ao display LVGL via WebSocket — JPEG stream.
 *
 * Estratégia:
 *   - Downscale configuravel (1..8) no ESP32.
 *   - Conversão RGB565 → RGB888 durante o downscale.
 *   - Encode RGB888 → JPEG (qualidade configuravel em runtime).
 *   - Envia frame JPEG completo via WebSocket binary (~5-15 KB/frame).
 *   - Throttle configuravel em ms (runtime).
 *   - Task dedicada no core 0 para encode+envio (LVGL no core 1).
 *   - Ping keepalive a cada 15s para manter conexão.
 *
 * Protocolo binário ESP32 → Browser:
 *   Mensagem WebSocket binary = frame JPEG completo (decodificável como imagem).
 *
 * Protocolo binário Browser → ESP32:
 *   [0]       flag (1 = press, 0 = release)
 *   [1-2]     pointer X (big-endian) — coordenadas do canvas remoto, escaladas para 800×480
 *   [3-4]     pointer Y (big-endian)
 */

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <esp_log.h>
#include <esp_heap_caps.h>

#include "app_settings.h"
#include "app_log.h"
#include "jpge.h"
#include "web_remote.h"
#include "web_remote_html.h"
#include "lvgl_port_v8.h"

static const char *TAG = "web_remote";

/* ── Configuração ──────────────────────────────────────────────── */
#define WR_HTTP_PORT          80
#define WR_WS_PATH            "/ws"
#define WR_SEND_TASK_STACK    (32 * 1024)  /* primeira tentativa; fallback reduz stack */
#define WR_SEND_TASK_PRIO     1
#define WR_SEND_TASK_CORE     0       /* Core 0 (LVGL no core 1)                  */
#define WR_PING_INTERVAL_MS   15000
#define WR_CLEANUP_INTERVAL   5000
#define WR_MAX_CLIENTS        2
#define WR_SCALE_MIN          1
#define WR_SCALE_MAX          8
#define WR_SCALE_DEFAULT      1
#define WR_JPEG_QUALITY_MIN   1
#define WR_JPEG_QUALITY_MAX   100
#define WR_JPEG_QUALITY_DEF   35
#define WR_INTERVAL_MS_MIN    80
#define WR_INTERVAL_MS_MAX    2000
#define WR_INTERVAL_MS_DEF    180
#define WR_MAX_CANVAS_W       (LVGL_PORT_DISP_WIDTH / WR_SCALE_MIN)   /* 800 */
#define WR_MAX_CANVAS_H       (LVGL_PORT_DISP_HEIGHT / WR_SCALE_MIN)  /* 480 */

/* ── Objectos do servidor ──────────────────────────────────────── */
static AsyncWebServer *s_server = nullptr;
static AsyncWebSocket *s_ws     = nullptr;

/* ── Task de envio ─────────────────────────────────────────────── */
static TaskHandle_t s_send_task = nullptr;

/* ── Buffers ───────────────────────────────────────────────────── */
static uint8_t *s_rgb_buf  = nullptr;   /* RGB888 até 400×240×3 = 288 KB */
static uint8_t *s_jpeg_buf = nullptr;   /* Buffer de saída JPEG                     */
static size_t   s_rgb_buf_sz  = 0;
static int      s_jpeg_buf_sz = 0;

/* ── Configuração de stream (runtime) ──────────────────────────── */
static portMUX_TYPE s_cfg_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t s_cfg_scale = WR_SCALE_DEFAULT;
static volatile uint8_t s_cfg_jpeg_quality = WR_JPEG_QUALITY_DEF;
static volatile uint16_t s_cfg_min_interval_ms = WR_INTERVAL_MS_DEF;

/*
 * Snapshot do framebuffer: ponteiro + dirty flag.
 * Escrito pela task LVGL, lido pela task de envio.
 */
static const lv_color_t *s_snap_fb   = nullptr;
static const lv_color_t *s_last_fb   = nullptr;
static volatile bool s_snap_ready = false;
static volatile bool s_send_busy  = false;

/* ── Acumulador de dirty region (task LVGL apenas) ─────────────── */
static lv_coord_t s_acc_x1 = LV_COORD_MAX;
static lv_coord_t s_acc_y1 = LV_COORD_MAX;
static lv_coord_t s_acc_x2 = LV_COORD_MIN;
static lv_coord_t s_acc_y2 = LV_COORD_MIN;
static uint32_t   s_last_send_ms = 0;

/* ── Estado do ponteiro remoto ─────────────────────────────────── */
static volatile lv_coord_t s_ptr_x       = 0;
static volatile lv_coord_t s_ptr_y       = 0;
static volatile bool       s_ptr_pressed = false;
static volatile bool       s_ptr_active  = false;

/* ── Helpers ───────────────────────────────────────────────────── */

static void acc_reset(void)
{
    s_acc_x1 = LV_COORD_MAX;
    s_acc_y1 = LV_COORD_MAX;
    s_acc_x2 = LV_COORD_MIN;
    s_acc_y2 = LV_COORD_MIN;
}

static uint8_t clamp_scale(uint8_t v)
{
    if (v < WR_SCALE_MIN) return WR_SCALE_MIN;
    if (v > WR_SCALE_MAX) return WR_SCALE_MAX;
    return v;
}

static uint8_t clamp_quality(uint8_t v)
{
    if (v < WR_JPEG_QUALITY_MIN) return WR_JPEG_QUALITY_MIN;
    if (v > WR_JPEG_QUALITY_MAX) return WR_JPEG_QUALITY_MAX;
    return v;
}

static uint16_t clamp_interval(uint16_t v)
{
    if (v < WR_INTERVAL_MS_MIN) return WR_INTERVAL_MS_MIN;
    if (v > WR_INTERVAL_MS_MAX) return WR_INTERVAL_MS_MAX;
    return v;
}

static inline uint16_t canvas_w_for_scale(uint8_t scale)
{
    return (uint16_t)(LVGL_PORT_DISP_WIDTH / scale);
}

static inline uint16_t canvas_h_for_scale(uint8_t scale)
{
    return (uint16_t)(LVGL_PORT_DISP_HEIGHT / scale);
}

static void cfg_read_snapshot(uint8_t *scale, uint8_t *quality, uint16_t *interval_ms)
{
    portENTER_CRITICAL(&s_cfg_mux);
    *scale = s_cfg_scale;
    *quality = s_cfg_jpeg_quality;
    *interval_ms = s_cfg_min_interval_ms;
    portEXIT_CRITICAL(&s_cfg_mux);
}

static void send_cfg_to_client(AsyncWebSocketClient *client)
{
    if (!client) return;
    uint8_t scale, quality;
    uint16_t interval_ms;
    cfg_read_snapshot(&scale, &quality, &interval_ms);
    const uint16_t cw = canvas_w_for_scale(scale);
    const uint16_t ch = canvas_h_for_scale(scale);
    char msg[64];
    snprintf(msg, sizeof(msg), "CFG,%u,%u,%u,%u", (unsigned)cw, (unsigned)ch,
             (unsigned)quality, (unsigned)interval_ms);
    client->text(msg);
}

static void send_cfg_to_all_clients(void)
{
    if (!s_ws) return;
    uint8_t scale, quality;
    uint16_t interval_ms;
    cfg_read_snapshot(&scale, &quality, &interval_ms);
    const uint16_t cw = canvas_w_for_scale(scale);
    const uint16_t ch = canvas_h_for_scale(scale);
    char msg[64];
    snprintf(msg, sizeof(msg), "CFG,%u,%u,%u,%u", (unsigned)cw, (unsigned)ch,
             (unsigned)quality, (unsigned)interval_ms);
    s_ws->textAll(msg);
}

/**
 * Agenda envio imediato usando o ultimo framebuffer conhecido.
 * Evita tela preta no primeiro connect quando nao ocorre novo flush.
 */
static void request_send_last_frame_now(void)
{
    if (!s_send_task || !s_last_fb || s_send_busy) {
        return;
    }
    s_snap_fb = s_last_fb;
    s_send_busy = true;
    s_snap_ready = true;
    xTaskNotifyGive(s_send_task);
}

/**
 * Downscale Nx1 do framebuffer RGB565 inteiro -> buffer RGB888.
 * N = scale (1..8), fazendo media scale x scale por pixel de saida.
 */
static void downscale_full_rgb888(const lv_color_t *fb, uint8_t scale, uint16_t canvas_w, uint16_t canvas_h)
{
    const uint16_t fb_w = LVGL_PORT_DISP_WIDTH;
    uint8_t *dst = s_rgb_buf;

    for (uint16_t dy = 0; dy < canvas_h; dy++) {
        const uint16_t sy = (uint16_t)(dy * scale);
        for (uint16_t dx = 0; dx < canvas_w; dx++) {
            const uint16_t sx = (uint16_t)(dx * scale);

            uint32_t r_sum = 0;
            uint32_t g_sum = 0;
            uint32_t b_sum = 0;
            for (uint8_t yy = 0; yy < scale; yy++) {
                const uint16_t *row = (const uint16_t *)&fb[(uint32_t)(sy + yy) * fb_w];
                for (uint8_t xx = 0; xx < scale; xx++) {
                    const uint16_t p = row[sx + xx];
                    r_sum += ((p >> 11) & 0x1F);
                    g_sum += ((p >> 5) & 0x3F);
                    b_sum += (p & 0x1F);
                }
            }
            const uint32_t samples = (uint32_t)scale * (uint32_t)scale;
            uint16_t r5 = (uint16_t)((r_sum + (samples / 2U)) / samples);
            uint16_t g6 = (uint16_t)((g_sum + (samples / 2U)) / samples);
            uint16_t b5 = (uint16_t)((b_sum + (samples / 2U)) / samples);

            /* R5→R8, G6→G8, B5→B8 */
            *dst++ = (uint8_t)((r5 << 3) | (r5 >> 2));
            *dst++ = (uint8_t)((g6 << 2) | (g6 >> 4));
            *dst++ = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
    }
}

/* ── Callback do WebSocket ─────────────────────────────────────── */

static void on_ws_event(AsyncWebSocket * /*server*/,
                        AsyncWebSocketClient *client,
                        AwsEventType type, void *arg,
                        uint8_t *data, size_t len)
{
    switch (type) {
    case WS_EVT_CONNECT:
        ESP_LOGI(TAG, "Cliente WS #%u conectado", client->id());
        send_cfg_to_client(client);
        /* Força refresh completo */
        s_acc_x1 = 0;
        s_acc_y1 = 0;
        s_acc_x2 = LVGL_PORT_DISP_WIDTH  - 1;
        s_acc_y2 = LVGL_PORT_DISP_HEIGHT - 1;
        s_last_send_ms = 0;
        request_send_last_frame_now();
        break;

    case WS_EVT_DISCONNECT:
        ESP_LOGI(TAG, "Cliente WS #%u desconectado", client->id());
        if (s_ws->count() == 0) {
            s_ptr_active  = false;
            s_ptr_pressed = false;
        }
        break;

    case WS_EVT_DATA: {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->opcode == WS_BINARY && len == 5) {
            uint8_t scale, quality;
            uint16_t interval_ms;
            cfg_read_snapshot(&scale, &quality, &interval_ms);
            (void)quality;
            (void)interval_ms;
            uint16_t bx = (uint16_t)((data[1] << 8) | data[2]);
            uint16_t by = (uint16_t)((data[3] << 8) | data[4]);
            const uint16_t cw = canvas_w_for_scale(scale);
            const uint16_t ch = canvas_h_for_scale(scale);
            if (bx >= cw) bx = (uint16_t)(cw - 1);
            if (by >= ch) by = (uint16_t)(ch - 1);
            s_ptr_x       = (lv_coord_t)(bx * scale);
            s_ptr_y       = (lv_coord_t)(by * scale);
            if (s_ptr_x >= LVGL_PORT_DISP_WIDTH) s_ptr_x = LVGL_PORT_DISP_WIDTH - 1;
            if (s_ptr_y >= LVGL_PORT_DISP_HEIGHT) s_ptr_y = LVGL_PORT_DISP_HEIGHT - 1;
            s_ptr_pressed = (data[0] != 0);
            s_ptr_active  = true;
        }
        break;
    }

    default:
        break;
    }
}

/* ── Task de envio (core 0) ────────────────────────────────────── */

static void send_task_func(void * /*arg*/)
{
    uint32_t last_ping_ms    = 0;
    uint32_t last_cleanup_ms = 0;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

        const uint32_t now = (uint32_t)millis();

        /* Manutenção periódica */
        if (s_ws) {
            if ((now - last_cleanup_ms) >= WR_CLEANUP_INTERVAL) {
                s_ws->cleanupClients(WR_MAX_CLIENTS);
                last_cleanup_ms = now;
            }
            if ((now - last_ping_ms) >= WR_PING_INTERVAL_MS && s_ws->count() > 0) {
                s_ws->pingAll();
                last_ping_ms = now;
            }
        }

        if (!s_snap_ready || !s_ws || s_ws->count() == 0) {
            if (s_snap_ready) { s_snap_ready = false; s_send_busy = false; }
            continue;
        }

        /* Captura snapshot info */
        const lv_color_t *fb = s_snap_fb;
        s_snap_ready = false;
        uint8_t scale, quality;
        uint16_t interval_ms;
        cfg_read_snapshot(&scale, &quality, &interval_ms);
        const uint16_t canvas_w = canvas_w_for_scale(scale);
        const uint16_t canvas_h = canvas_h_for_scale(scale);

        /* 1) Downscale RGB565 -> RGB888 */
        downscale_full_rgb888(fb, scale, canvas_w, canvas_h);

        /* 2) Encode RGB888 → JPEG via jpge */
        jpge::params comp_params;
        comp_params.m_quality = quality;
        comp_params.m_subsampling = jpge::H2V2;

        int jpeg_out_size = s_jpeg_buf_sz;
        bool ok = jpge::compress_image_to_jpeg_file_in_memory(
            s_jpeg_buf, jpeg_out_size,
            canvas_w, canvas_h, 3,
            s_rgb_buf, comp_params
        );

        if (!ok || jpeg_out_size <= 0) {
            ESP_LOGW(TAG, "JPEG encode falhou: size=%d", jpeg_out_size);
            s_send_busy = false;
            continue;
        }

        ESP_LOGD(TAG, "JPEG frame: %d bytes (%.1f KB)", jpeg_out_size, jpeg_out_size / 1024.0f);

        /*
         * 3) Enviar frame JPEG via WebSocket (tolerante a cliente lento).
         * Evita o gate all-or-nothing de availableForWriteAll(), que podia
         * bloquear video para todos quando apenas um cliente ficava congestionado.
         */
        const AsyncWebSocket::SendStatus send_st = s_ws->binaryAll(s_jpeg_buf, (size_t)jpeg_out_size);
        const unsigned clients_now = (unsigned)s_ws->count();
        if (send_st == AsyncWebSocket::ENQUEUED) {
            ESP_LOGD(TAG, "Frame enviado: bytes=%d clients=%u status=ENQ", jpeg_out_size, clients_now);
        } else if (send_st == AsyncWebSocket::PARTIALLY_ENQUEUED) {
            ESP_LOGW(TAG, "Frame parcial: bytes=%d clients=%u status=PART", jpeg_out_size, clients_now);
        } else {
            ESP_LOGW(TAG, "Frame descartado por backpressure: bytes=%d clients=%u", jpeg_out_size, clients_now);
        }

        s_send_busy = false;
    }
}

/* ── API pública ───────────────────────────────────────────────── */

void web_remote_init(void)
{
    web_remote_stream_cfg_t cfg;
    cfg.scale = app_settings_vnc_scale();
    cfg.jpeg_quality = app_settings_vnc_jpeg_quality();
    cfg.interval_ms = app_settings_vnc_interval_ms();
    web_remote_set_stream_config(&cfg);

    /* Buffer RGB888 maximo (escala minima): 800×480×3 = 1 152 000 bytes */
    s_rgb_buf_sz = (size_t)WR_MAX_CANVAS_W * WR_MAX_CANVAS_H * 3;
    s_rgb_buf = (uint8_t *)heap_caps_malloc(s_rgb_buf_sz,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_rgb_buf) {
        ESP_LOGE(TAG, "Falha ao alocar RGB buffer (%u bytes)", (unsigned)s_rgb_buf_sz);
        app_log_feature_writef("ERROR", "VNC", "Falha alocar RGB buffer (%u bytes)",
                               (unsigned)s_rgb_buf_sz);
        return;
    }

    /*
     * Buffer JPEG de saída. Na escala minima suportada (1:1 => 800x480),
     * usar 420 KB reduz risco de truncar em qualidades altas.
     */
    s_jpeg_buf_sz = 420 * 1024;
    s_jpeg_buf = (uint8_t *)heap_caps_malloc((size_t)s_jpeg_buf_sz,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_jpeg_buf) {
        ESP_LOGE(TAG, "Falha ao alocar JPEG buffer (%d bytes)", s_jpeg_buf_sz);
        app_log_feature_writef("ERROR", "VNC", "Falha alocar JPEG buffer (%d bytes)", s_jpeg_buf_sz);
        heap_caps_free(s_rgb_buf);
        s_rgb_buf = nullptr;
        return;
    }

    uint8_t scale, quality;
    uint16_t interval_ms;
    cfg_read_snapshot(&scale, &quality, &interval_ms);
    ESP_LOGI(TAG, "JPEG encoder (jpge): scale=%u canvas=%ux%u q=%u min_ms=%u RGB=%uKB JPEG=%dKB",
             (unsigned)scale, (unsigned)canvas_w_for_scale(scale), (unsigned)canvas_h_for_scale(scale),
             (unsigned)quality, (unsigned)interval_ms,
             (unsigned)(s_rgb_buf_sz / 1024), s_jpeg_buf_sz / 1024);

    /* WebSocket + HTTP */
    s_ws = new AsyncWebSocket(WR_WS_PATH);
    s_ws->onEvent(on_ws_event);

    s_server = new AsyncWebServer(WR_HTTP_PORT);
    s_server->addHandler(s_ws);
    s_server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", WEB_REMOTE_HTML);
    });
    s_server->begin();

    /* Task de envio no core 0 (retry com stacks menores se faltar heap) */
    const uint32_t stack_candidates[] = {
        WR_SEND_TASK_STACK,
        24U * 1024U,
        20U * 1024U
    };
    BaseType_t create_ret = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    uint32_t selected_stack = 0;
    for (size_t i = 0; i < (sizeof(stack_candidates) / sizeof(stack_candidates[0])); i++) {
        const uint32_t stack_bytes = stack_candidates[i];
        s_send_task = nullptr;
        create_ret = xTaskCreatePinnedToCore(send_task_func,
                                             "wr_send",
                                             stack_bytes,
                                             nullptr,
                                             WR_SEND_TASK_PRIO,
                                             &s_send_task,
                                             WR_SEND_TASK_CORE);
        if (create_ret == pdPASS && s_send_task != nullptr) {
            selected_stack = stack_bytes;
            break;
        }
    }
    const bool send_task_ok = (create_ret == pdPASS && s_send_task != nullptr);
    if (!send_task_ok) {
        ESP_LOGE(TAG, "Falha ao criar task de envio WS (ret=%d)", (int)create_ret);
    } else {
        ESP_LOGI(TAG, "Task de envio WS criada (stack=%u core=%d)",
                 (unsigned)selected_stack, (int)WR_SEND_TASK_CORE);
    }

    ESP_LOGI(TAG, "HTTP:%d WS:%s task:core%d", WR_HTTP_PORT, WR_WS_PATH, WR_SEND_TASK_CORE);
    app_log_feature_writef("INFO", "VNC", "Servidor ativo HTTP:%d WS:%s core=%d",
                           WR_HTTP_PORT, WR_WS_PATH, WR_SEND_TASK_CORE);
}

void web_remote_flush(const lv_area_t *area, const lv_color_t *color_map, bool last)
{
    /* Mantem referencia do ultimo framebuffer para bootstrap de novos clientes. */
    s_last_fb = color_map;

    /* Acumula bounding-box (para saber se houve mudança) */
    if (area->x1 < s_acc_x1) s_acc_x1 = area->x1;
    if (area->y1 < s_acc_y1) s_acc_y1 = area->y1;
    if (area->x2 > s_acc_x2) s_acc_x2 = area->x2;
    if (area->y2 > s_acc_y2) s_acc_y2 = area->y2;

    if (!last) return;

    /* Sem clientes ou não inicializado */
    const bool ws_null = (s_ws == nullptr);
    const bool task_null = (s_send_task == nullptr);
    const uint32_t clients_now = (s_ws ? (uint32_t)s_ws->count() : 0U);
    if (ws_null || task_null || clients_now == 0U) {
        acc_reset();
        return;
    }

    /* Task de envio ocupada → skip (mantém acumulador) */
    if (s_send_busy) {
        return;
    }

    /* Throttle configuravel em runtime */
    const uint32_t now = (uint32_t)millis();
    uint8_t scale, quality;
    uint16_t interval_ms;
    cfg_read_snapshot(&scale, &quality, &interval_ms);
    (void)scale;
    (void)quality;
    if ((now - s_last_send_ms) < interval_ms) {
        return;
    }

    /* Verifica se houve mudança real */
    if (s_acc_x1 > s_acc_x2 || s_acc_y1 > s_acc_y2) { acc_reset(); return; }

    /*
     * Publica ponteiro do framebuffer para a task de envio.
     * Em direct mode, color_map aponta para o framebuffer LCD (PSRAM),
     * que permanece estável até o próximo flush completo.
     * Com JPEG, enviamos o frame inteiro (não apenas dirty region).
     */
    s_snap_fb    = color_map;
    s_send_busy  = true;
    s_snap_ready = true;

    xTaskNotifyGive(s_send_task);

    acc_reset();
    s_last_send_ms = now;
}

bool web_remote_get_pointer(lv_coord_t *x, lv_coord_t *y, bool *pressed)
{
    if (!s_ptr_active) return false;
    *x       = s_ptr_x;
    *y       = s_ptr_y;
    *pressed = s_ptr_pressed;
    return true;
}

void web_remote_get_stream_config(web_remote_stream_cfg_t *cfg)
{
    if (!cfg) return;
    uint8_t scale, quality;
    uint16_t interval_ms;
    cfg_read_snapshot(&scale, &quality, &interval_ms);
    cfg->scale = scale;
    cfg->jpeg_quality = quality;
    cfg->interval_ms = interval_ms;
}

void web_remote_set_stream_config(const web_remote_stream_cfg_t *cfg)
{
    if (!cfg) return;
    const uint8_t scale = clamp_scale(cfg->scale);
    const uint8_t quality = clamp_quality(cfg->jpeg_quality);
    const uint16_t interval_ms = clamp_interval(cfg->interval_ms);
    portENTER_CRITICAL(&s_cfg_mux);
    s_cfg_scale = scale;
    s_cfg_jpeg_quality = quality;
    s_cfg_min_interval_ms = interval_ms;
    portEXIT_CRITICAL(&s_cfg_mux);
    ESP_LOGI(TAG, "Config stream: scale=%u canvas=%ux%u q=%u min_ms=%u",
             (unsigned)scale, (unsigned)canvas_w_for_scale(scale), (unsigned)canvas_h_for_scale(scale),
             (unsigned)quality, (unsigned)interval_ms);
    /* Evita I/O em SD no contexto de UI/LVGL ao alterar configuracao em runtime. */
    ESP_LOGI(TAG, "[VNC] Config stream aplicada em runtime");
    send_cfg_to_all_clients();
}
