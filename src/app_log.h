#pragma once

#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Logger em arquivo rotativo no SD (raiz: /fdigi.log).
 *
 * Formato de linha:
 *   AAAA-MM-DD HH:MM:SS | NIVEL | mensagem
 */
void app_log_init(void);

/** Grava uma linha no log (ignora se SD indisponivel). */
void app_log_write(const char *level, const char *message);

/** Versao formatada (printf-like) para gravar no log. */
void app_log_writef(const char *level, const char *fmt, ...);

/**
 * @brief Grava no log com etiqueta de feature.
 *
 * Formato da mensagem final:
 *   [FEATURE] mensagem
 */
void app_log_feature_write(const char *level, const char *feature, const char *message);

/** Versao formatada (printf-like) com etiqueta de feature. */
void app_log_feature_writef(const char *level, const char *feature, const char *fmt, ...);

/** Le as ultimas linhas/bytes do log para mostrar na UI. */
bool app_log_read_tail(char *buf, size_t buf_size, size_t *out_file_size, bool *out_truncated);

/** Limpa o arquivo de log (mantem o arquivo criado vazio). */
bool app_log_clear(void);

/**
 * Notificação opcional de cada linha escrita (mesmo texto que no ficheiro, sem \n final).
 * Usado pelo portal web (WebSocket). Pode ser chamado a partir de qualquer tarefa.
 */
void app_log_set_line_notify(void (*fn)(const char *line, void *user), void *user);
void app_log_clear_line_notify(void);

const char *app_log_path(void);
size_t app_log_max_size_bytes(void);
