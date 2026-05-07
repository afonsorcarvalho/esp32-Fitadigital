#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lvgl.h>

/**
 * @file screenshot.h
 * @brief Captura snapshot do LVGL display, grava em SD como BMP RGB888.
 *
 * Mantem shadow buffer 768 KB em PSRAM continuamente actualizado via flush hook
 * LVGL (zero overhead alem de uma copia memcpy por flush). screenshot_capture_to_sd()
 * encoda BMP e escreve no SD on-demand.
 */

/* Inicializa: aloca shadow buffer PSRAM + regista flush hook LVGL.
 * Idempotente. Deve ser chamado depois de lvgl_port_init() e da inicializacao do LCD. */
void screenshot_init(void);

/* Hook chamado pelo flush_callback do LVGL. NAO chamar manualmente. */
void screenshot_flush_notify(const lv_area_t *area, const lv_color_t *color_map);

/* Captura screen actual e grava como BMP RGB888 em filepath (caminho absoluto SD,
 * ex: "/sd/screenshots/screen-20260505-153045.bmp"). Cria directorios se necessario.
 * Retorna true em sucesso. file_size = ~1.15 MB para 800x480. */
bool screenshot_capture_to_sd(const char *filepath);

/* Variante com delay pre-captura: worker faz vTaskDelay(delay_ms) antes de encodar.
 * Usar quando caller acabou de pedir wake do screensaver — da tempo ao timer LVGL
 * (1 Hz) de fechar o overlay e ao flush hook de copiar o frame fresco para o shadow
 * buffer. Recomendado >= 1200 ms. delay_ms=0 equivalente a screenshot_capture_to_sd. */
bool screenshot_capture_to_sd_delayed(const char *filepath, uint32_t delay_ms);

/* Retorna tamanho expectado do ficheiro BMP RGB888 em bytes (54 + width*height*3 com padding). */
size_t screenshot_bmp_size(void);
