/**
 * @file screenshot.cpp
 * @brief Snapshot do LVGL display em JPEG gravado no SD (background).
 *
 * Mantem shadow buffer 768 KB em PSRAM continuamente actualizado via flush hook
 * LVGL. screenshot_capture_to_sd() agenda encode JPEG + write em sd_io task —
 * caller (HTTP handler async) volta imediato para evitar TASK_WDT.
 *
 * JPEG via bitbank2/JPEGENC — RGB565 native input, ~50-150 KB output (vs 1.15 MB BMP).
 */
#include "screenshot.h"
#include "lvgl_port_v8.h"
#include "sd_access.h"

#include <Arduino.h>
#include <JPEGENC.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "screenshot";

static constexpr int    kDispW = LVGL_PORT_DISP_WIDTH;
static constexpr int    kDispH = LVGL_PORT_DISP_HEIGHT;
static constexpr size_t kShadowBufBytes = (size_t)kDispW * kDispH * 2;

static uint8_t *s_shadow_buf = nullptr;

void screenshot_flush_notify(const lv_area_t *area, const lv_color_t *color_map) {
    if (!s_shadow_buf) return;
    const int x1 = area->x1, y1 = area->y1, x2 = area->x2, y2 = area->y2;
    const int w  = x2 - x1 + 1;
    for (int row = y1; row <= y2; row++) {
        uint8_t       *dst = s_shadow_buf + ((size_t)row * kDispW + x1) * 2;
        const uint8_t *src = (const uint8_t *)color_map + ((size_t)row * kDispW + x1) * 2;
        memcpy(dst, src, (size_t)w * 2);
    }
}

void screenshot_init(void) {
    if (s_shadow_buf != nullptr) return;
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < kShadowBufBytes + 1024U * 1024U) {
        log_e("[screenshot] PSRAM insuficiente (precisa %u + 1M livre)", (unsigned)kShadowBufBytes);
        return;
    }
    s_shadow_buf = (uint8_t *)heap_caps_malloc(kShadowBufBytes, MALLOC_CAP_SPIRAM);
    if (!s_shadow_buf) {
        log_e("[screenshot] heap_caps_malloc(%u) falhou", (unsigned)kShadowBufBytes);
        return;
    }
    memset(s_shadow_buf, 0, kShadowBufBytes);
    lvgl_port_set_flush_hook(screenshot_flush_notify);
    log_i("[screenshot] init OK shadow_buf=%u bytes PSRAM", (unsigned)kShadowBufBytes);
}

size_t screenshot_bmp_size(void) {
    /* JPEG quality HIGH 800x480 ≈ 60-120 KB tipicamente. Estimativa optimista. */
    return 120UL * 1024UL;
}

/* mkdir -p no ficheiro pai. Chamar dentro de sd_io task. */
static void ensure_parent_dirs(const char *filepath) {
    char buf[128];
    strncpy(buf, filepath, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *slash = strrchr(buf, '/');
    if (!slash || slash == buf) return;
    *slash = '\0';
    if (!SD.exists(buf)) {
        SD.mkdir(buf);
    }
}

/* --- Callbacks JPEGENC para output em File SD --- */
static void *jpg_open_cb(const char *fname) {
    File *f = new File(SD.open(fname, FILE_WRITE));
    if (!*f) { delete f; return nullptr; }
    return f;
}
static void jpg_close_cb(JPEGE_FILE *pf) {
    if (!pf || !pf->fHandle) return;
    File *f = (File *)pf->fHandle;
    f->close();
    delete f;
}
static int32_t jpg_read_cb(JPEGE_FILE *pf, uint8_t *buf, int32_t len) {
    File *f = (File *)pf->fHandle;
    return (int32_t)f->read(buf, (size_t)len);
}
static int32_t jpg_write_cb(JPEGE_FILE *pf, uint8_t *buf, int32_t len) {
    File *f = (File *)pf->fHandle;
    return (int32_t)f->write(buf, (size_t)len);
}
static int32_t jpg_seek_cb(JPEGE_FILE *pf, int32_t pos) {
    File *f = (File *)pf->fHandle;
    return f->seek((uint32_t)pos) ? pos : -1;
}

/* Worker em sd_io task (32 KB stack). filepath_dup libertado aqui.
 * delay_ms > 0: aguarda antes de encodar (uso pos-wake-screensaver). */
static void screenshot_worker(char *filepath_dup, uint32_t delay_ms) {
    if (delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    ensure_parent_dirs(filepath_dup);

    JPEGENC jpg;
    JPEGENCODE enc;
    int rc = jpg.open(filepath_dup, jpg_open_cb, jpg_close_cb, jpg_read_cb,
                       jpg_write_cb, jpg_seek_cb);
    if (rc != JPEGE_SUCCESS) {
        log_e("[screenshot] JPEGENC.open(%s) falhou rc=%d", filepath_dup, rc);
        free(filepath_dup);
        return;
    }
    rc = jpg.encodeBegin(&enc, kDispW, kDispH, JPEGE_PIXEL_RGB565,
                         JPEGE_SUBSAMPLE_420, JPEGE_Q_HIGH);
    if (rc != JPEGE_SUCCESS) {
        log_e("[screenshot] encodeBegin falhou rc=%d", rc);
        jpg.close();
        free(filepath_dup);
        return;
    }
    rc = jpg.addFrame(&enc, s_shadow_buf, kDispW * 2);
    if (rc != JPEGE_SUCCESS) {
        log_e("[screenshot] addFrame falhou rc=%d", rc);
        jpg.close();
        free(filepath_dup);
        return;
    }
    jpg.close();
    log_i("[screenshot] gravado %s (JPEG)", filepath_dup);
    free(filepath_dup);
}

bool screenshot_capture_to_sd_delayed(const char *filepath, uint32_t delay_ms) {
    if (!s_shadow_buf) {
        log_e("[screenshot] nao inicializado");
        return false;
    }
    if (!filepath || !filepath[0]) return false;

    char *fp_dup = strdup(filepath);
    if (!fp_dup) return false;

    /* Agenda em sd_io task — caller (HTTP handler async) nao bloqueia.
     * delay_ms aplicado dentro do worker (nao bloqueia a task async TCP). */
    sd_access_async([fp_dup, delay_ms]() {
        screenshot_worker(fp_dup, delay_ms);
    });
    return true;
}

bool screenshot_capture_to_sd(const char *filepath) {
    return screenshot_capture_to_sd_delayed(filepath, 0);
}
