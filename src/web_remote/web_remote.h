/**
 * @file web_remote.h
 * @brief Acesso remoto ao display LVGL via browser (WebSocket + JPEG stream).
 *
 * Servidor HTTP (porta 80) serve uma página HTML; via WebSocket binário (/ws)
 * envia frames JPEG comprimidos (downscale configuravel via UI) e recebe eventos
 * de mouse/touch do browser.
 *
 * Dependências PlatformIO:
 *   - ESP32Async/AsyncTCP
 *   - ESP32Async/ESPAsyncWebServer
 *   - esp-arduino-libs/ESP32_JPEG (encoder JPEG)
 */
#pragma once

#include <lvgl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inicia o servidor HTTP + WebSocket e o encoder JPEG para acesso remoto.
 * Deve ser chamado após net_services_start_background_task() (WiFi pode
 * ainda não estar conectado — ESPAsyncWebServer lida com isso internamente).
 */
void web_remote_init(void);

/**
 * Acumula dirty region e, no último flush do frame (last == true),
 * dispara encode JPEG + envio via WebSocket (se houver cliente
 * conectado e o throttle permitir).
 *
 * Chamado a partir do flush_callback em lvgl_port_v8.cpp.
 *
 * @param area      Região suja do frame atual
 * @param color_map Ponteiro para o framebuffer completo (direct mode)
 * @param last      true quando lv_disp_flush_is_last() == true
 */
void web_remote_flush(const lv_area_t *area, const lv_color_t *color_map, bool last);

/**
 * Retorna o estado do ponteiro remoto (mouse/touch do browser).
 *
 * @param x       [out] Coordenada X (800×480)
 * @param y       [out] Coordenada Y (800×480)
 * @param pressed [out] true se pressionado
 * @return true se existe um cliente remoto ativo que já enviou dados de ponteiro
 */
bool web_remote_get_pointer(lv_coord_t *x, lv_coord_t *y, bool *pressed);

typedef struct {
  uint8_t scale;          /**< Fator de downscale (1..8). */
  uint8_t jpeg_quality;   /**< Qualidade JPEG (1..100). */
  uint16_t interval_ms;   /**< Intervalo minimo entre frames em ms (80..2000). */
} web_remote_stream_cfg_t;

/** Retorna a configuracao atual do stream remoto. */
void web_remote_get_stream_config(web_remote_stream_cfg_t *cfg);

/** Atualiza a configuracao do stream remoto e notifica clientes conectados. */
void web_remote_set_stream_config(const web_remote_stream_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
