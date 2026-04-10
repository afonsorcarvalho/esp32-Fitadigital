/**
 * @file app_settings_sd.h
 * @brief Copia espelho das preferencias em ficheiro na raiz do volume SD.
 *
 * Ficheiro: kAppSettingsSdConfigPath (raiz do SD; no PC aparece como /fdigi.cfg na raiz FAT), formato texto leve
 * com linhas chave=valor e seccoes [nome]. Ver comentarios no topo do ficheiro
 * gerado em app_settings.cpp (funcao app_settings_sync_config_file_to_sd).
 */
#pragma once

#include <stdbool.h>

/**
 * Caminho para SD.open() na raiz do volume (ficheiro real em /sd/fdigi.cfg no VFS).
 * Nao' usar "/sd/..." aqui: a classe SD do Arduino-ESP32 prefixa o mount point.
 */
extern const char *const kAppSettingsSdConfigPath;

/**
 * Le o ficheiro de configuracao; se o cabecalho e os valores forem validos,
 * grava na NVS (namespace fdigi). Se invalido ou SD ausente, nao altera NVS.
 * @return true se importou com sucesso.
 */
bool app_settings_try_load_from_sd_config(void);

/** Escreve o estado actual da NVS para o ficheiro (ignora se SD nao montado). */
void app_settings_sync_config_file_to_sd(void);
