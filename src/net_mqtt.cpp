/**
 * @file net_mqtt.cpp
 * @brief Cliente MQTT assincrono (espMqttClient via AsyncTCP).
 *
 * Task FreeRTOS `mqtt_svc` (core 0, prio 1) gere ligacao, reconexao com
 * backoff exponencial e publicacao periodica de telemetria de saude.
 *
 * Nao usa TLS (v1). Para TLS, substituir espMqttClientAsync por espMqttClientSecure.
 *
 * Invariante: s_client e' apenas tocado dentro da task mqtt_svc ou dos
 * callbacks do espMqttClient (que correm no contexto do AsyncTCP, core 0).
 * Nunca chamar s_client.* fora desses contextos sem sincronizacao adicional.
 */
#include "net_mqtt.h"
#include "app_settings.h"
#include "sd_access.h"

#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <time.h>

#include <espMqttClientAsync.h>
#include <ArduinoJson.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "net_mqtt";

/* ------------------------------------------------------------------ */
/* Estado interno                                                        */
/* ------------------------------------------------------------------ */

static espMqttClientAsync s_client;
static MqttStatus   s_status      = MqttStatus::Disabled;
static char         s_last_error[80] = {};
static uint32_t     s_next_attempt_ms = 0;
static uint8_t      s_backoff_idx  = 0;
static bool         s_suspended    = false;
static bool         s_task_created = false;

/* Momento da ultima publicacao de telemetria (ms). */
static uint32_t     s_last_telemetry_ms = 0;

static constexpr uint32_t kBackoffTableMs[] = {5000, 10000, 20000, 40000, 60000, 60000};
static constexpr size_t   kBackoffCount      = sizeof(kBackoffTableMs) / sizeof(kBackoffTableMs[0]);

/* ------------------------------------------------------------------ */
/* Helpers                                                               */
/* ------------------------------------------------------------------ */

/* Cache do base topic em RAM para evitar NVS getString em cada publish.
 * Refrescado em apply_settings e on_connect. NVS read consome ~3 KB de stack
 * (Preferences::getString -> nvs -> spi_flash mutex), o que crashou a task
 * mqtt_kw a primeira vez que tentou publicar uma keyword (Fase 4 v1). */
static char s_base_topic_cached[160] = {0};

static void refresh_base_topic_cache(void) {
    String base = app_settings_mqtt_base_topic();
    strncpy(s_base_topic_cached, base.c_str(), sizeof(s_base_topic_cached) - 1U);
    s_base_topic_cached[sizeof(s_base_topic_cached) - 1U] = '\0';
}

/** Constroi o topic completo: base_topic + suffix. Usa cache em RAM. */
static void build_topic(char *buf, size_t cap, const char *suffix) {
    if (s_base_topic_cached[0] == '\0') {
        refresh_base_topic_cache();
    }
    snprintf(buf, cap, "%s%s", s_base_topic_cached, suffix);
}

