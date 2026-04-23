/**
 * @file ota_manager.cpp
 * @brief ArduinoOTA push — estado centralizado para a UI LVGL.
 */
#include "ota_manager.h"
#include "app_log.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <atomic>
#include <cstring>

static const char *TAG = "OTA";

static OtaState  s_state    = OtaState::IDLE;
static uint8_t   s_progress = 0;
static char      s_error[128] = {};
static bool      s_handlers_registered = false;

// HTTP OTA — variáveis atômicas para thread-safety entre async_tcp (core 0) e LVGL (core 1)
static std::atomic<OtaState> s_http_state{OtaState::IDLE};
static std::atomic<uint8_t>  s_http_progress{0};
static char                  s_http_error[128] = {};

static void register_handlers(void) {
    if (s_handlers_registered) {
        return;
    }
    s_handlers_registered = true;

    ArduinoOTA.onStart([]() {
        s_state    = OtaState::RECEIVING;
        s_progress = 0;
        app_log_write("INFO", "OTA: inicio de recepcao");
    });

    ArduinoOTA.onProgress([](unsigned int received, unsigned int total) {
        s_progress = (total > 0) ? (uint8_t)((received * 100U) / total) : 0;
    });

    ArduinoOTA.onEnd([]() {
        s_progress = 100;
        s_state    = OtaState::DONE;
        app_log_write("INFO", "OTA: recepcao concluida; reboot em 1 s");
    });

    ArduinoOTA.onError([](ota_error_t err) {
        const char *reason = "desconhecido";
        switch (err) {
            case OTA_AUTH_ERROR:    reason = "falha de autenticacao"; break;
            case OTA_BEGIN_ERROR:   reason = "falha ao iniciar";      break;
            case OTA_CONNECT_ERROR: reason = "falha de ligacao";      break;
            case OTA_RECEIVE_ERROR: reason = "falha na recepcao";     break;
            case OTA_END_ERROR:     reason = "falha ao finalizar";    break;
            default: break;
        }
        snprintf(s_error, sizeof(s_error), "Erro OTA (%u): %s", (unsigned)err, reason);
        s_state = OtaState::ERROR;
        app_log_write("ERROR", s_error);
    });
}

bool ota_manager_start(void) {
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(s_error, sizeof(s_error), "Wi-Fi nao ligado");
        s_state = OtaState::ERROR;
        return false;
    }
    if (s_state == OtaState::LISTENING || s_state == OtaState::RECEIVING) {
        return true; /* ja ativo */
    }

    register_handlers();

    ArduinoOTA.setHostname("fitadigital");
    /* Sem senha por defeito (rede local controlada; documentar se necessario). */
    ArduinoOTA.begin();

    s_state    = OtaState::LISTENING;
    s_progress = 0;
    s_error[0] = '\0';
    app_log_write("INFO", "OTA: servidor iniciado; aguardando push");
    return true;
}

void ota_manager_stop(void) {
    if (s_state == OtaState::IDLE) {
        return;
    }
    ArduinoOTA.end();
    s_state    = OtaState::IDLE;
    s_progress = 0;
    s_error[0] = '\0';
    app_log_write("INFO", "OTA: servidor parado");
}

void ota_manager_loop(void) {
    if (s_state == OtaState::LISTENING || s_state == OtaState::RECEIVING) {
        ArduinoOTA.handle();
    }
}

OtaState ota_manager_state(void) {
    return s_state;
}

uint8_t ota_manager_progress(void) {
    return s_progress;
}

const char *ota_manager_error_msg(void) {
    return s_error;
}

void ota_http_begin(void) {
    s_http_state.store(OtaState::HTTP_RECEIVING, std::memory_order_relaxed);
    s_http_progress.store(0, std::memory_order_relaxed);
}

void ota_http_set_progress(uint8_t pct) {
    s_http_progress.store(pct, std::memory_order_relaxed);
}

void ota_http_set_done(void) {
    s_http_progress.store(100, std::memory_order_relaxed);
    s_http_state.store(OtaState::HTTP_DONE, std::memory_order_relaxed);
}

void ota_http_set_error(const char *msg) {
    if (msg) {
        strncpy(s_http_error, msg, sizeof(s_http_error) - 1);
        s_http_error[sizeof(s_http_error) - 1] = '\0';
    } else {
        s_http_error[0] = '\0';
    }
    s_http_state.store(OtaState::HTTP_ERROR, std::memory_order_relaxed);
}

OtaState ota_http_state(void) {
    return s_http_state.load(std::memory_order_relaxed);
}

uint8_t ota_http_progress(void) {
    return s_http_progress.load(std::memory_order_relaxed);
}

const char *ota_http_error_msg(void) {
    return s_http_error;
}
