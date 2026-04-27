/**
 * @file net_mqtt_keywords.cpp
 * @brief Detecao de palavras-chave RS485 e publicacao MQTT (Fase 4).
 *
 * Task FreeRTOS `mqtt_kw` (core 1, prio 1, stack 3072 B).
 * Tick 5 s. Em cada tick:
 *   1. Verifica se MQTT esta ligado e ha palavras-chave configuradas.
 *   2. Le o chunk novo do ficheiro do dia via sd_access_sync.
 *   3. Processa as ultimas 10 linhas do chunk contra cada keyword.
 *   4. Em match, publica JSON via net_mqtt_publish("/keyword", ...).
 *
 * AVISO: por iteracao sao processadas no maximo as ultimas 10 linhas,
 * independentemente de quantas chegaram no intervalo de 5 s.
 * Este limite e' intencional para evitar picos de heap e latencia.
 *
 * Buffer de leitura s_buf e' estatico (namespace anonimo) — nao vai para
 * a stack da task (que e' apenas 3 KB).
 */
#include "net_mqtt_keywords.h"
#include "net_mqtt.h"
#include "app_settings.h"
#include "sd_access.h"
#include "cycles_rs485.h"

#include <SD.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <esp_log.h>
#include <time.h>
#include <cctype>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mqtt_kw";

/* ------------------------------------------------------------------ */
/* Estado interno                                                        */
/* ------------------------------------------------------------------ */

namespace {

static char     s_last_path[48]     = {};   /* ultimo path processado (para detectar mudanca de dia) */
static size_t   s_last_offset       = 0;    /* offset bytes ja' lidos no ficheiro actual */
static uint32_t s_last_seen_count   = 0;    /* ultimo line count visto */
static bool     s_task_created      = false;

/* Buffer estatico — nao stack — 4096 bytes mais null terminator. */
static char s_buf[4097];

/* Ponteiros para ate 10 linhas no s_buf apos parse. */
static const char *s_lines[10];

} /* namespace */

/* ------------------------------------------------------------------ */
/* Helper: strcasestr local (newlib pode nao ter em todos os targets)   */
/* ------------------------------------------------------------------ */

