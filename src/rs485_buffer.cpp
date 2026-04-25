/**
 * @file rs485_buffer.cpp
 * @brief Implementacao de rs485_buffer.h — buffer SPIFFS fallback para RS485.
 *
 * Ficheiro SPIFFS: /rs485_pending.log
 * Formato de cada registo: "<unix_epoch_decimal>\t<linha_rs485>\n"
 *
 * Append: abre em modo "a", escreve, fecha. Sem handle persistente —
 * evita corrupcao se reset ocorrer entre operacoes.
 *
 * Flush: abre em modo "r", le linha a linha, parse epoch + conteudo,
 * distribui por /CICLOS/AAAA/MM/AAAAMMDD.txt no SD. Remove ficheiro apos EOF.
 * Se SD falhar a meio: aborta, mantem ficheiro SPIFFS.
 */

#include "rs485_buffer.h"
#include "cycles_rs485.h"   /* cycles_rs485_format_path_from_tm, cycles_rs485_mkdirs_for_ym */
#include "sd_access.h"
#include "app_log.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <SD.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Constantes                                                           */
/* ------------------------------------------------------------------ */

static constexpr const char *kSpiffsPath    = "/rs485_pending.log";
static constexpr size_t      kSpiffsMaxBytes = 2U * 1024U * 1024U;  /* 2 MB */
static constexpr time_t      kMinValidEpoch  = 1577836800LL;        /* 2020-01-01 00:00 UTC */

/** Tamanho maximo de uma linha RS485 guardada (epoch + tab + conteudo + LF). */
static constexpr size_t kLineStoreCap = 20U + 1U + 512U + 1U; /* 534 bytes */

/* ------------------------------------------------------------------ */
/* Estado interno                                                       */
/* ------------------------------------------------------------------ */

/** Flag para log "RTC invalido" e "buffer cheio" — emite apenas uma vez. */
static bool s_warned_rtc_invalid = false;
static bool s_warned_full        = false;

/* ------------------------------------------------------------------ */
/* Implementacao publica                                                */
/* ------------------------------------------------------------------ */

void rs485_buffer_init(void) {
    /* Verificar se ficheiro existe e tem conteudo. */
    if (!SPIFFS.exists(kSpiffsPath)) {
        app_log_write("INFO", "[rs485_buffer] init ok — sem buffer pendente.");
        return;
    }
    File f = SPIFFS.open(kSpiffsPath, "r");
    const size_t sz = f ? (size_t)f.size() : 0U;
    if (f) {
        f.close();
    }
    if (sz == 0U) {
        app_log_write("INFO", "[rs485_buffer] init ok — ficheiro vazio.");
        return;
    }
    app_log_writef("INFO", "[rs485_buffer] init: ficheiro pendente %u bytes.", (unsigned)sz);

    /* Se SD ja montado no boot: flush imediato. */
    if (sd_access_is_mounted()) {
        app_log_write("INFO", "[rs485_buffer] SD montado no boot — flush imediato.");
        rs485_buffer_flush_to_sd();
    } else {
        app_log_write("INFO", "[rs485_buffer] SD ausente — aguarda insercao para flush.");
    }
}

void rs485_buffer_append(const char *line, size_t len) {
    if (line == nullptr || len == 0U) {
        return;
    }

    /* Validar RTC. */
    const time_t now = time(nullptr);
    if (now < kMinValidEpoch) {
        if (!s_warned_rtc_invalid) {
            s_warned_rtc_invalid = true;
            app_log_write("WARN", "[rs485_buffer] RTC invalido — linha nao bufferizada.");
        }
        return;
    }

    /* Verificar quota. */
    {
        File fcheck = SPIFFS.open(kSpiffsPath, "r");
        const size_t cur_sz = fcheck ? (size_t)fcheck.size() : 0U;
        if (fcheck) {
            fcheck.close();
        }
        if (cur_sz >= kSpiffsMaxBytes) {
            if (!s_warned_full) {
                s_warned_full = true;
                app_log_write("WARN", "[rs485_buffer] full — dropping line (drop-newest).");
            }
            return;
        }
    }

    /* Repor flags de aviso se entretanto o buffer foi drenado. */
    s_warned_full = false;

    /* Append: "<epoch>\t<linha>\n" */
    File fa = SPIFFS.open(kSpiffsPath, "a");
    if (!fa) {
        app_log_write("WARN", "[rs485_buffer] falha ao abrir SPIFFS para append.");
        return;
    }
    char hdr[24];
    const int hdr_len = snprintf(hdr, sizeof(hdr), "%lld\t", (long long)now);
    if (hdr_len > 0) {
        (void)fa.write(reinterpret_cast<const uint8_t *>(hdr), (size_t)hdr_len);
    }
    (void)fa.write(reinterpret_cast<const uint8_t *>(line), len);
    const uint8_t nl = static_cast<uint8_t>('\n');
    (void)fa.write(&nl, 1U);
    fa.close();
}

