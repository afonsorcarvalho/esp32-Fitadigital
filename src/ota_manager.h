/**
 * @file ota_manager.h
 * @brief Gestao de atualizacao OTA por ArduinoOTA (push via IDE/espota.py).
 *
 * Decisao de arquitetura: ArduinoOTA (push) em vez de HTTP pull.
 * Razao: sem dependencia de servidor externo; espota.py ou IDE envia o
 * binario diretamente; implementacao minima sem NVS adicional.
 * Sem validacao de assinatura (ArduinoOTA nao suporta por padrao;
 * documentado aqui para referencia futura).
 *
 * Ciclo de vida:
 *   ota_manager_start()  — inicia escuta; chamar apos PIN validado.
 *   ota_manager_loop()   — chamar periodicamente (fora do mutex LVGL).
 *   ota_manager_stop()   — cancela escuta sem reiniciar.
 *   ota_manager_state()  — estado atual para a UI.
 */
#pragma once

#include <stdint.h>

enum class OtaState : uint8_t {
    IDLE,            /**< Escuta nao iniciada */
    LISTENING,       /**< Aguardando push do IDE/espota.py */
    RECEIVING,       /**< A receber binario (progresso 0-100) */
    DONE,            /**< Concluido; reboot pendente */
    ERROR,           /**< Falha; mensagem em ota_manager_error_msg() */
    HTTP_RECEIVING,  /**< A receber via HTTP upload (progresso 0-100) */
    HTTP_DONE,       /**< Upload HTTP concluido; reboot pendente */
    HTTP_ERROR,      /**< Falha HTTP; mensagem em ota_http_error_msg() */
};

/** Inicia o servidor ArduinoOTA. Requer Wi-Fi conectado. */
bool ota_manager_start(void);

/** Para o servidor ArduinoOTA sem reiniciar o dispositivo. */
void ota_manager_stop(void);

/**
 * Deve ser chamado frequentemente pela tarefa de rede (ou loop).
 * Chama ArduinoOTA.handle() e atualiza estado interno.
 * Seguro chamar mesmo em IDLE (no-op).
 */
void ota_manager_loop(void);

OtaState ota_manager_state(void);

/** Progresso da recepcao em percentagem (0-100). Valido apenas em RECEIVING. */
uint8_t ota_manager_progress(void);

/** Mensagem de erro (valida apenas em ERROR). Ponteiro estatico — copiar se necessario. */
const char *ota_manager_error_msg(void);

/**
 * @name API para OTA HTTP (upload .bin via browser)
 * Funcoes chamadas pelo handler web_portal.cpp durante upload HTTP.
 * Thread-safe: handler (async_tcp, core 0) escreve; UI LVGL (core 1) le.
 * @{
 */

/** Inicia upload HTTP — chamar no primeiro chunk. */
void ota_http_begin(void);

/** Atualiza progresso durante upload (0-100). */
void ota_http_set_progress(uint8_t pct);

/** Flash concluido com sucesso — chamar no final. */
void ota_http_set_done(void);

/** Erro durante upload — chamar se Update falhar. */
void ota_http_set_error(const char *msg);

/** Estado atual do upload HTTP. */
OtaState ota_http_state(void);

/** Progresso atual do upload HTTP (0-100). */
uint8_t ota_http_progress(void);

/** Mensagem de erro HTTP (valida apenas em HTTP_ERROR). */
const char *ota_http_error_msg(void);

/** @} **/
