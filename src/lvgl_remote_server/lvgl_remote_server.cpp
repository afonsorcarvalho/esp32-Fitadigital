/**
 * @file lvgl_remote_server.cpp
 * @brief API C para integrar RemoteDisplay (LVGLRemoteServer) no firmware.
 */
#include "lvgl_remote_server.h"
#include "remote_display_lv8.h"
#include "esp_log.h"

static const char *TAG = "lvgl_rmt_srv";

/** Porta UDP por defeito do viewer CubeCoders (README upstream). */
static constexpr int kDefaultUdpPort = 24680;

static RemoteDisplay *s_rd = nullptr;

void lvgl_remote_server_init(void)
{
    if (s_rd != nullptr) {
        return;
    }
    s_rd = new RemoteDisplay(kDefaultUdpPort);
    s_rd->init();
    ESP_LOGI(TAG, "LVGL Remote: UDP/%d — cliente Windows: downloads.cubecoders.com/LVGLRemote/", kDefaultUdpPort);
}

void lvgl_remote_server_flush(const lv_area_t *area, lv_color_t *color_map)
{
    if (s_rd == nullptr || area == nullptr || color_map == nullptr) {
        return;
    }
    s_rd->sendRLE(area, (uint8_t *)color_map);
}

void lvgl_remote_server_poll_input(void)
{
    if (s_rd == nullptr) {
        return;
    }
    s_rd->pollTouchFromUdp();
}

bool lvgl_remote_server_get_pointer(lv_coord_t *x, lv_coord_t *y, bool *pressed)
{
    if (s_rd == nullptr || x == nullptr || y == nullptr || pressed == nullptr) {
        return false;
    }
    return s_rd->getRemotePointer(x, y, pressed);
}
