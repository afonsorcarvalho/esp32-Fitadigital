/**
 * @file ui_share_qr.h
 * @brief Modal com QR code para partilhar um ficheiro do SD via URL remota.
 */
#pragma once

/**
 * Abre o modal com um QR code codificando
 * `<url_base>?path=<url_encoded_path>`. Se `app_settings_download_url()`
 * estiver vazio, usa o portal proprio do dispositivo
 * (`http://<IP>/api/fs/file`). Em erro (Wi-Fi em baixo, path invalido),
 * mostra toast.
 */
void ui_share_qr_show(const char *path);