/** Publica JSON de telemetria no topico /status. */
static void publish_telemetry(void) {
    if (!s_client.connected() || s_suspended) {
        return;
    }

    /* Fix 4 v2: skip se heap interna < 8 KB — evita pico de alloc do StaticJsonDocument
     * + String WiFi.localIP + SD bytes quando heap ja esta sob pressao.
     * Originalmente 9 KB; baixou para 8 KB porque baseline pos-MQTT e ~14 KB e 9 KB
     * estava a fazer skip na maioria dos ticks, escondendo o estado. */
    const uint32_t heap_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (heap_int < 8192U) {
        ESP_LOGW(TAG, "skip telemetry: heap_int_free=%u < 8KB", (unsigned)heap_int);
        s_last_telemetry_ms = millis(); /* evitar busy-loop */
        return;
    }

    StaticJsonDocument<384> doc;
    doc["online"]          = true;
    doc["ts"]              = (uint32_t)time(nullptr);
    doc["uptime_s"]        = (uint32_t)(millis() / 1000UL);
    doc["heap_int_free"]   = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    doc["heap_int_min"]    = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    doc["heap_psram_free"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    doc["boot_count"]      = app_settings_boot_count_get();
    doc["heap_guard_reboots"] = app_settings_heap_guard_count_get();
    bool mounted = sd_access_is_mounted();
    doc["sd_mounted"] = mounted;
    if (mounted) {
        uint64_t total = 0, used = 0;
        sd_access_sync([&] {
            total = SD.totalBytes();
            used  = SD.usedBytes();
        });
        doc["sd_total_b"] = (uint32_t)(total > UINT32_MAX ? UINT32_MAX : total);
        doc["sd_used_b"]  = (uint32_t)(used  > UINT32_MAX ? UINT32_MAX : used);
        uint64_t free64   = total > used ? (total - used) : 0;
        doc["sd_free_b"]  = (uint32_t)(free64 > UINT32_MAX ? UINT32_MAX : free64);
    }
    doc["wifi_rssi_dbm"]   = WiFi.RSSI();
    doc["ip"]              = WiFi.localIP().toString();
    doc["fw_ver"]          = FITADIGITAL_VERSION;

    char payload[384];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0 || n >= sizeof(payload)) {
        ESP_LOGW(TAG, "telemetria JSON overflow ou vazio");
        return;
    }

    char topic[160];
    build_topic(topic, sizeof(topic), "/status");
    /* QoS 1, retain false — telemetria e' ponto-no-tempo, nao precisa de retain. */
    s_client.publish(topic, 1, false, payload);
    s_last_telemetry_ms = millis();
    ESP_LOGD(TAG, "telemetria publicada (%u bytes)", (unsigned)n);
}

/** Publica retain {"online":true} no LWT topic para sobrepor LWT apos reconexao. */
static void publish_online_retain(void) {
    char topic[160];
    build_topic(topic, sizeof(topic), "/status");
    /* Payload minimo: apenas online=true, retain=true — sobrepoe o LWT. */
    s_client.publish(topic, 1, true, "{\"online\":true}");
}

/* ------------------------------------------------------------------ */
/* Callbacks espMqttClient                                               */
/* ------------------------------------------------------------------ */

static void on_connect(bool session_present) {
    (void)session_present;
    s_status      = MqttStatus::Connected;
    s_backoff_idx = 0;
    s_last_error[0] = '\0';
    /* Refresh do cache de base topic — evita que outros publishers (mqtt_kw)
     * batam em NVS dentro da sua propria task com stack apertada. */
    refresh_base_topic_cache();
    ESP_LOGI(TAG, "conectado ao broker");
    publish_online_retain();
    publish_telemetry();
}

static void on_disconnect(espMqttClientTypes::DisconnectReason reason) {
    /* Distinguir desconexao esperada (disabled) de erro de rede. */
    if (s_status == MqttStatus::Disabled) {
        /* Desconexao pedida por nos (apply_settings ou WiFi caiu). */
        return;
    }
    snprintf(s_last_error, sizeof(s_last_error), "disconnect reason=%u", (unsigned)reason);
    s_status = MqttStatus::Connecting;
    /* Backoff: proximo intento apos intervalo crescente. */
    const uint32_t delay_ms = kBackoffTableMs[s_backoff_idx];
    s_next_attempt_ms = (uint32_t)(millis() + delay_ms);
    if (s_backoff_idx < kBackoffCount - 1U) {
        s_backoff_idx++;
    }
    ESP_LOGW(TAG, "desconectado (reason=%u); retry em %lu ms", (unsigned)reason, (unsigned long)delay_ms);
}

static void on_message(const espMqttClientTypes::MessageProperties & /*props*/,
                        const char * /*topic*/, const uint8_t * /*payload*/,
                        size_t /*len*/, size_t /*index*/, size_t /*total*/) {
    /* Sem subscricoes em v1. */
}

/* ------------------------------------------------------------------ */
/* Task principal                                                         */
/* ------------------------------------------------------------------ */

