/**
 * @file cycles_rs485.h
 * @brief Leitura RS485 (Serial1) e gravacao de cada linha em /CICLOS/AAAA/MM/AAAAMMDD.txt no SD.
 *
 * Layout de pastas e ficheiro (data local do sistema):
 *   /CICLOS/<ano>/<mes>/<AAAAMMDD>.txt
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

/**
 * Preenche o caminho absoluto VFS do ficheiro .txt do dia corrente (hora local).
 * @return false se buffer invalido ou localtime falhou.
 */
bool cycles_rs485_format_today_path(char *out, size_t out_sz);

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
