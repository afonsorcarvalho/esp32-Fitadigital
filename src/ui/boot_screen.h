/**
 * @file boot_screen.h
 * @brief Ecra de arranque dedicado: fundo preto, linhas tipo terminal (pontos + [OK]/[ERROR]).
 *
 * @note As funcoes obtêm `lvgl_port_lock()` internamente (mutex recursivo do port).
 *
 * \warning Chame `boot_screen_destroy()` só depois de `lv_scr_load()` para o ecrã principal
 *          ou Wi-Fi; destruir o ecrã activo antes disso deixa o LVGL sem destino válido.
 */
#pragma once

#include <stddef.h>

typedef enum {
  BOOT_STEP_SD = 0,
  BOOT_STEP_RTC,
  BOOT_STEP_WIFI,
  BOOT_STEP_NTP,
  BOOT_STEP_FTP,
  BOOT_STEP_WIREGUARD,
  BOOT_STEP_WEB_PORTAL,
  BOOT_STEP_COUNT
} boot_step_t;

/** Constroi o ecra, define layout e chama `lv_scr_load`. Texto só em ASCII (compatível com fonte UI). */
void boot_screen_show(void);

/**
 * Actualiza a linha do passo com resultado tipo terminal: "1 SD ........ [OK]" usando só ASCII.
 * @param level "INFO", "WARN" ou "ERROR" -> [OK], [WARN], [ERROR].
 */
void boot_screen_set_step(boot_step_t step, const char *level);

void boot_screen_set_subtitle(const char *msg);

void boot_screen_set_footer(const char *msg);

/** Remove o ecra de arranque (após já ter carregado outro ecra com `lv_scr_load`). */
void boot_screen_destroy(void);