static const char *kw_strcasestr(const char *haystack, const char *needle) {
    if (!needle || !*needle) return haystack;
    for (; *haystack; ++haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            ++h; ++n;
        }
        if (!*n) return haystack;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ */
/* Parser: colecciona ponteiros para as ultimas ate 10 linhas no buf.   */
/* Substitui '\n' (e '\r' antes) por '\0' in-place.                    */
/* Retorna o numero de linhas encontradas (max 10).                     */
/* ------------------------------------------------------------------ */

static int collect_last_lines(char *buf) {
    /* Primeiro passo: percorrer e anotar inicio de cada linha. */
    /* Usamos s_lines como anel circular — guardamos sempre o mais recente. */
    int total = 0;
    int head  = 0; /* indice circular no array s_lines[10] */

    char *p = buf;
    char *line_start = buf;

    while (*p) {
        if (*p == '\n') {
            /* Terminar linha: substituir '\r' anterior se existir. */
            if (p > buf && *(p - 1) == '\r') {
                *(p - 1) = '\0';
            }
            *p = '\0';

            /* Registar esta linha (pode ser vazia — nao nos importa, sera' ignorada no match). */
            s_lines[head % 10] = line_start;
            head++;
            total++;

            line_start = p + 1;
        }
        ++p;
    }

    /* Linha final sem '\n' (chunk terminou no meio de uma linha) — ignorada
     * porque o offset foi recuado para o ultimo newline antes de entrar aqui. */

    if (total == 0) return 0;

    /* Reorganizar: queremos as ultimas min(total,10) em ordem cronologica. */
    int n = (total < 10) ? total : 10;
    /* head aponta para o proximo slot; as ultimas n linhas estao em
     * s_lines[(head-n)%10 .. (head-1)%10] em ordem. */
    /* Copiar para array temporario na ordem correcta. */
    static const char *tmp[10];
    for (int i = 0; i < n; ++i) {
        /* Use unsigned arithmetic to avoid negative modulo (C++11 truncates toward zero). */
        tmp[i] = s_lines[((unsigned)(head - n + i)) % 10U];
    }
    for (int i = 0; i < n; ++i) {
        s_lines[i] = tmp[i];
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Publicar evento de keyword                                            */
/* ------------------------------------------------------------------ */

static void publish_keyword_event(const char *kw_matched,
                                  const char *line,
                                  const char *file_path,
                                  uint32_t   line_approx) {
    StaticJsonDocument<384> doc;
    doc["ts"]      = (uint32_t)time(nullptr);
    doc["kw"]      = kw_matched;

    /* Truncar linha a 200 chars para evitar overflow do payload MQTT. */
    char line_trunc[201];
    strncpy(line_trunc, line, 200);
    line_trunc[200] = '\0';
    doc["line"]    = line_trunc;

    doc["file"]    = file_path;
    doc["line_no"] = line_approx;

    char payload[384];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0 || n >= sizeof(payload)) {
        ESP_LOGW(TAG, "keyword JSON overflow");
        return;
    }

    net_mqtt_publish("/keyword", payload, false);
    ESP_LOGI(TAG, "keyword match: kw='%s' line_no=%lu", kw_matched, (unsigned long)line_approx);
}

/* ------------------------------------------------------------------ */
/* Task principal                                                         */
/* ------------------------------------------------------------------ */

static void mqtt_kw_task(void *) {
    constexpr TickType_t kTickMs = pdMS_TO_TICKS(5000);

    for (;;) {
        vTaskDelay(kTickMs);

        /* --- Condicoes de saida rapida --- */
        if (net_mqtt_status() != MqttStatus::Connected) {
            continue;
        }

        String kw_setting = app_settings_mqtt_keywords();
        if (kw_setting.length() == 0) {
            continue;
        }

        if (!sd_access_is_mounted()) {
            continue;
        }

        /* --- Obter path do dia --- */
        char today_path[48];
        if (!cycles_rs485_format_today_path(today_path, sizeof(today_path))) {
            continue;
        }

        /* Detectar mudanca de dia: reset offset se path mudou. */
        if (strcmp(today_path, s_last_path) != 0) {
            strncpy(s_last_path, today_path, sizeof(s_last_path) - 1);
            s_last_path[sizeof(s_last_path) - 1] = '\0';
            s_last_offset = 0;
            /* Nao resetar s_last_seen_count para nao reprocessar linhas
             * de uma sessao anterior do mesmo dia apos um reboot. */
        }

        /* --- Verificar se ha linhas novas --- */
        uint32_t count = cycles_rs485_today_line_count();
        if (count == s_last_seen_count) {
            continue;
        }

        /* --- Ler chunk novo via sd_access_sync --- */
        int got = 0;
        sd_access_sync([&]() {
            File f = SD.open(s_last_path, FILE_READ);
            if (!f) {
                /* Ficheiro ainda nao existe (sem linhas gravadas hoje). */
                s_last_offset = 0;
                return;
            }

            size_t fsz = f.size();

            /* Detectar truncagem/rotacao (nao e' esperada em condicoes normais,
             * mas tratamos na mesma — melhor reler 4 KB do que crashar). */
            if (fsz < s_last_offset) {
                s_last_offset = (fsz > 4096U) ? (fsz - 4096U) : 0U;
            }

            if (fsz <= s_last_offset) {
                f.close();
                return;
            }

            f.seek(s_last_offset);

            /* Ler ate sizeof(s_buf)-1 bytes. */
            int r = f.read((uint8_t *)s_buf, sizeof(s_buf) - 1);
            f.close();

            if (r <= 0) return;

            s_buf[r] = '\0';
            got = r;

            /* Recuar offset ao ultimo newline para nao partir linha a meio. */
            /* Se o ultimo byte ja e' '\n', offset avanca normalmente. */
            if (s_buf[r - 1] != '\n') {
                /* Procurar ultimo '\n' dentro do buffer. */
                int last_nl = r - 1;
                while (last_nl >= 0 && s_buf[last_nl] != '\n') {
                    --last_nl;
                }
                if (last_nl >= 0) {
                    /* Ajustar got para incluir apenas ate (e com) o ultimo '\n'. */
                    got = last_nl + 1;
                    s_buf[got] = '\0';
                }
                /* Se nao ha nenhum '\n' no buffer inteiro, a linha ainda nao
                 * terminou — nao avancamos o offset (got fica com o valor
                 * completo, mas nao atualiza s_last_offset abaixo porque
                 * collect_last_lines nao encontra '\n' e retorna 0 linhas). */
                if (last_nl < 0) {
                    /* Nada completo para processar; nao avanca offset. */
                    got = 0;
                    return;
                }
            }

            s_last_offset += (size_t)got;
        });

        if (got <= 0) {
            s_last_seen_count = count;
            continue;
        }

        /* --- Parse: ate 10 linhas (coloca ponteiros em s_lines) --- */
        int n_lines = collect_last_lines(s_buf);
        if (n_lines <= 0) {
            s_last_seen_count = count;
            continue;
        }

        /* Numero aproximado da primeira linha neste chunk.
         * count e' o total de linhas; n_lines e' o que processamos neste tick.
         * As linhas processadas sao as mais recentes, portanto a primeira
         * tem numero aproximado de (count - n_lines + 1). */
        uint32_t first_line_no = (count > (uint32_t)n_lines)
                                 ? (count - (uint32_t)n_lines + 1U)
                                 : 1U;

        /* --- Iterar keywords vs linhas --- */
        /* Trabalhar sobre copia da string de keywords (strtok modifica in-place). */
        char kw_buf[256];
        strncpy(kw_buf, kw_setting.c_str(), sizeof(kw_buf) - 1);
        kw_buf[sizeof(kw_buf) - 1] = '\0';

        for (int li = 0; li < n_lines; ++li) {
            const char *line = s_lines[li];
            if (!line || *line == '\0') continue;

            uint32_t line_no = first_line_no + (uint32_t)li;

            /* Iterar cada keyword separada por ';'.
             * strtok nao e' re-entrante; como estamos numa unica task nao
             * ha problema — mas usamos strtok_r por clareza. */
            char kw_copy[256];
            strncpy(kw_copy, kw_buf, sizeof(kw_copy) - 1);
            kw_copy[sizeof(kw_copy) - 1] = '\0';

            char *saveptr = nullptr;
            char *kw_tok  = strtok_r(kw_copy, ";", &saveptr);
            while (kw_tok) {
                /* Remover espacos iniciais/finais da keyword. */
                while (*kw_tok == ' ') ++kw_tok;
                char *end = kw_tok + strlen(kw_tok);
                while (end > kw_tok && *(end - 1) == ' ') --end;
                *end = '\0';

                if (*kw_tok != '\0') {
                    if (kw_strcasestr(line, kw_tok) != nullptr) {
                        publish_keyword_event(kw_tok, line, s_last_path, line_no);
                    }
                }
                kw_tok = strtok_r(nullptr, ";", &saveptr);
            }
        }

        s_last_seen_count = count;
    }
}

/* ------------------------------------------------------------------ */
/* API publica                                                           */
/* ------------------------------------------------------------------ */

void net_mqtt_keywords_start(void) {
    if (s_task_created) {
        return; /* Idempotente. */
    }
    s_task_created = true;

    /* Stack 8192: o caminho de publish (publish_keyword_event -> net_mqtt_publish ->
     * build_topic -> app_settings_mqtt_base_topic -> Preferences::getString -> NVS
     * read -> SPI flash mutex + low-int ISR) consome >3 KB. Stack canary
     * watchpoint disparou em 3072 no primeiro match (logs/crash_keyword/). */
    BaseType_t ok = xTaskCreatePinnedToCore(
        mqtt_kw_task, "mqtt_kw",
        8192U, nullptr, 1U,
        nullptr, 1 /* core 1 */
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "falha ao criar task mqtt_kw");
        s_task_created = false;
        return;
    }

    ESP_LOGI(TAG, "task mqtt_kw iniciada (core 1, stack 8192, tick 5s)");
}

void net_mqtt_keywords_apply_settings(void) {
    /* A task le app_settings_mqtt_keywords() em cada tick — sem accao necessaria. */
}
