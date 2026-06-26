/**
 * @file cycle_integrity.h
 * @brief Cadeia HMAC-SHA256 (truncada a 4 bytes) inline nos .txt de ciclos.
 *
 * Cada linha gravada passa a "<linha>\t<seq>\t<mac8hex>". O mac encadeia na linha
 * anterior, logo editar/inserir/remover/reordenar quebra a cadeia a partir do ponto
 * alterado. Verificacao offline no PC com a mesma password (tools/verify_integrity.py).
 *
 * Modelo de seguranca e limites: ver docs/superpowers/plans/2026-06-25-... (password
 * hardcoded global; protege adulteracao casual, nao reverse do firmware; truncamento
 * da cauda nao detetavel localmente).
 *
 * IMPORTANTE: prepare()/compose() mexem em estado partilhado e fazem I/O de SD.
 * Chamar APENAS no contexto sd_io (via sd_access_sync), tal como os escritores .txt.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Le a password embutida e o device id. Idempotente. Chamar no boot. */
void cycle_integrity_init(void);

/** true se a feature esta' ligada (app_settings_integrity_enabled). */
bool cycle_integrity_enabled(void);

/**
 * Sincroniza a cadeia para `path` (deteta mudanca de ficheiro) e, se for um ficheiro
 * novo/legado sem cabecalho, compoe a linha de cabecalho `#FDIGI-INT ...` (sem '\n')
 * em `hdr`. Faz recuperacao por tail se o ficheiro ja' estiver assinado.
 *
 * @return numero de bytes escritos em `hdr` (cabecalho a gravar ANTES da linha de
 *         dados), ou 0 se nao e' preciso cabecalho. Chamar no contexto sd_io.
 */
size_t cycle_integrity_prepare(const char *path, char *hdr, size_t hdr_cap);

/**
 * Compoe a proxima linha de dados da cadeia corrente: "<raw>\t<seq>\t<mac8hex>" (sem '\n').
 * Avanca o estado da cadeia. Chamar apos cycle_integrity_prepare() para o mesmo `path`.
 *
 * @return numero de bytes escritos em `out`, ou 0 em erro. Chamar no contexto sd_io.
 */
size_t cycle_integrity_compose(const char *raw, size_t raw_len, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
