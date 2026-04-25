/**
 * @file sd_mount.h
 * @brief Funcoes de montagem do SD partilhadas entre app.cpp e sd_hotplug.cpp.
 *
 * `mount_sd_fallback()` e `sd_rw_self_test()` sao definidas em app.cpp (para
 * acesso ao s_sd_spi static) e declaradas extern aqui para uso pelo hotplug.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Tenta montar o SD card com velocidades SPI crescentes (400k, 1M, 4M, 10M).
 * Retorna true se a montagem teve exito a alguma velocidade.
 * NAO chama SD.end() antes da primeira tentativa (chamar externamente se necessario).
 * Seguro para re-invocar apos remocao do cartao.
 */
bool mount_sd_fallback(void);

/**
 * Teste de escrita/leitura/remocao num ficheiro temporario na raiz do SD.
 * Retorna true se o SD esta funcional para leitura e escrita.
 * Chamar apenas depois de montagem bem-sucedida.
 */
bool sd_rw_self_test(void);

#ifdef __cplusplus
}
#endif