void rs485_buffer_flush_to_sd(void) {
    if (!SPIFFS.exists(kSpiffsPath)) {
        return; /* Nada a drenar. */
    }

    File fr = SPIFFS.open(kSpiffsPath, "r");
    if (!fr) {
        app_log_write("WARN", "[rs485_buffer] flush: nao foi possivel abrir ficheiro SPIFFS.");
        return;
    }

    uint32_t n_ok    = 0U;
    uint32_t n_skip  = 0U;
    bool     sd_error = false;

    /* Buffer de leitura de linha (epoch + tab + conteudo + LF). */
    char     raw[kLineStoreCap + 1U];
    size_t   raw_pos = 0U;

    /* Leitura byte a byte para detetar LF sem depender de String (heap). */
    while (fr.available() || raw_pos > 0U) {
        /* Preencher raw ate LF ou EOF. */
        bool got_line = false;
        while (fr.available()) {
            const int c = fr.read();
            if (c < 0) {
                break;
            }
            if ((uint8_t)c == '\n') {
                got_line = true;
                break;
            }
            if (raw_pos < kLineStoreCap) {
                raw[raw_pos++] = (char)c;
            } else {
                /* Linha demasiado longa — descartar restante ate ao proximo LF. */
                raw_pos = 0U;
            }
        }

        if (!got_line) {
            /* EOF atingido sem LF final: linha incompleta — descartar. */
            if (raw_pos > 0U) {
                ++n_skip;
            }
            break;
        }

        raw[raw_pos] = '\0';
        const size_t line_len = raw_pos;
        raw_pos = 0U;

        if (line_len == 0U) {
            continue; /* Linha vazia — ignorar. */
        }

        /* Parse: "<epoch>\t<conteudo>" */
        char *tab_ptr = strchr(raw, '\t');
        if (tab_ptr == nullptr) {
            ++n_skip;
            continue;
        }
        *tab_ptr = '\0';
        const time_t epoch = (time_t)atoll(raw);
        const char  *content = tab_ptr + 1U;
        const size_t content_len = line_len - (size_t)(tab_ptr - raw) - 1U;

        if (epoch < kMinValidEpoch || content_len == 0U) {
            ++n_skip;
            continue;
        }

        /* Converter epoch em struct tm local. */
        struct tm lt {};
        if (localtime_r(&epoch, &lt) == nullptr) {
            ++n_skip;
            continue;
        }

        /* Criar diretorias /CICLOS/AAAA/MM se necessario. */
        const int year  = lt.tm_year + 1900;
        const int month = lt.tm_mon  + 1;
        cycles_rs485_mkdirs_for_ym(year, month);

        /* Formatar caminho de destino. */
        char path[48];
        if (!cycles_rs485_format_path_from_tm(&lt, path, sizeof(path))) {
            ++n_skip;
            continue;
        }

        /* Escrever no SD. */
        File fw = SD.open(path, FILE_APPEND);
        if (!fw) {
            app_log_writef("WARN", "[rs485_buffer] flush: erro ao abrir SD %s", path);
            sd_error = true;
            break;
        }
        (void)fw.write(reinterpret_cast<const uint8_t *>(content), content_len);
        const uint8_t nl = static_cast<uint8_t>('\n');
        (void)fw.write(&nl, 1U);
        fw.close();
        ++n_ok;
    }

    fr.close();

    if (sd_error) {
        app_log_writef("WARN", "[rs485_buffer] flush aborted: SD error (%u gravadas, %u ignoradas).",
                       (unsigned)n_ok, (unsigned)n_skip);
        /* Manter ficheiro SPIFFS intacto para retry no proximo insert. */
        return;
    }

    /* Remover ficheiro SPIFFS apos drain completo. */
    (void)SPIFFS.remove(kSpiffsPath);

    /* Repor flags de aviso para proxima sessao de captura sem SD. */
    s_warned_full        = false;
    s_warned_rtc_invalid = false;

    app_log_writef("INFO", "[rs485_buffer] flush ok %u lines (%u ignoradas).",
                   (unsigned)n_ok, (unsigned)n_skip);
}

size_t rs485_buffer_size_bytes(void) {
    if (!SPIFFS.exists(kSpiffsPath)) {
        return 0U;
    }
    File f = SPIFFS.open(kSpiffsPath, "r");
    if (!f) {
        return 0U;
    }
    const size_t sz = (size_t)f.size();
    f.close();
    return sz;
}
