/**
 * @file lvgl_remote_server.h
 * @brief Integração LVGLRemoteServer (UDP/RLE) — activo quando USE_LVGL_REMOTE_SERVER=1.
 *
 * Documentação upstream: https://github.com/CubeCoders/LVGLRemoteServer
 */
#pragma once

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void lvgl_remote_server_init(void);
void lvgl_remote_server_flush(const lv_area_t *area, lv_color_t *color_map);
void lvgl_remote_server_poll_input(void);
bool lvgl_remote_server_get_pointer(lv_coord_t *x, lv_coord_t *y, bool *pressed);

#ifdef __cplusplus
}
#endif
