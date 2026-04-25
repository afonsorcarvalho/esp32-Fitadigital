/**
 * @file cycles_rs485.h
 * @brief Leitura RS485 (Serial1) e gravacao de cada linha em /CICLOS/AAAA/MM/AAAAMMDD.txt no SD.
 *
 * Layout de pastas e ficheiro (data local do sistema):
 *   /CICLOS/<ano>/<mes>/<AAAOMMDD>.txt
 * Ex.: 13/04/2026 -> /CICLOS/2026/04/20260413.txt
 *
 * Cada linha recebida (terminada em LF ou CR, estilo RS232): grava no ficheiro do dia
 * (abre, escreve a linha + LF, fecha). A UI so segue o ficheiro apos
 * `cycles_rs485_set_line_to_ui_follow(true)` (ex.: timer pos-boot).
 * Pinos RS485: ver `board_pins.h` (documentacao Waveshare).
 *
 * Teste de TX opcional (macro CYCLES_RS485_TX_TEST_EVERY_5S=1 em build_flags): envio periodico
 * de bytes de teste pela Serial1 — desativado por defeito em producao.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Preenche o caminho absoluto VFS do ficheiro .txt do dia corrente (hora local).
 * @return false se buffer invalido ou localtime falhou.
 */
bool cycles_rs485_format_today_path(char *out, size_t out_sz);

/**
 * Formata o caminho /CICLOS/AAAA/MM/AAAAMMDD.txt a partir de um struct tm local.
 * Exposto para reutilizacao por rs485_buffer (flush de linhas com timestamps historicos).
 * @param lt  Estrutura tm ja' preenchida (hora local).
 * @param out Buffer de destino (minimo 48 bytes).
 * @param cap Tamanho de out.
 * @return true se sucesso.
 */
bool cycles_rs485_format_path_from_tm(const struct tm *lt, char *out, size_t cap);

/**
 * Cria /CICLOS, /CICLOS/AAAA e /CICLOS/AAAA/MM se necessario.
 * Exposto para reutilizacao por rs485_buffer (flush).
 * Executar apenas no contexto sd_io.
 */
void cycles_rs485_mkdirs_for_ym(int year, int month);

/**
 * Ativa ou desativa o sinal a UI para abrir/atualizar o .txt do dia apos cada linha gravada.
 * Por defeito falso no arranque; p.ex. o explorador agenda `true` apos estabilizar (timer LVGL).
 */
void cycles_rs485_set_line_to_ui_follow(bool enabled);

/** Inicia Serial1 e tarefa de leitura (so chamar com SD montado e sd_io a correr). */
void cycles_rs485_init(void);

/**
 * Encerra Serial1 e remove as tarefas RS485 (para reaplicar baud/trama apos alterar NVS).
 * Seguro chamar mesmo se `cycles_rs485_init` ainda nao tiver corrido com sucesso.
 */
void cycles_rs485_deinit(void);

/** Reabre a UART com `app_settings_rs485_*` (deinit + init se o SD estiver montado). */
void cycles_rs485_apply_settings(void);

/**
 * Numero de linhas ja' gravadas no ficheiro do dia corrente (reset em mudanca de dia).
 * Contado a inicializacao (scan do ficheiro) e incrementado a cada linha gravada.
 */
uint32_t cycles_rs485_today_line_count(void);

/**
 * Formata a hora da ultima gravacao como "HH:MM:SS" no fuso local; escreve "--:--:--"
 * se nenhuma linha foi gravada ainda nesta sessao.
 */
void cycles_rs485_last_write_hhmmss(char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif
