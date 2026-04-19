/**
 * @file splash_screen.h
 * @brief Splash de boot com logo da empresa (AFR Soluções Inteligentes).
 *
 * Exibido após o checklist de arranque e antes da interface principal.
 * A duração é configurável via NVS/fdigi.cfg (chave splash_s, default 3 s).
 *
 * @note As funções obtêm `lvgl_port_lock()` internamente.
 */
#pragma once

/**
 * Cria o ecrã de splash com a logo centralizada, carrega-o e
 * destrói o ecrã de boot (checklist) de forma segura.
 */
void splash_screen_show(void);

/**
 * Remove o ecrã de splash (chamar apenas depois de lv_scr_load para outro ecrã).
 */
void splash_screen_destroy(void);