static void mqtt_svc_task(void *) {
    /* Tick 200 ms: responsive sem desperdicar CPU. */
    constexpr TickType_t kTickMs = pdMS_TO_TICKS(200);

    for (;;) {
        /* --- Caso 1: desativado ou sem WiFi --- */
        if (!app_settings_mqtt_enabled() || !WiFi.isConnected()) {
            if (s_client.connected()) {
                s_status = MqttStatus::Disabled;
                s_client.disconnect();
            } else {
                s_status = MqttStatus::Disabled;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* --- Caso 2: habilitado, nao conectado, aguardar backoff --- */
        if (!s_client.connected()) {
            const uint32_t now = (uint32_t)millis();
            if ((int32_t)(now - s_next_attempt_ms) < 0) {
                /* Ainda dentro do backoff. */
                vTaskDelay(kTickMs);
                continue;
            }

            /* Configurar e ligar. CRITICO: espMqttClient guarda PONTEIROS para as
             * strings dos setters (host, user, pass, clientId, will topic) — nao copia.
             * Buffers stack do scope { } ficam dangling apos sair, e a lib le-os no
             * connect() (e em reconexoes). Logo todos os buffers tem que ser estaticos
             * (ou membros de classe com lifetime equivalente). Sintoma observado:
             * Mosquitto reject "client identifier not valid" porque o cid stack
             * estava ja sobrescrito quando a lib o leu na fase CONNECT do MQTT. */
            static char s_mqtt_host_buf[96];
            static char s_mqtt_user_buf[64];
            static char s_mqtt_pass_buf[64];
            static char s_mqtt_cid_buf[32];
            static char s_mqtt_lwt_topic_buf[160];

            String host = app_settings_mqtt_host();
            if (host.length() == 0) {
                s_status = MqttStatus::Disabled;
                s_last_error[0] = '\0';
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            strncpy(s_mqtt_host_buf, host.c_str(), sizeof(s_mqtt_host_buf) - 1U);
            s_mqtt_host_buf[sizeof(s_mqtt_host_buf) - 1U] = '\0';

            s_client.setServer(s_mqtt_host_buf, app_settings_mqtt_port());

            /* Sempre actualizar credenciais para reflectir mudancas via apply_settings.
             * Strings vazias = sem autenticacao (passar nullptr). */
            {
                String user = app_settings_mqtt_user();
                String pass = app_settings_mqtt_pass();
                if (user.length() > 0) {
                    strncpy(s_mqtt_user_buf, user.c_str(), sizeof(s_mqtt_user_buf) - 1U);
                    s_mqtt_user_buf[sizeof(s_mqtt_user_buf) - 1U] = '\0';
                } else {
                    s_mqtt_user_buf[0] = '\0';
                }
                if (pass.length() > 0) {
                    strncpy(s_mqtt_pass_buf, pass.c_str(), sizeof(s_mqtt_pass_buf) - 1U);
                    s_mqtt_pass_buf[sizeof(s_mqtt_pass_buf) - 1U] = '\0';
                } else {
                    s_mqtt_pass_buf[0] = '\0';
                }
                s_client.setCredentials(
                    s_mqtt_user_buf[0] != '\0' ? s_mqtt_user_buf : nullptr,
                    s_mqtt_pass_buf[0] != '\0' ? s_mqtt_pass_buf : nullptr
                );
            }

            /* ClientId unico: base no MAC (3 bytes). 14 chars total — dentro do
             * limite MQTT 3.1 (23 chars), so alfanumericos + underscore. */
            {
                uint64_t mac = ESP.getEfuseMac();
                snprintf(s_mqtt_cid_buf, sizeof(s_mqtt_cid_buf), "fitadig_%02x%02x%02x",
                         (unsigned)((mac >> 16) & 0xFF),
                         (unsigned)((mac >>  8) & 0xFF),
                         (unsigned)(mac & 0xFF));
                s_client.setClientId(s_mqtt_cid_buf);
            }

            /* LWT: broker publica quando a conexao cai. */
            build_topic(s_mqtt_lwt_topic_buf, sizeof(s_mqtt_lwt_topic_buf), "/status");
            s_client.setWill(s_mqtt_lwt_topic_buf, 1, true, "{\"online\":false}");

            /* Registar callbacks (idempotente: espMqttClient substitui). */
            s_client.onConnect(on_connect);
            s_client.onDisconnect(on_disconnect);
            s_client.onMessage(on_message);

            s_status = MqttStatus::Connecting;
            ESP_LOGI(TAG, "a ligar a %s:%u", host.c_str(), (unsigned)app_settings_mqtt_port());
            s_client.connect();

            vTaskDelay(kTickMs);
            continue;
        }

        /* --- Caso 3: conectado --- */
        if (!s_suspended) {
            const uint32_t now = (uint32_t)millis();
            const uint32_t iv_ms = (uint32_t)app_settings_mqtt_telemetry_interval_s() * 1000UL;
            if ((now - s_last_telemetry_ms) >= iv_ms) {
                publish_telemetry();
            }
        }

        vTaskDelay(kTickMs);
    }
}

/* ------------------------------------------------------------------ */
/* API publica                                                           */
/* ------------------------------------------------------------------ */

void net_mqtt_init(void) {
    if (s_task_created) {
        return; /* Idempotente. */
    }
    s_task_created = true;
    s_status = MqttStatus::Disabled;

    /* Task no core 0 — mesmo core do AsyncTCP (CONFIG_ASYNC_TCP_RUNNING_CORE=1 nao, e core 0 aqui).
     * Nota: platformio.ini define CONFIG_ASYNC_TCP_RUNNING_CORE=1 (core 1). Manter mqtt_svc
     * no core 0 garante que os callbacks do espMqttClient (que correm no core AsyncTCP=1)
     * nao colidem com a task mqtt_svc na escrita de s_status (variaveis simples, sem mutex,
     * acesso nao critico para leitura pela UI). Se necessario mutex no futuro, adicionar aqui. */
    BaseType_t ok = xTaskCreatePinnedToCore(
        mqtt_svc_task, "mqtt_svc",
        4096U, nullptr, 1U,
        nullptr, 0 /* core 0 */
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "falha ao criar task mqtt_svc");
        s_task_created = false;
        return;
    }

    ESP_LOGI(TAG, "init: host=%s port=%u topic=%s iv=%us enabled=%d",
             app_settings_mqtt_host().c_str(),
             (unsigned)app_settings_mqtt_port(),
             app_settings_mqtt_base_topic().c_str(),
             (unsigned)app_settings_mqtt_telemetry_interval_s(),
             (int)app_settings_mqtt_enabled());
}

void net_mqtt_apply_settings(void) {
    /* Forcar reconexao: desligar (se conectado), resetar backoff, tentar imediatamente. */
    if (s_client.connected()) {
        /* on_disconnect vai ser chamado pelo espMqttClient; para evitar retry automatico
         * de on_disconnect, marcar como Disabled antes de chamar disconnect(). */
        s_status = MqttStatus::Disabled;
        s_client.disconnect();
    }
    s_backoff_idx     = 0;
    s_next_attempt_ms = 0; /* Tentar imediatamente no proximo tick. */
    s_last_error[0]   = '\0';
    /* Re-cache base topic (pode ter sido alterado pela UI). */
    refresh_base_topic_cache();
    ESP_LOGI(TAG, "apply_settings: reconexao forcada");
}

MqttStatus net_mqtt_status(void) {
    return s_status;
}

const char *net_mqtt_last_error(void) {
    return s_last_error;
}

bool net_mqtt_publish(const char *topic_suffix, const char *payload, bool retain) {
    if (!s_client.connected() || s_suspended) {
        return false;
    }
    char topic[160];
    build_topic(topic, sizeof(topic), topic_suffix);
    return s_client.publish(topic, 1, retain, payload) != 0;
}

void net_mqtt_set_suspended(bool suspended) {
    s_suspended = suspended;
    ESP_LOGD(TAG, "suspended=%d", (int)suspended);
}
