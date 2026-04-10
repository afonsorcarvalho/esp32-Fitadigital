#pragma once

#include <stdbool.h>

/**
 * @brief Inicializa journal de boot na flash interna (SPIFFS).
 */
bool boot_journal_init(void);

/**
 * @brief Limpa o arquivo de boot atual e inicia novo ciclo.
 */
bool boot_journal_reset(void);

/**
 * @brief Acrescenta uma linha no journal interno.
 */
void boot_journal_append(const char *level, const char *message);

/**
 * @brief Copia journal interno para o SD.
 * @return true se a copia foi concluida.
 */
bool boot_journal_copy_to_sd(void);

/**
 * @brief Inicia task de espelhamento do journal para o SD.
 *
 * A task verifica periodicamente se o SD esta montado e atualiza
 * `/boot.log` no SD com o conteudo de `/boot.log` da flash interna.
 */
void boot_journal_start_sd_mirror_task(void);
