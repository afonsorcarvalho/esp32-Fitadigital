/**
 * @file rs485_buffer.h
 * @brief Buffer SPIFFS fallback para linhas RS485 quando o SD nao esta montado.
 *
 * Quando o SD e' removido durante captura RS485, as linhas sao gravadas em
 * `/rs485_pending.log` no SPIFFS interno (formato `<unix_epoch>\t<linha>\n`).
 * Ao reinserir o SD, `rs485_buffer_flush_to_sd()` e' invocado pelo ticker do
 * sd_hotplug e drena as linhas para os ficheiros /CICLOS/AAAA/MM/AAAAMMDD.txt
 * correctos, usando o timestamp original de cada linha.
 *
 * Quota: 2 MB — a ~9600 bps (~960 B/s) equivale a ~35 min de captura.
 * Overflow: drop-newest com log unico. Sem ring-buffer (SPIFFS nao suporta
 * head-truncate eficiente).
 *
 * Thread safety: append e flush correm ambos no contexto sd_io (sd_access_sync
 * serializa o append; flush e' chamado do ticker sd_hotplug, tambem sd_io).
 * Sem mutex extra necessario.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inicializa o modulo.
 * Chamar apos SPIFFS ja estar montado (boot_journal_init faz o mount).
 * Se o ficheiro pendente existir e o SD ja estiver montado, faz flush imediato.
 */
void rs485_buffer_init(void);

/**
 * Adiciona uma linha ao buffer SPIFFS.
 * Descarta silenciosamente se:
 *   - RTC invalido (epoch < 2020-01-01)
 *   - Buffer atingiu 2 MB (drop-newest, log unico)
 * Executar no contexto sd_io (via sd_access_sync em cycles_rs485).
 * @param line  Bytes da linha (sem LF/CR terminal).
 * @param len   Comprimento em bytes.
 */
void rs485_buffer_append(const char *line, size_t len);

/**
 * Drena o buffer SPIFFS para o SD.
 * Abre `/rs485_pending.log`, processa linha a linha, distribui em
 * /CICLOS/AAAA/MM/AAAAMMDD.txt usando o timestamp original.
 * Remove o ficheiro SPIFFS apos EOF bem-sucedido.
 * Se o SD falhar a meio: aborta e mantem o ficheiro SPIFFS intacto.
 * Executar no contexto sd_io (ticker sd_hotplug).
 */
void rs485_buffer_flush_to_sd(void);

/**
 * Retorna o tamanho actual do ficheiro de buffer em bytes (0 se nao existe).
 * Pode ser chamado de qualquer contexto (apenas leitura de metadata SPIFFS).
 */
size_t rs485_buffer_size_bytes(void);

#ifdef __cplusplus
}
#endif
