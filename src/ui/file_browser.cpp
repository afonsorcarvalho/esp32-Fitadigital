/**
 * @file file_browser.cpp
 * @brief Explorador tipo lista (Nome | Tamanho | Data). SD.open() usa raiz '/' (ver VFS Arduino).
 * Listagem: cada job sd_io limita openNextFile; ao reabrir a pasta e' preciso saltar `consumed_entries`
 * entradas com um contador local por job (evita duplicar a lista na raiz).
 *
 * Visualizador .txt/.log com janela deslizante: indexa offsets de linhas no arquivo (PSRAM)
 * e carrega apenas ~kWindowLines linhas por vez na lv_table, recarregando automaticamente
 * conforme o usuario faz scroll (sliding window). Suporta arquivos de qualquer tamanho
 * ate kMaxIndexLines linhas.
 *
 * Fonte monoespaçada nativa LVGL (Unscii 8/16).
 * Courier New em vários px requer gerar .c com https://lvgl.io/tools/fontconverter a partir de cour.ttf.
 */
#include "file_browser.h"
#include <atomic>
#include <Arduino.h>
#include <SD.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <esp_heap_caps.h>
#include "lvgl_port_v8.h"
#include "app_settings.h"
#include "app_log.h"
#include "sd_access.h"
#include "cycles_rs485.h"
#include "ui/ui_date_goto.h"
#include "ui/ui_loading.h"
#include "ui/ui_share_qr.h"
#include "ui/ui_theme.h"
#include "ui/ui_toast.h"

/* ── Constantes do explorador de ficheiros ────────────────────────────── */

static constexpr uintptr_t kUserDataParentDir = 0xFFFFFFFFu;
static constexpr unsigned kMaxListEntries = 48;
/** Maximo de openNextFile() por job sd_io (skip + listagem); varios jobs permitem async_tcp (TWDT). */
static constexpr unsigned kMaxOpenNextPerSdJob = 8;
/** Limite de ciclos listagem (pastas enormes / anomalias). */
static constexpr unsigned kMaxDirListBatches = 512;
static constexpr unsigned kMaxCellChars = 120;
static constexpr char kFsRoot[] = "/";

/* ── Constantes do visualizador paginado ─────────────────────────────── */

/** Tamanho do buffer reutilizado para leitura de janelas de texto. */
static constexpr size_t kTextBufSize = 8 * 1024;
/** Maximo de linhas indexaveis num arquivo (~60 KB de PSRAM para offsets). */
static constexpr unsigned kMaxIndexLines = 15000;
/** Linhas carregadas na lv_table por vez (janela deslizante). */
static constexpr unsigned kWindowLines = 150;
/** Linhas deslocadas ao atingir a borda da janela durante scroll. */
static constexpr unsigned kWindowShift = 60;
/** Pixels da borda da tabela para disparar recarga da janela. */
static constexpr lv_coord_t kScrollThresholdPx = 80;
/** Intervalo minimo entre recargas de janela (ms). */
static constexpr uint32_t kReloadCooldownMs = 250;

/* ── Estado do explorador de ficheiros ───────────────────────────────── */

/** Timer para reindexar o .txt do dia quando o RS485 acrescenta dados (visualizador aberto). */
static lv_timer_t *s_cycles_live_timer = nullptr;
/** Contador de linhas RS485 gravadas; timer LVGL reage e abre/atualiza o .txt do dia no fim. */
static std::atomic<uint32_t> s_rs485_saved_seq{0};
static uint32_t s_rs485_saved_last_handled = 0;
static lv_timer_t *s_rs485_open_timer = nullptr;
/** One-shot LVGL: apos o atraso, activa `cycles_rs485_set_line_to_ui_follow(true)` (sistema estabilizado). */
static constexpr uint32_t kRs485UiFollowArmDelayMs = 3000U;
static lv_timer_t *s_rs485_ui_follow_arm_timer = nullptr;
/** >0 durante show_text_file (fase de indexacao SD); evita refresh_silent a limpar a lista. */
static int s_suppress_auto_file_list_refresh = 0;
/** Callback registado por ui_app: muda para a view do explorador quando RS485 ativo fora do browser. */
static void (*s_auto_open_cb)(void) = nullptr;

static lv_obj_t *s_root = nullptr;
/** Fonte ativa (definições UI); nullptr usa LV_FONT_DEFAULT no visualizador. */
static const lv_font_t *s_active_ui_font = nullptr;
static lv_obj_t *s_path_label = nullptr;
/** Barra de breadcrumbs (chips) no topo do explorador, substitui `s_path_label`. */
static lv_obj_t *s_breadcrumb = nullptr;
/** Botao "Ir para data": so visivel dentro de /CICLOS. */
static lv_obj_t *s_date_goto_btn = nullptr;
static lv_obj_t *s_list = nullptr;
static char s_current_path[256];
static char s_entry_paths[kMaxListEntries][192];
static bool s_entry_is_dir[kMaxListEntries];
/** Texto da linha na lista (preenchido durante scan SD sem mutex LVGL). */
static char s_entry_lines[kMaxListEntries][128];
static unsigned s_entry_count = 0;
/** Timestamp (millis) da ultima vez que a lista de ficheiros foi atualizada na UI. */
static uint32_t s_last_refresh_ms = 0;

/* ── Estado do refresh assincrono da listagem SD ─────────────────────── */
/**
 * Listar o SD no callback LVGL bloqueava a task `lvgl` (~alguns segundos em pastas
 * grandes / SD com contencao), congelando a UI e o toque. O scan corre agora na
 * task `sd_io` em chunks (`kMaxOpenNextPerSdJob` entradas por job reagendado
 * via `sd_access_async`), permitindo que outros jobs (async_tcp, FTP) intercalem
 * e que `lv_timer_handler` continue a redesenhar e a animar o overlay.
 */
static std::atomic<bool> s_refresh_in_flight{false};
static unsigned s_async_scanned = 0;
static unsigned s_async_consumed = 0;
static bool s_async_dir_ok = false;
static bool s_async_show_overlay = false;
static lv_obj_t *s_overlay = nullptr;
/** Tabela do visualizador .txt (scroll); limpo ao fechar overlay. */
static lv_obj_t *s_viewer_table = nullptr;

/* ── Estado do visualizador paginado (janela deslizante) ─────────────── */

/** Offsets (bytes) do inicio de cada linha no arquivo; alocado em PSRAM. */
static uint32_t *s_line_offsets = nullptr;
static unsigned s_total_lines = 0;
static size_t s_file_size = 0;
/** Indice da primeira linha carregada na janela atual da tabela. */
static unsigned s_window_start = 0;
/** Quantidade de linhas efetivamente carregadas na janela atual. */
static unsigned s_window_count = 0;
static char s_viewer_path[256] = {};
/** Titulo no leitor: nome do ficheiro + caminho completo. */
static lv_obj_t *s_viewer_path_label = nullptr;
/** Label com indicador de posicao ("Linhas X-Y de Z"). */
static lv_obj_t *s_viewer_info_label = nullptr;
static bool s_loading_window = false;
static uint32_t s_last_reload_ms = 0;
/** true se o indice de linhas foi truncado (arquivo > kMaxIndexLines). */
static bool s_index_truncated = false;
/** Buffer reutilizado para leitura de texto das janelas. */
static char s_text_buf[kTextBufSize];
/** Ponteiros para linhas dentro de s_text_buf (split_lines_inplace). */
static char *s_line_ptrs[kWindowLines + 2];

/* ── Highlight piscante da nova linha RS485 no viewer ──────────────────── */
/** Linha (indice dentro da tabela) a destacar; UINT16_MAX = sem highlight. */
static uint16_t s_highlight_row = UINT16_MAX;
/** Timer de blink; intervalo 250 ms. */
static lv_timer_t *s_highlight_blink_timer = nullptr;
/** Ticks restantes (6 = 3 ciclos on/off). */
static uint8_t s_highlight_ticks_remaining = 0;
/** Estado actual do blink (true = destaque visivel). */
static bool s_highlight_on = false;

/* ── Constantes e estado dos botoes do visualizador ──────────────────── */

static constexpr lv_coord_t kViewerBtnColW = 62;
static constexpr lv_coord_t kViewerBtnH = 48;
static constexpr lv_coord_t kViewerBtnPad = 6;
/** Largura fixa da coluna de numeracao de linhas (gutter).
 *  Suficiente para 4 digitos (9999) com a fonte monospace + padding, sem layout shift. */
static constexpr lv_coord_t kViewerGutterW = 56;

static constexpr intptr_t kViewerNavUp = 1;
static constexpr intptr_t kViewerNavDown = 2;
static constexpr intptr_t kViewerNavTop = 3;
static constexpr intptr_t kViewerNavEnd = 4;
static constexpr intptr_t kViewerNavGoto = 5;

/* ── Forward declarations ────────────────────────────────────────────── */

static bool load_viewer_window(unsigned first_line, unsigned count);
static void update_viewer_info(void);
static void show_text_file(const char *full_path, bool quiet_index = false, bool scroll_to_end = false);

/* ── Funcoes auxiliares do visualizador ───────────────────────────────── */

/**
 * Fonte monoespaçada para células do .txt: alinha ao índice de tamanho da UI (0=menor, 3=maior).
 * Unscii só oferece 8px e 16px de altura de linha (doc LVGL built-in fonts).
 */
static const lv_font_t *viewer_monospace_font_for_settings(void) {
#if LV_FONT_UNSCII_8 && LV_FONT_UNSCII_16
  if (app_settings_font_index() == 0) {
    return &lv_font_unscii_8;
  }
  return &lv_font_unscii_16;
#elif LV_FONT_UNSCII_16
  return &lv_font_unscii_16;
#else
  return (const lv_font_t *)LV_FONT_DEFAULT;
#endif
}

/** Estilo "Voltar" no padrao verde primario dos restantes botoes. */
static void viewer_style_ios_back_button(lv_obj_t *btn, lv_obj_t *lbl, const lv_font_t *ui_font) {
  lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
  lv_obj_set_style_pad_hor(btn, 12, LV_PART_MAIN);
  lv_label_set_text(lbl, LV_SYMBOL_LEFT " Voltar");
  lv_obj_set_style_text_color(lbl, UI_COLOR_WHITE, LV_PART_MAIN);
  if (ui_font != nullptr) {
    lv_obj_set_style_text_font(lbl, ui_font, LV_PART_MAIN);
  }
  lv_obj_center(lbl);
}

/**
 * Calcula quantas linhas cabem no viewport visivel da tabela.
 * Usa a altura do viewport e a altura total do conteudo para estimar.
 */
static unsigned viewer_visible_line_count(void) {
  if (s_viewer_table == nullptr || s_window_count == 0) {
    return 1;
  }
  lv_obj_update_layout(s_viewer_table);
  const lv_coord_t viewport_h = lv_obj_get_height(s_viewer_table);
  const lv_coord_t content_h = lv_obj_get_self_height(s_viewer_table);
  if (content_h <= 0 || viewport_h <= 0) {
    return 1;
  }
  unsigned vis = (unsigned)((uint32_t)viewport_h * s_window_count / (uint32_t)content_h);
  return (vis < 1) ? 1 : vis;
}

/**
 * Menor indice de linha (0-based) tal que o sufixo bytes[line..EOF] cabe em kTextBufSize.
 * (Quanto menor o indice, mais linhas no sufixo.) Para ir ao fim do ficheiro sem truncar
 * por buffer, comeca-se nesta linha e le-se ate EOF numa passagem.
 */
static unsigned viewer_tail_fit_first_line(void) {
  if (s_line_offsets == nullptr || s_total_lines == 0 || s_file_size == 0) {
    return 0;
  }
  const size_t cap = kTextBufSize - 64U;
  unsigned best = s_total_lines - 1;
  for (unsigned fl = 0; fl < s_total_lines; ++fl) {
    const size_t sz = s_file_size - (size_t)s_line_offsets[fl];
    if (sz <= cap) {
      best = fl;
      break; /* fl crescente: primeiro que cabe tem o sufixo mais longo possivel. */
    }
  }
  return best;
}

/**
 * Scroll/reload para linha arbitraria (0-based).
 * Se a linha cabe na janela carregada, faz scroll animado; senao recarrega janela centrada.
 */
static void viewer_goto_line(unsigned target_line, bool anim) {
  if (s_viewer_table == nullptr || s_line_offsets == nullptr || s_total_lines == 0) {
    return;
  }
  if (target_line >= s_total_lines) {
    target_line = s_total_lines - 1;
  }
  lv_obj_update_layout(s_viewer_table);

  if (target_line >= s_window_start && target_line < s_window_start + s_window_count) {
    const unsigned row_in_window = target_line - s_window_start;
    const lv_coord_t total_h = lv_obj_get_self_height(s_viewer_table);
    const lv_coord_t target_y = (lv_coord_t)(
        (uint32_t)row_in_window * (uint32_t)total_h / s_window_count);
    lv_obj_scroll_to_y(s_viewer_table, target_y, anim ? LV_ANIM_ON : LV_ANIM_OFF);
    return;
  }

  const unsigned margin = kWindowLines / 4;
  unsigned new_start = (target_line > margin) ? target_line - margin : 0;
  if (new_start + kWindowLines > s_total_lines && s_total_lines > kWindowLines) {
    new_start = viewer_tail_fit_first_line();
  }
  const unsigned max_lines = (s_total_lines > new_start) ? s_total_lines - new_start : 1;
  load_viewer_window(new_start, max_lines);
  lv_obj_update_layout(s_viewer_table);

  if (target_line >= s_window_start && s_window_count > 0) {
    const unsigned row_in_window = target_line - s_window_start;
    const lv_coord_t total_h = lv_obj_get_self_height(s_viewer_table);
    const lv_coord_t target_y = (lv_coord_t)(
        (uint32_t)row_in_window * (uint32_t)total_h / s_window_count);
    lv_obj_scroll_to_y(s_viewer_table, target_y, LV_ANIM_OFF);
  } else {
    lv_obj_scroll_to_y(s_viewer_table, 0, LV_ANIM_OFF);
  }
}

/* ── Modal "Ir para linha" ─────────────────────────────────────────────── */
static lv_obj_t *s_goto_line_bg = nullptr;
static lv_obj_t *s_goto_line_ta = nullptr;

static void goto_line_modal_close(void) {
  if (s_goto_line_bg != nullptr) {
    lv_obj_del(s_goto_line_bg);
    s_goto_line_bg = nullptr;
    s_goto_line_ta = nullptr;
  }
}

static void goto_line_cancel_cb(lv_event_t * /*e*/) {
  goto_line_modal_close();
}

static void goto_line_ok_cb(lv_event_t * /*e*/) {
  if (s_goto_line_ta == nullptr) {
    goto_line_modal_close();
    return;
  }
  const char *txt = lv_textarea_get_text(s_goto_line_ta);
  unsigned line1 = 0;
  if (txt != nullptr) {
    for (const char *p = txt; *p; ++p) {
      if (*p < '0' || *p > '9') { line1 = 0; break; }
      line1 = line1 * 10U + (unsigned)(*p - '0');
      if (line1 > 10000000U) { line1 = 10000000U; break; }
    }
  }
  goto_line_modal_close();
  if (line1 == 0) {
    ui_toast_show(ToastKind::Warn, "Numero de linha invalido");
    return;
  }
  if (s_total_lines == 0) {
    ui_toast_show(ToastKind::Info, "Arquivo vazio");
    return;
  }
  unsigned target0 = (line1 > s_total_lines) ? (s_total_lines - 1U) : (line1 - 1U);
  viewer_goto_line(target0, true);
}

static void goto_line_bg_click_cb(lv_event_t *e) {
  if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
    goto_line_modal_close();
  }
}

static void goto_line_open_modal(void) {
  if (s_goto_line_bg != nullptr || s_viewer_table == nullptr) return;
  if (s_total_lines == 0) {
    ui_toast_show(ToastKind::Info, "Arquivo vazio");
    return;
  }

  const lv_coord_t scr_w = lv_disp_get_hor_res(nullptr);
  const lv_coord_t scr_h = lv_disp_get_ver_res(nullptr);

  s_goto_line_bg = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_goto_line_bg, scr_w, scr_h);
  lv_obj_align(s_goto_line_bg, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(s_goto_line_bg, UI_COLOR_BLACK, 0);
  lv_obj_set_style_bg_opa(s_goto_line_bg, LV_OPA_60, 0);
  lv_obj_set_style_border_width(s_goto_line_bg, 0, 0);
  lv_obj_set_style_pad_all(s_goto_line_bg, 0, 0);
  lv_obj_set_style_radius(s_goto_line_bg, 0, 0);
  lv_obj_clear_flag(s_goto_line_bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_goto_line_bg, goto_line_bg_click_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *modal = lv_obj_create(s_goto_line_bg);
  lv_obj_set_width(modal, 460);
  lv_obj_set_height(modal, LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(modal, scr_h - 20, 0);
  lv_obj_center(modal);
  lv_obj_set_style_bg_color(modal, ui_color_surface(app_settings_dark_mode()), 0);
  lv_obj_set_style_radius(modal, 12, 0);
  lv_obj_set_style_pad_all(modal, 16, 0);
  lv_obj_set_style_pad_row(modal, 10, 0);
  lv_obj_set_layout(modal, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(modal);
  char title_buf[48];
  snprintf(title_buf, sizeof(title_buf), "Ir para linha (1-%u)", s_total_lines);
  lv_label_set_text(title, title_buf);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(title, UI_COLOR_PRIMARY, 0);

  lv_obj_t *ta = lv_textarea_create(modal);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_accepted_chars(ta, "0123456789");
  lv_textarea_set_max_length(ta, 8);
  lv_textarea_set_placeholder_text(ta, "ex: 1234");
  lv_obj_set_width(ta, LV_PCT(100));
  lv_obj_set_style_text_font(ta, &lv_font_montserrat_20, 0);
  s_goto_line_ta = ta;

  lv_obj_t *kb = lv_keyboard_create(modal);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
  lv_obj_set_size(kb, LV_PCT(100), (lv_coord_t)(scr_h * 32 / 100));
  lv_keyboard_set_textarea(kb, ta);

  lv_obj_t *btn_row = lv_obj_create(modal);
  lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(btn_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(btn_row, 0, 0);
  lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(btn_row, 0, 0);
  lv_obj_set_style_pad_top(btn_row, 6, 0);
  lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *btn_cancel = lv_btn_create(btn_row);
  lv_obj_set_size(btn_cancel, 160, 54);
  lv_obj_set_style_bg_color(btn_cancel, UI_COLOR_TEXT_MUTED, 0);
  lv_obj_t *lbl_c = lv_label_create(btn_cancel);
  lv_label_set_text(lbl_c, LV_SYMBOL_CLOSE " Cancelar");
  lv_obj_set_style_text_font(lbl_c, &lv_font_montserrat_16, 0);
  lv_obj_center(lbl_c);
  lv_obj_add_event_cb(btn_cancel, goto_line_cancel_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *btn_ok = lv_btn_create(btn_row);
  lv_obj_set_size(btn_ok, 160, 54);
  lv_obj_set_style_bg_color(btn_ok, UI_COLOR_PRIMARY, 0);
  lv_obj_t *lbl_ok = lv_label_create(btn_ok);
  lv_label_set_text(lbl_ok, LV_SYMBOL_OK " Ir");
  lv_obj_set_style_text_font(lbl_ok, &lv_font_montserrat_16, 0);
  lv_obj_center(lbl_ok);
  lv_obj_add_event_cb(btn_ok, goto_line_ok_cb, LV_EVENT_CLICKED, nullptr);
}

/**
 * Callback dos botoes de navegacao do visualizador.
 * Page Up/Down avancam pela quantidade de linhas visiveis no viewport (nao o buffer inteiro).
 * Home/End saltam para inicio/fim do arquivo.
 * Se o destino cabe dentro da janela carregada, faz scroll local; caso contrario carrega nova janela.
 */
static void viewer_nav_btn_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_viewer_table == nullptr) {
    return;
  }
  const intptr_t act = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));

  if (act == kViewerNavGoto) {
    goto_line_open_modal();
    return;
  }

  if (s_line_offsets != nullptr && s_total_lines > 0) {
    lv_obj_update_layout(s_viewer_table);

    const unsigned page_step = viewer_visible_line_count();

    /* Linha do arquivo atualmente visivel no topo do viewport. */
    const lv_coord_t scroll_y = lv_obj_get_scroll_y(s_viewer_table);
    const lv_coord_t content_h = lv_obj_get_self_height(s_viewer_table);
    unsigned cur_visible_line = s_window_start;
    if (content_h > 0 && s_window_count > 0) {
      cur_visible_line = s_window_start +
                         (unsigned)((uint32_t)scroll_y * s_window_count / (uint32_t)content_h);
    }

    unsigned target_line = cur_visible_line;
    bool jump_to_end = false;

    if (act == kViewerNavUp) {
      target_line = (cur_visible_line > page_step) ? cur_visible_line - page_step : 0;
    } else if (act == kViewerNavDown) {
      target_line = cur_visible_line + page_step;
      /* Ultima pagina: ir para a ultima linha do arquivo e rolar ao fundo. */
      if (target_line >= s_total_lines) {
        target_line = (s_total_lines > 0) ? s_total_lines - 1 : 0;
        jump_to_end = true;
      }
    } else if (act == kViewerNavTop) {
      target_line = 0;
    } else if (act == kViewerNavEnd) {
      /* Ultima linha (0-based); rolar ao fundo do viewport. */
      target_line = (s_total_lines > 0) ? s_total_lines - 1 : 0;
      jump_to_end = true;
    }

    if (target_line >= s_total_lines && s_total_lines > 0) {
      target_line = s_total_lines - 1;
    }

    /* Se a linha-alvo esta dentro da janela carregada, basta fazer scroll local. */
    if (target_line >= s_window_start &&
        target_line < s_window_start + s_window_count) {
      const unsigned row_in_window = target_line - s_window_start;
      const lv_coord_t new_total_h = lv_obj_get_self_height(s_viewer_table);
      lv_coord_t target_y;
      if (jump_to_end) {
        target_y = LV_COORD_MAX;
      } else {
        target_y = (lv_coord_t)(
            (uint32_t)row_in_window * (uint32_t)new_total_h / s_window_count);
      }
      lv_obj_scroll_to_y(s_viewer_table, target_y, LV_ANIM_ON);
    } else {
      /* Linha-alvo fora da janela: carregar nova janela centrada nela. */
      unsigned new_start;
      if (jump_to_end) {
        /* Garantir que o trecho desde new_start ate EOF cabe no buffer (8 KiB). */
        new_start = viewer_tail_fit_first_line();
      } else {
        const unsigned margin = kWindowLines / 4;
        new_start = (target_line > margin) ? target_line - margin : 0;
        if (new_start + kWindowLines > s_total_lines && s_total_lines > kWindowLines) {
          new_start = viewer_tail_fit_first_line();
        }
      }

      const unsigned max_lines = (s_total_lines > new_start) ? s_total_lines - new_start : 1;
      load_viewer_window(new_start, max_lines);
      lv_obj_update_layout(s_viewer_table);

      if (jump_to_end) {
        lv_obj_scroll_to_y(s_viewer_table, LV_COORD_MAX, LV_ANIM_OFF);
      } else if (target_line >= s_window_start && s_window_count > 0) {
        const unsigned row_in_window = target_line - s_window_start;
        const lv_coord_t new_total_h = lv_obj_get_self_height(s_viewer_table);
        const lv_coord_t target_y = (lv_coord_t)(
            (uint32_t)row_in_window * (uint32_t)new_total_h / s_window_count);
        lv_obj_scroll_to_y(s_viewer_table, target_y, LV_ANIM_OFF);
      } else {
        lv_obj_scroll_to_y(s_viewer_table, 0, LV_ANIM_OFF);
      }
    }
    return;
  }

  /* Fallback: arquivo pequeno sem indice. */
  lv_obj_update_layout(s_viewer_table);
  const lv_coord_t h = lv_obj_get_height(s_viewer_table);
  const lv_coord_t step = LV_MAX(72, h / 3);

  if (act == kViewerNavUp) {
    lv_obj_scroll_by_bounded(s_viewer_table, 0, step, LV_ANIM_ON);
  } else if (act == kViewerNavDown) {
    lv_obj_scroll_by_bounded(s_viewer_table, 0, -step, LV_ANIM_ON);
  } else if (act == kViewerNavTop) {
    lv_obj_scroll_by_bounded(s_viewer_table, 0, 8000, LV_ANIM_ON);
  } else if (act == kViewerNavEnd) {
    lv_obj_scroll_by_bounded(s_viewer_table, 0, -8000, LV_ANIM_ON);
  }
}

static lv_obj_t *viewer_make_nav_btn(lv_obj_t *col, const char *symbol, intptr_t act, const lv_font_t *font) {
  lv_obj_t *btn = lv_btn_create(col);
  lv_obj_set_size(btn, kViewerBtnColW - 8, kViewerBtnH);
  lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0); /* cor primaria (padrao) */
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, symbol);
  lv_obj_center(lbl);
  if (font != nullptr) {
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
  }
  lv_obj_add_event_cb(btn, viewer_nav_btn_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(act));
  return btn;
}

/**
 * Pads e espaçamento entre linhas nas células da tabela.
 * Não limitar max_height: a lv_table calcula a altura pela fonte e pelo texto; um teto baixo
 * (ex.: lh/2) cortava descendentes e linhas com mais de uma linha visual.
 */
static void viewer_apply_compact_table_rows(lv_obj_t *table, const lv_font_t *font) {
  if (table == nullptr || font == nullptr) {
    return;
  }
  (void)font;
  lv_obj_set_style_pad_top(table, 2, LV_PART_ITEMS);
  lv_obj_set_style_pad_bottom(table, 4, LV_PART_ITEMS);
  lv_obj_set_style_pad_left(table, 3, LV_PART_ITEMS);
  lv_obj_set_style_pad_right(table, 3, LV_PART_ITEMS);
  lv_obj_set_style_text_line_space(table, 2, LV_PART_ITEMS);
}

/**
 * LVGL 8: cada coluna da lv_table tem largura fixa (col_w[]). Se a soma for menor que o widget,
 * fica faixa vazia à direita e o texto quebra numa coluna estreita. Ajusta col0 (nº) + col1 (texto)
 * à largura útil real (100% da área interior da tabela).
 */
static void viewer_table_fit_width(lv_obj_t *table) {
  if (table == nullptr) {
    return;
  }
  lv_coord_t cw = lv_obj_get_content_width(table);
  if (cw < kViewerGutterW + 48) {
    return;
  }
  lv_table_set_col_width(table, 0, kViewerGutterW);
  lv_table_set_col_width(table, 1, cw - kViewerGutterW);
}

static void viewer_table_on_size_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_SIZE_CHANGED) {
    return;
  }
  viewer_table_fit_width(lv_event_get_target(e));
}

static void apply_font_tree(lv_obj_t *obj, const lv_font_t *font) {
  if (!obj || !font) {
    return;
  }
  lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
  const uint32_t n = lv_obj_get_child_cnt(obj);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *ch = lv_obj_get_child(obj, (int32_t)i);
    if (ch != nullptr) {
      apply_font_tree(ch, font);
    }
  }
}

static void go_parent_dir(void) {
  if (strcmp(s_current_path, kFsRoot) == 0) {
    return;
  }
  char *slash = strrchr(s_current_path, '/');
  if (slash != nullptr && slash > s_current_path) {
    *slash = '\0';
  } else {
    strlcpy(s_current_path, kFsRoot, sizeof(s_current_path));
  }
}

static void refresh_file_list(bool show_loading_overlay);

/** Handler de clique num chip: user_data = nivel a manter (0 = root). */
static void breadcrumb_chip_cb(lv_event_t *e) {
  const intptr_t keep = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  char target[sizeof(s_current_path)];
  if (keep <= 0) {
    strlcpy(target, kFsRoot, sizeof(target));
  } else {
    strlcpy(target, s_current_path, sizeof(target));
    intptr_t count = 0;
    for (size_t i = 1; target[i] != '\0'; i++) {
      if (target[i] == '/') {
        count++;
        if (count == keep) {
          target[i] = '\0';
          break;
        }
      }
    }
  }
  strlcpy(s_current_path, target, sizeof(s_current_path));
  refresh_file_list(true);
}

/** Cria um chip (botao arredondado) no breadcrumb. `keep` = segmento alvo; `is_current` = ativo. */
static lv_obj_t *breadcrumb_add_chip(const char *label, intptr_t keep, bool is_current) {
  if (is_current) {
    lv_obj_t *lbl = lv_label_create(s_breadcrumb);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_pad_ver(lbl, 4, 0);
    lv_obj_set_style_pad_hor(lbl, 10, 0);
    lv_obj_set_style_radius(lbl, 14, 0);
    lv_obj_set_style_bg_color(lbl, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_WHITE, 0);
    return lbl;
  }
  lv_obj_t *btn = lv_btn_create(s_breadcrumb);
  lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(btn, 4, 0);
  lv_obj_set_style_pad_hor(btn, 10, 0);
  lv_obj_set_style_radius(btn, 14, 0);
  lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY_LIGHT, 0);
  lv_obj_set_style_text_color(btn, UI_COLOR_PRIMARY_DARK, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, label);
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn, breadcrumb_chip_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(keep));
  return btn;
}

/** Reconstroi a barra de breadcrumbs a partir de `s_current_path`. Trunca com "..." se profunda. */
static void update_breadcrumb(void) {
  if (s_breadcrumb == nullptr) {
    return;
  }
  /* Botao "Ir para data" visivel apenas dentro de /CICLOS (ou a sua raiz). */
  if (s_date_goto_btn != nullptr) {
    const bool in_ciclos = (strncmp(s_current_path, "/CICLOS", 7) == 0) &&
                           (s_current_path[7] == '\0' || s_current_path[7] == '/');
    if (in_ciclos) {
      lv_obj_clear_flag(s_date_goto_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_date_goto_btn, LV_OBJ_FLAG_HIDDEN);
    }
  }
  lv_obj_clean(s_breadcrumb);

  const bool at_root = (strcmp(s_current_path, kFsRoot) == 0);
  breadcrumb_add_chip(LV_SYMBOL_HOME " sd", 0, at_root);
  if (at_root) {
    return;
  }

  /* Tokeniza s_current_path em segmentos. */
  char buf[sizeof(s_current_path)];
  strlcpy(buf, s_current_path, sizeof(buf));
  const char *tokens[16];
  int cnt = 0;
  char *save = nullptr;
  for (char *tok = strtok_r(buf, "/", &save); tok != nullptr && cnt < 16;
       tok = strtok_r(nullptr, "/", &save)) {
    tokens[cnt++] = tok;
  }

  constexpr int kMaxVisible = 3; /* max. segmentos para alem do Home antes de truncar */
  int start = 0;
  if (cnt > kMaxVisible) {
    lv_obj_t *ell = lv_label_create(s_breadcrumb);
    lv_label_set_text(ell, "...");
    lv_obj_set_style_pad_hor(ell, 4, 0);
    lv_obj_set_style_text_color(ell, UI_COLOR_TEXT_MUTED, 0);
    start = cnt - kMaxVisible;
  }
  for (int i = start; i < cnt; i++) {
    const bool is_current = (i == cnt - 1);
    breadcrumb_add_chip(tokens[i], i + 1, is_current);
  }
}

static void update_path_label(void) {
  /* Compat: o caminho e' agora mostrado via breadcrumb (chips). */
  update_breadcrumb();
}

static bool is_text_preview_file(const char *full_path) {
  if (full_path == nullptr) {
    return false;
  }
  const size_t len = strlen(full_path);
  return (len >= 4 && strcasecmp(full_path + len - 4, ".txt") == 0) ||
         (len >= 4 && strcasecmp(full_path + len - 4, ".log") == 0);
}

static void format_entry_datetime(time_t ts, char *out, size_t out_sz) {
  if (out == nullptr || out_sz == 0U) {
    return;
  }
  if (ts <= (time_t)1577836800) {
    strlcpy(out, "--/--/---- --:--", out_sz);
    return;
  }
  struct tm ti = {};
  if (localtime_r(&ts, &ti) == nullptr || strftime(out, out_sz, "%d/%m/%Y %H:%M", &ti) == 0U) {
    strlcpy(out, "--/--/---- --:--", out_sz);
  }
}

/* ── Gerenciamento de estado do visualizador paginado ────────────────── */

/** Libera recursos do visualizador paginado (PSRAM, estado). */
static void viewer_free_state(void) {
  if (s_highlight_blink_timer != nullptr) {
    lv_timer_del(s_highlight_blink_timer);
    s_highlight_blink_timer = nullptr;
  }
  s_highlight_row = UINT16_MAX;
  s_highlight_ticks_remaining = 0;
  s_highlight_on = false;
  if (s_line_offsets != nullptr) {
    heap_caps_free(s_line_offsets);
    s_line_offsets = nullptr;
  }
  s_total_lines = 0;
  s_file_size = 0;
  s_window_start = 0;
  s_window_count = 0;
  s_viewer_path[0] = '\0';
  s_viewer_path_label = nullptr;
  s_viewer_info_label = nullptr;
  s_loading_window = false;
  s_last_reload_ms = 0;
  s_index_truncated = false;
}

/** Atualiza o label de posicao na barra superior do visualizador. */
static void update_viewer_info(void) {
  if (s_viewer_info_label == nullptr) {
    return;
  }
  if (s_total_lines == 0) {
    lv_label_set_text(s_viewer_info_label, "0 linhas");
    return;
  }
  const unsigned first = s_window_start + 1;
  unsigned last = s_window_start + s_window_count;
  if (last > s_total_lines) {
    last = s_total_lines;
  }

  if (s_file_size >= 1024) {
    lv_label_set_text_fmt(s_viewer_info_label, "Linhas %u-%u de %u  (%u KB)",
                          first, last, s_total_lines, (unsigned)(s_file_size / 1024));
  } else {
    lv_label_set_text_fmt(s_viewer_info_label, "Linhas %u-%u de %u  (%u B)",
                          first, last, s_total_lines, (unsigned)s_file_size);
  }
}

static void close_overlay_cb(lv_event_t *e) {
  (void)e;
  s_viewer_table = nullptr;
  viewer_free_state();
  if (s_overlay != nullptr) {
    lv_obj_del(s_overlay);
    s_overlay = nullptr;
  }
}

/* ── Funcoes de manipulacao de texto ─────────────────────────────────── */

/** Corta linha no buffer (ASCII .txt/.log) para caber na célula da tabela. */
static void truncate_line_for_cell(char *line) {
  size_t L = strlen(line);
  if (L <= kMaxCellChars) {
    return;
  }
  if (kMaxCellChars < 4) {
    line[0] = '\0';
    return;
  }
  line[kMaxCellChars - 3] = '.';
  line[kMaxCellChars - 2] = '.';
  line[kMaxCellChars - 1] = '.';
  line[kMaxCellChars] = '\0';
}

/** Substitui bytes de controlo (excepto TAB) para evitar glitches na tabela LVGL. */
static void normalize_text_buffer(char *buf) {
  if (buf == nullptr) {
    return;
  }
  for (char *p = buf; *p != '\0'; ++p) {
    const unsigned char c = (unsigned char)*p;
    if (c == '\r' || c == '\n' || c == '\t') {
      continue;
    }
    if (c < 0x20U || c == 0x7FU) {
      *p = ' ';
    }
  }
}

/**
 * Parte o texto em linhas no próprio buffer (substitui '\n' por '\0').
 * @param truncated true se o buffer tiver mais linhas do que max_lines.
 */
static unsigned split_lines_inplace(char *buf, char **out_lines, unsigned max_lines, bool *truncated) {
  *truncated = false;
  if (buf == nullptr || max_lines == 0) {
    return 0;
  }
  unsigned n = 0;
  out_lines[n++] = buf;
  char *p = buf;
  while (*p != '\0') {
    char *nl = strchr(p, '\n');
    if (nl == nullptr) {
      return n;
    }
    *nl = '\0';
    if (nl > p && nl[-1] == '\r') {
      nl[-1] = '\0';
    }
    p = nl + 1;
    if (n >= max_lines) {
      *truncated = true;
      return max_lines;
    }
    out_lines[n++] = p;
  }
  return n;
}

/* ── Janela deslizante: carga de trecho do arquivo ───────────────────── */

/**
 * Carrega uma janela de linhas do arquivo aberto e popula a lv_table.
 * Faz unlock/lock do mutex LVGL internamente para acesso ao SD.
 * @param first_line Indice (0-based) da primeira linha a carregar.
 * @param count Numero maximo de linhas a carregar.
 * @return true se a carga foi bem-sucedida.
 */
static bool load_viewer_window(unsigned first_line, unsigned count) {
  if (s_viewer_table == nullptr || s_line_offsets == nullptr || s_total_lines == 0) {
    return false;
  }

  if (first_line >= s_total_lines) {
    first_line = s_total_lines - 1;
  }
  if (first_line + count > s_total_lines) {
    count = s_total_lines - first_line;
  }
  /* s_line_ptrs[] tem tamanho kWindowLines+2 — clampar antes de split_lines_inplace
   * para evitar buffer overflow no array de ponteiros (causa do crash LoadProhibited
   * em truncate_line_for_cell quando file cresce > kWindowLines linhas em soak). */
  if (count > kWindowLines) {
    count = kWindowLines;
  }
  if (count == 0) {
    return false;
  }

  /* Calcular range de bytes a ler do arquivo. */
  const uint32_t start_offset = s_line_offsets[first_line];
  uint32_t end_offset;
  if (first_line + count < s_total_lines) {
    end_offset = s_line_offsets[first_line + count];
  } else {
    end_offset = (uint32_t)s_file_size;
  }

  size_t bytes_needed = end_offset - start_offset;

  /* Se os dados excedem o buffer, reduzir a janela. */
  while (bytes_needed > kTextBufSize - 64 && count > 1) {
    count--;
    if (first_line + count < s_total_lines) {
      end_offset = s_line_offsets[first_line + count];
    } else {
      end_offset = (uint32_t)s_file_size;
    }
    bytes_needed = end_offset - start_offset;
  }
  if (bytes_needed > kTextBufSize - 64) {
    bytes_needed = kTextBufSize - 64;
  }

  s_loading_window = true;

  size_t nread = 0;
  bool read_ok = false;

  lvgl_port_unlock();

  sd_access_sync([&] {
    File f = SD.open(s_viewer_path, FILE_READ);
    if (f) {
      f.seek(start_offset);
      while (nread < bytes_needed) {
        const size_t chunk = ((bytes_needed - nread) > 512U) ? 512U : (bytes_needed - nread);
        const int got = f.read((uint8_t *)(s_text_buf + nread), chunk);
        if (got <= 0) {
          break;
        }
        nread += (size_t)got;
      }
      f.close();
      read_ok = true;
    }
  });

  (void)lvgl_port_lock(-1);

  if (!read_ok || nread == 0) {
    s_loading_window = false;
    return false;
  }

  s_text_buf[nread] = '\0';
  normalize_text_buffer(s_text_buf);

  bool dummy_trunc = false;
  const unsigned nlines = split_lines_inplace(s_text_buf, s_line_ptrs, count, &dummy_trunc);

  lv_obj_t *table = s_viewer_table;

  const bool show_overflow = (s_index_truncated && first_line + nlines >= s_total_lines);
  const unsigned extra = show_overflow ? 1u : 0u;

  lv_table_set_row_cnt(table, (uint16_t)(nlines + extra));

  char num[12];
  for (unsigned i = 0; i < nlines; i++) {
    truncate_line_for_cell(s_line_ptrs[i]);
    snprintf(num, sizeof(num), "%u", first_line + i + 1);
    lv_table_set_cell_value(table, i, 0, num);
    lv_table_set_cell_value(table, i, 1, s_line_ptrs[i]);
  }

  if (show_overflow) {
    lv_table_set_cell_value(table, nlines, 0, "!");
    lv_table_set_cell_value(table, nlines, 1,
                            "(Arquivo excede limite de linhas indexaveis.)");
  }

  s_window_start = first_line;
  s_window_count = nlines;

  update_viewer_info();

  s_loading_window = false;
  return true;
}

/**
 * Callback de scroll na tabela do visualizador: detecta proximidade das bordas
 * e carrega nova janela de linhas automaticamente (sliding window).
 */
static void viewer_scroll_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_SCROLL) {
    return;
  }
  if (s_loading_window || s_total_lines == 0 || s_viewer_table == nullptr) {
    return;
  }
  if (s_line_offsets == nullptr) {
    return;
  }

  /* Cooldown para evitar recargas excessivas durante animacao de scroll. */
  const uint32_t now = millis();
  if (now - s_last_reload_ms < kReloadCooldownMs) {
    return;
  }

  lv_obj_t *table = s_viewer_table;
  lv_obj_update_layout(table);

  const lv_coord_t scroll_y = lv_obj_get_scroll_y(table);
  const lv_coord_t scroll_bottom = lv_obj_get_scroll_bottom(table);

  unsigned new_start = s_window_start;

  if (scroll_bottom < kScrollThresholdPx &&
      s_window_start + s_window_count < s_total_lines) {
    /* Perto do fim: deslocar janela para baixo. */
    new_start = s_window_start + kWindowShift;
    if (new_start + kWindowLines > s_total_lines && s_total_lines > kWindowLines) {
      /* Usar inicio que faz o sufixo caber no buffer (nao s_total_lines - kWindowLines). */
      new_start = viewer_tail_fit_first_line();
    }
  } else if (scroll_y < kScrollThresholdPx && s_window_start > 0) {
    /* Perto do topo: deslocar janela para cima. */
    new_start = (s_window_start > kWindowShift) ? s_window_start - kWindowShift : 0;
  }

  if (new_start == s_window_start) {
    return;
  }

  s_last_reload_ms = now;

  /* Calcular qual linha do arquivo esta visivel no topo do viewport. */
  const lv_coord_t total_h = lv_obj_get_self_height(table);
  unsigned visible_file_line = s_window_start;
  if (total_h > 0 && s_window_count > 0) {
    visible_file_line = s_window_start +
                        (unsigned)((uint32_t)scroll_y * s_window_count / (uint32_t)total_h);
  }

  {
    const unsigned max_lines = (s_total_lines > new_start) ? s_total_lines - new_start : 1;
    load_viewer_window(new_start, max_lines);
  }

  /* Restaurar posicao de scroll para manter a mesma linha visivel. */
  lv_obj_update_layout(table);
  const lv_coord_t new_total_h = lv_obj_get_self_height(table);
  if (new_total_h > 0 && s_window_count > 0 && visible_file_line >= s_window_start) {
    const unsigned row_in_window = visible_file_line - s_window_start;
    const lv_coord_t target_y = (lv_coord_t)(
        (uint32_t)row_in_window * (uint32_t)new_total_h / s_window_count);
    lv_obj_scroll_to_y(table, target_y, LV_ANIM_OFF);
  }
}

/**
 * Posiciona o visualizador na ultima pagina (equivalente ao botao Fim na barra lateral).
 */
/**
 * Draw event no table: gutter (col 0) sempre com fundo cinza claro, texto alinhado a direita
 * e cor muted. Quando s_highlight_on e a linha e s_highlight_row, o highlight amarelo tem
 * prioridade sobre o fundo do gutter (ambas as colunas ficam amarelas).
 */
static void viewer_table_draw_part_event_cb(lv_event_t *e) {
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (dsc == nullptr || dsc->part != LV_PART_ITEMS) {
    return;
  }
  /* dsc->id = row * col_cnt + col ; col_cnt = 2 */
  const uint32_t col = dsc->id % 2U;
  const uint32_t row = dsc->id / 2U;

  /* Highlight piscante RS485 tem prioridade: pinta toda a linha (col 0 e 1) de amarelo. */
  const bool is_highlight_row = (s_highlight_row != UINT16_MAX && s_highlight_on &&
                                  row == (uint32_t)s_highlight_row);
  if (is_highlight_row) {
    if (dsc->rect_dsc != nullptr) {
      dsc->rect_dsc->bg_color = lv_color_hex(0xFFEB3B); /* amarelo highlight */
      dsc->rect_dsc->bg_opa = LV_OPA_COVER;
    }
    return;
  }

  /* Gutter (col 0): fundo cinza claro + texto muted alinhado a direita. */
  if (col == 0U) {
    if (dsc->rect_dsc != nullptr) {
      dsc->rect_dsc->bg_color = ui_color_viewer_gutter_bg(app_settings_dark_mode());
      dsc->rect_dsc->bg_opa = LV_OPA_COVER;
    }
    if (dsc->label_dsc != nullptr) {
      dsc->label_dsc->color = UI_COLOR_TEXT_MUTED;
      dsc->label_dsc->align = LV_TEXT_ALIGN_RIGHT;
    }
  }
}

static void highlight_blink_timer_cb(lv_timer_t *t) {
  if (s_viewer_table == nullptr || s_highlight_ticks_remaining == 0) {
    s_highlight_on = false;
    s_highlight_row = UINT16_MAX;
    s_highlight_ticks_remaining = 0;
    if (s_viewer_table != nullptr) {
      lv_obj_invalidate(s_viewer_table);
    }
    lv_timer_del(t);
    s_highlight_blink_timer = nullptr;
    return;
  }
  s_highlight_on = !s_highlight_on;
  lv_obj_invalidate(s_viewer_table);
  s_highlight_ticks_remaining--;
}

/** Inicia blink (3 ciclos on/off = 6 ticks a 250 ms) na ultima linha da janela. */
static void viewer_highlight_new_line_start(void) {
  if (s_viewer_table == nullptr || s_window_count == 0) {
    return;
  }
  s_highlight_row = (uint16_t)(s_window_count - 1U);
  s_highlight_ticks_remaining = 6U;
  s_highlight_on = true;
  lv_obj_invalidate(s_viewer_table);
  if (s_highlight_blink_timer == nullptr) {
    s_highlight_blink_timer = lv_timer_create(highlight_blink_timer_cb, 250, nullptr);
  } else {
    lv_timer_reset(s_highlight_blink_timer);
  }
}

static void viewer_scroll_to_last_page(void) {
  if (s_viewer_table == nullptr || s_line_offsets == nullptr || s_total_lines == 0) {
    return;
  }
  const unsigned new_start = viewer_tail_fit_first_line();
  const unsigned max_lines = (s_total_lines > new_start) ? s_total_lines - new_start : 1U;
  (void)load_viewer_window(new_start, max_lines);
  lv_obj_update_layout(s_viewer_table);
  lv_obj_scroll_to_y(s_viewer_table, LV_COORD_MAX, LV_ANIM_OFF);
  update_viewer_info();
}

/**
 * Se o mesmo .txt ja esta aberto e o ficheiro cresceu (append), estende apenas o indice
 * de linhas a partir de s_file_size — muito mais rapido que reindexar tudo (fila sd_io).
 * @return true se o caso foi tratado (ou nada a fazer); false para forcar show_text_file completo.
 */
static bool viewer_try_extend_index_after_grow(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (s_overlay == nullptr || s_viewer_table == nullptr || s_line_offsets == nullptr) {
    return false;
  }
  if (s_total_lines == 0U || strcmp(s_viewer_path, path) != 0 || s_index_truncated) {
    return false;
  }

  const size_t old_sz = s_file_size;
  size_t new_sz = old_sz;

  lvgl_port_unlock();
  /* Sem _front: refresh nunca precede escritas pendentes do reader RS485. */
  sd_access_sync([&]() {
    File f = SD.open(path, FILE_READ);
    if (f && !f.isDirectory()) {
      new_sz = f.size();
    }
    if (f) {
      f.close();
    }
  });
  (void)lvgl_port_lock(-1);

  if (new_sz < old_sz) {
    return false;
  }
  if (new_sz <= old_sz) {
    return true;
  }

  static constexpr size_t kTailChunk = 2048U;
  /* Inicia 1 byte antes de old_sz (se houver) para detectar se o ficheiro antigo
   * acabava em LF: nesse caso old_sz e' o inicio da primeira linha nova. Sem isso,
   * cada chamada perderia a primeira linha adicionada (o LF pertence ao ficheiro antigo
   * mas o start da nova linha e' a partir de old_sz). */
  const size_t scan_pos_initial = (old_sz > 0U) ? old_sz - 1U : 0U;
  size_t scan_pos = scan_pos_initial;
  while (scan_pos < new_sz) {
    size_t to_read = new_sz - scan_pos;
    if (to_read > kTailChunk) {
      to_read = kTailChunk;
    }
    char chunk[kTailChunk];
    int got = 0;
    lvgl_port_unlock();
    sd_access_sync([&]() {
      File f = SD.open(path, FILE_READ);
      if (!f || f.isDirectory()) {
        return;
      }
      if (!f.seek(scan_pos)) {
        f.close();
        return;
      }
      got = f.read(reinterpret_cast<uint8_t *>(chunk), to_read);
      f.close();
    });
    (void)lvgl_port_lock(-1);

    if (got <= 0) {
      return false;
    }
    for (int i = 0; i < got; ++i) {
      if (chunk[i] != '\n') {
        continue;
      }
      const uint32_t next_start = static_cast<uint32_t>(scan_pos + static_cast<size_t>(i) + 1U);
      /* Ignora "fantasma" no EOF (LF final) e LFs anteriores a old_sz (ja indexados). */
      if (next_start >= static_cast<uint32_t>(new_sz)) {
        continue;
      }
      if (next_start < static_cast<uint32_t>(old_sz)) {
        continue;
      }
      if (s_total_lines >= kMaxIndexLines) {
        return false;
      }
      uint32_t *grown = static_cast<uint32_t *>(
          heap_caps_realloc(s_line_offsets, (s_total_lines + 1U) * sizeof(uint32_t), MALLOC_CAP_SPIRAM));
      if (grown == nullptr) {
        return false;
      }
      s_line_offsets = grown;
      s_line_offsets[s_total_lines] = next_start;
      s_total_lines++;
    }
    scan_pos += static_cast<size_t>(got);
  }

  s_file_size = new_sz;
  viewer_scroll_to_last_page();
  viewer_highlight_new_line_start();
  return true;
}

/**
 * Apos cada linha RS485 gravada (contador na tarefa sd_io), reabre o .txt do dia no
 * visualizador com scroll no fim (mutex LVGL detido pelo handler de timer).
 */
static void rs485_open_timer_cb(lv_timer_t * /*t*/) {
  const uint32_t seq = s_rs485_saved_seq.load(std::memory_order_relaxed);
  if (seq == s_rs485_saved_last_handled) {
    return;
  }
  /* Guard heap: abrir viewer aloca widgets LVGL + buffers no heap interno (DRAM).
   * Com heap interno < 24 KB, skip para evitar OOM/PANIC (v1.02 issue: crash apos
   * idle prolongado quando chega linha RS485 com heap baixo). Seq fica unhandled,
   * proxima tentativa re-avalia com heap eventualmente recuperado. */
  static constexpr uint32_t kRs485OpenMinInternalHeap = 24U * 1024U;
  const uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (heap_free < kRs485OpenMinInternalHeap) {
    static uint32_t last_warn_ms = 0;
    const uint32_t now_ms = lv_tick_get();
    if (now_ms - last_warn_ms > 5000U) {
      app_log_writef("WARN", "RS485 auto-open: heap interno baixo (%u B), skip",
                     (unsigned)heap_free);
      last_warn_ms = now_ms;
    }
    return;
  }
  if (s_root == nullptr) {
    /* File browser nao esta visivel: mudar para ele se o SD estiver disponivel.
     * Nao marcar seq como handled — o proximo tick abre o ficheiro com s_root ja definido. */
    if (sd_access_is_mounted() && s_auto_open_cb != nullptr) {
      s_auto_open_cb();
    } else {
      s_rs485_saved_last_handled = seq; /* SD em falta: descartar para evitar loop. */
    }
    return;
  }
  if (!sd_access_is_mounted()) {
    s_rs485_saved_last_handled = seq;
    return;
  }
  s_rs485_saved_last_handled = seq;
  char path[256];
  if (!cycles_rs485_format_today_path(path, sizeof path)) {
    return;
  }
  bool exists = false;
  lvgl_port_unlock();
  /* Sem _front: evita furar a fila do SD e atrasar a escrita de linhas pelo reader RS485. */
  sd_access_sync([&path, &exists]() {
    if (SD.exists(path)) {
      File probe = SD.open(path, FILE_READ);
      if (probe && !probe.isDirectory()) {
        exists = true;
      }
      if (probe) {
        probe.close();
      }
    }
  });
  (void)lvgl_port_lock(-1);
  if (exists) {
    if (!viewer_try_extend_index_after_grow(path)) {
      show_text_file(path, true, true);
    }
  }
}

/**
 * Se o overlay mostra o ficheiro .txt do dia em /CICLOS e o tamanho na SD aumentou,
 * reindexa e salta para o fim (novas linhas RS485 visiveis).
 */
static void cycles_live_timer_cb(lv_timer_t * /*t*/) {
  if (s_overlay == nullptr || s_viewer_path[0] == '\0' || s_loading_window) {
    return;
  }
  char today[256];
  if (!cycles_rs485_format_today_path(today, sizeof today)) {
    return;
  }
  if (strcmp(s_viewer_path, today) != 0) {
    return;
  }
  size_t sz = 0;
  lvgl_port_unlock();
  /* Sem _front: refresh nao deve atrasar escritas pendentes no SD. */
  sd_access_sync([&sz]() {
    File f = SD.open(s_viewer_path, FILE_READ);
    if (f) {
      sz = f.size();
      f.close();
    }
  });
  (void)lvgl_port_lock(-1);

  if (sz > s_file_size) {
    if (!viewer_try_extend_index_after_grow(today)) {
      show_text_file(s_viewer_path, true, true);
    }
  }
}

/* ── Visualizador de arquivo texto (.txt / .log) ─────────────────────── */

static void show_text_file(const char *full_path, bool quiet_index, bool scroll_to_end) {
  struct SuppressAutoFileListRefreshGuard {
    SuppressAutoFileListRefreshGuard() { ++s_suppress_auto_file_list_refresh; }
    ~SuppressAutoFileListRefreshGuard() { --s_suppress_auto_file_list_refresh; }
  } suppress_list_refresh;

  char path_work[256];
  strlcpy(path_work, full_path != nullptr ? full_path : "", sizeof(path_work));

  const lv_font_t *ui_font = s_active_ui_font != nullptr
                                  ? s_active_ui_font
                                  : (const lv_font_t *)LV_FONT_DEFAULT;
  const lv_font_t *mono_font = viewer_monospace_font_for_settings();

  lv_obj_t *const host = (s_root != nullptr) ? lv_obj_get_parent(s_root) : nullptr;
  lv_obj_t *const ov_parent = (host != nullptr) ? host : lv_layer_top();

  if (s_overlay != nullptr) {
    s_viewer_table = nullptr;
    lv_obj_del(s_overlay);
    s_overlay = nullptr;
  }
  viewer_free_state();
  strlcpy(s_viewer_path, path_work, sizeof(s_viewer_path));

  if (!quiet_index) {
    ui_loading_show(ov_parent, "Indexando arquivo...");
    /* Sem lv_refr_now aqui: com o mutex LVGL detido pelo setup ou pela tarefa LVGL, o flush
     * sincrono pode bloquear a tarefa lvgl (RGB) e congelar barra de estado / touch. */
  }

  /* ── Fase 1: indexar offsets de linhas (sem mutex LVGL) ──────────── */

  lvgl_port_unlock();

  bool open_fail = false;
  bool is_dir_file = false;

  uint32_t *offsets = static_cast<uint32_t *>(
      heap_caps_malloc(kMaxIndexLines * sizeof(uint32_t), MALLOC_CAP_SPIRAM));

  if (offsets == nullptr) {
    app_log_writef("WARN", "PSRAM: falha ao alocar indice de linhas (%u bytes)",
                   (unsigned)(kMaxIndexLines * sizeof(uint32_t)));
    open_fail = true;
  }

  const uint32_t t0 = millis();

  if (!open_fail) {
    /* Abrir e obter tamanho num job curto; a leitura completa nao pode ficar num unico
     * sd_access_sync — bloqueia sd_io durante segundos (ficheiros MB numa linha) e
     * outros clientes (ex.: async_tcp a servir /api/fs/file) disparam o task WDT. */
    sd_access_sync([&] {
      File f = SD.open(path_work, FILE_READ);
      if (!f) {
        open_fail = true;
        return;
      }
      if (f.isDirectory()) {
        is_dir_file = true;
        f.close();
        return;
      }
      s_file_size = f.size();
      f.close();
    });
  }

  if (!open_fail && !is_dir_file) {
    /* Chunks maiores = menos jobs na fila sd_io. O portal HTTP usa sd_access_sync_front()
     * para saltar a frente desta fila (evita TWDT em async_tcp durante indexacao longa). */
    static constexpr size_t kScanChunk = 16384;
    /* static: evita stack grande na task LVGL; show_text_file nao e reentrante. */
    static char scan_buf[kScanChunk];
    offsets[0] = 0;
    unsigned line_count = 1;
    size_t pos = 0;

    while (pos < s_file_size && line_count < kMaxIndexLines) {
      int got = 0;
      sd_access_sync([&] {
        File f = SD.open(path_work, FILE_READ);
        if (!f) {
          open_fail = true;
          return;
        }
        if (!f.seek(static_cast<uint32_t>(pos))) {
          f.close();
          open_fail = true;
          return;
        }
        size_t to_read = kScanChunk;
        if (pos + to_read > s_file_size) {
          to_read = s_file_size - pos;
        }
        got = f.read(reinterpret_cast<uint8_t *>(scan_buf), to_read);
        f.close();
      });
      if (open_fail || got <= 0) {
        break;
      }
      for (int i = 0; i < got && line_count < kMaxIndexLines; i++) {
        if (scan_buf[i] == '\n') {
          offsets[line_count++] = static_cast<uint32_t>(pos + static_cast<size_t>(i) + 1U);
        }
      }
      pos += static_cast<size_t>(got);
    }

    if (!open_fail) {
      /* Remover ultima linha se for vazia (trailing newline). */
      if (line_count > 1 && offsets[line_count - 1] >= static_cast<uint32_t>(s_file_size)) {
        line_count--;
      }
      s_total_lines = line_count;
      s_index_truncated = (line_count >= kMaxIndexLines);
    }
  }

  const uint32_t dt_ms = millis() - t0;

  if (open_fail || is_dir_file) {
    s_total_lines = 0;
    s_file_size = 0;
    if (offsets != nullptr) {
      heap_caps_free(offsets);
    }
    s_line_offsets = nullptr;
  } else {
    /* Reduzir alocacao ao tamanho efetivo. */
    if (s_total_lines > 0 && s_total_lines < kMaxIndexLines) {
      uint32_t *shrunk = static_cast<uint32_t *>(
          heap_caps_realloc(offsets, s_total_lines * sizeof(uint32_t), MALLOC_CAP_SPIRAM));
      if (shrunk != nullptr) {
        offsets = shrunk;
      }
    }
    s_line_offsets = offsets;

    app_log_writef("INFO", "Indexacao arquivo: %s (%u linhas, %u bytes, %ums)",
                   path_work, s_total_lines, (unsigned)s_file_size, (unsigned)dt_ms);
  }

  (void)lvgl_port_lock(-1);

  if (!quiet_index) {
    ui_loading_hide();
  }

  /* ── Fase 2: criar overlay e widgets ─────────────────────────────── */

  s_overlay = lv_obj_create(ov_parent);
  lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(s_overlay, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_overlay, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(s_overlay, 4, 0);
  lv_obj_set_style_pad_row(s_overlay, 4, 0);

  /* Barra superior: Voltar + nome/caminho do ficheiro + indicador de linhas. */
  lv_obj_t *back_row = lv_obj_create(s_overlay);
  lv_obj_remove_style_all(back_row);
  lv_obj_set_width(back_row, LV_PCT(100));
  lv_obj_set_height(back_row, LV_SIZE_CONTENT);
  lv_obj_clear_flag(back_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(back_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(back_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(back_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(back_row, 8, 0);

  lv_obj_t *back = lv_btn_create(back_row);
  lv_obj_t *back_lbl = lv_label_create(back);
  viewer_style_ios_back_button(back, back_lbl, ui_font);
  lv_obj_add_event_cb(back, close_overlay_cb, LV_EVENT_CLICKED, nullptr);

  /* Botao "Partilhar": abre modal com QR code e URL de descarga. */
  lv_obj_t *share_btn = lv_btn_create(back_row);
  lv_obj_set_size(share_btn, 48, 36);
  lv_obj_set_style_bg_color(share_btn, UI_COLOR_PRIMARY, 0);
  lv_obj_t *share_lbl = lv_label_create(share_btn);
  lv_label_set_text(share_lbl, LV_SYMBOL_UPLOAD);
  lv_obj_center(share_lbl);
  lv_obj_add_event_cb(
      share_btn,
      [](lv_event_t * /*e*/) { ui_share_qr_show(s_viewer_path); },
      LV_EVENT_CLICKED, nullptr);

  s_viewer_path_label = lv_label_create(back_row);
  /** Uma linha: caminho VFS completo (inclui o nome do ficheiro); DOT corta com "..." se nao couber. */
  lv_label_set_long_mode(s_viewer_path_label, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(s_viewer_path_label, 1);
  if (path_work[0] == '\0') {
    lv_label_set_text(s_viewer_path_label, "—");
  } else {
    lv_label_set_text(s_viewer_path_label, path_work);
  }
  lv_obj_set_style_text_color(s_viewer_path_label, UI_COLOR_BORDER, LV_PART_MAIN);
  if (ui_font != nullptr) {
    lv_obj_set_style_text_font(s_viewer_path_label, ui_font, LV_PART_MAIN);
  }

  s_viewer_info_label = lv_label_create(back_row);
  lv_label_set_text(s_viewer_info_label, "");
  lv_obj_set_style_text_color(s_viewer_info_label, UI_COLOR_TEXT_MUTED, LV_PART_MAIN);
  if (ui_font != nullptr) {
    lv_obj_set_style_text_font(s_viewer_info_label, ui_font, LV_PART_MAIN);
  }
  lv_label_set_long_mode(s_viewer_info_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_viewer_info_label, LV_SIZE_CONTENT);

  /* Linha principal: tabela (flex grow) + coluna de botões à direita. */
  lv_obj_t *viewer_row = lv_obj_create(s_overlay);
  lv_obj_remove_style_all(viewer_row);
  lv_obj_set_width(viewer_row, LV_PCT(100));
  lv_obj_set_flex_grow(viewer_row, 1);
  lv_obj_set_layout(viewer_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(viewer_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(viewer_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(viewer_row, 8, 0);
  lv_obj_clear_flag(viewer_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *table = lv_table_create(viewer_row);
  lv_obj_set_flex_grow(table, 1);
  lv_obj_set_height(table, LV_PCT(100));
  lv_table_set_col_cnt(table, 2);
  lv_obj_add_event_cb(table, viewer_table_on_size_cb, LV_EVENT_SIZE_CHANGED, nullptr);
  lv_obj_set_scrollbar_mode(table, LV_SCROLLBAR_MODE_AUTO);

  /* Registrar callback de scroll para janela deslizante. */
  lv_obj_add_event_cb(table, viewer_scroll_event_cb, LV_EVENT_SCROLL, nullptr);
  /* Draw event para highlight piscante da nova linha RS485. */
  lv_obj_add_event_cb(table, viewer_table_draw_part_event_cb, LV_EVENT_DRAW_PART_BEGIN, nullptr);

  lv_obj_t *btn_col = lv_obj_create(viewer_row);
  lv_obj_remove_style_all(btn_col);
  lv_obj_set_size(btn_col, kViewerBtnColW, LV_PCT(100));
  lv_obj_set_layout(btn_col, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(btn_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(btn_col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(btn_col, kViewerBtnPad, 0);
  lv_obj_clear_flag(btn_col, LV_OBJ_FLAG_SCROLLABLE);

  viewer_apply_compact_table_rows(table, mono_font);
  lv_obj_set_style_text_font(table, mono_font, LV_PART_ITEMS);
  apply_font_tree(btn_col, ui_font);

  s_viewer_table = table;

  viewer_make_nav_btn(btn_col, LV_SYMBOL_UP, kViewerNavUp, ui_font);
  viewer_make_nav_btn(btn_col, LV_SYMBOL_DOWN, kViewerNavDown, ui_font);
  viewer_make_nav_btn(btn_col, LV_SYMBOL_HOME, kViewerNavTop, ui_font);
  viewer_make_nav_btn(btn_col, LV_SYMBOL_NEXT, kViewerNavEnd, ui_font);
  viewer_make_nav_btn(btn_col, LV_SYMBOL_GPS, kViewerNavGoto, ui_font);

  /* ── Fase 3: popular a tabela ────────────────────────────────────── */

  if (open_fail) {
    lv_table_set_row_cnt(table, 1);
    lv_table_set_cell_value(table, 0, 0, "-");
    lv_table_set_cell_value(table, 0, 1, "(Nao foi possivel abrir o arquivo.)");
  } else if (is_dir_file) {
    lv_table_set_row_cnt(table, 1);
    lv_table_set_cell_value(table, 0, 0, "-");
    lv_table_set_cell_value(table, 0, 1, "(E uma pasta, nao um arquivo.)");
  } else if (s_total_lines == 0) {
    lv_table_set_row_cnt(table, 1);
    lv_table_set_cell_value(table, 0, 0, "1");
    lv_table_set_cell_value(table, 0, 1, "(vazio)");
  } else if (scroll_to_end) {
    viewer_scroll_to_last_page();
  } else {
    /* Carregar primeira janela de linhas. */
    load_viewer_window(0, kWindowLines);
  }

  lv_obj_update_layout(s_overlay);
  viewer_table_fit_width(table);
}

/* ── Explorador de ficheiros (lista de directorio) ───────────────────── */

static void refresh_file_list(bool show_loading_overlay);

static lv_obj_t *file_browser_loading_parent(void) {
  if (s_root != nullptr && lv_obj_get_parent(s_root) != nullptr) {
    return lv_obj_get_parent(s_root);
  }
  return s_root;
}

static void on_list_btn_clicked(lv_event_t *e) {
  uintptr_t tag = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (tag == kUserDataParentDir) {
    go_parent_dir();
    refresh_file_list(true);
    return;
  }
  if (tag >= s_entry_count) {
    return;
  }
  const char *full = s_entry_paths[tag];
  if (s_entry_is_dir[tag]) {
    strlcpy(s_current_path, full, sizeof(s_current_path));
    refresh_file_list(true);
    return;
  }
  if (is_text_preview_file(full)) {
    show_text_file(full);
  }
}

/**
 * Finaliza a listagem na task LVGL (executado via lv_async_call).
 * Preenche `s_list` a partir dos buffers preenchidos pelo scan sd_io.
 */
static void refresh_file_list_finish_cb(void * /*user_data*/) {
  /* UI pode ter sido desmontada enquanto o scan corria. */
  if (s_list == nullptr || s_breadcrumb == nullptr) {
    s_refresh_in_flight = false;
    return;
  }

  lv_obj_clean(s_list);
  s_entry_count = 0;

  if (!s_async_dir_ok) {
    app_log_writef("WARN", "Falha ao abrir diretorio no SD: %s", s_current_path);
    update_breadcrumb();
    if (s_async_show_overlay) {
      ui_loading_hide();
    }
    s_refresh_in_flight = false;
    return;
  }

  /* Sem entrada ".." na lista: navegacao ascendente e' feita pelo breadcrumb. */

  for (unsigned i = 0; i < s_async_scanned; i++) {
    const char *icon = s_entry_is_dir[i] ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
    lv_obj_t *btn = lv_list_add_btn(s_list, icon, s_entry_lines[i]);
    /* Linha ~29% mais alta e icone maior em cor primaria (pasta e ficheiro). */
    lv_obj_set_style_min_height(btn, 52, 0);
    lv_obj_t *icon_lbl = lv_obj_get_child(btn, 0);
    if (icon_lbl != nullptr) {
      lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_20, 0);
      lv_obj_set_style_text_color(icon_lbl, UI_COLOR_PRIMARY, 0);
    }
    lv_obj_add_event_cb(btn, on_list_btn_clicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }
  s_entry_count = s_async_scanned;

  update_path_label();
  s_last_refresh_ms = millis();
  if (s_last_refresh_ms == 0) {
    s_last_refresh_ms = 1;
  }
  if (s_async_show_overlay) {
    ui_loading_hide();
  }
  s_refresh_in_flight = false;
}

/**
 * Publica a finalizacao na task LVGL. Pode ser chamado a partir da task sd_io.
 * lv_async_call tem fila propria; protegido com lvgl_port_lock para serializar
 * acesso ao core LVGL (LVGL 8.3 nao exige mutex para lv_async_call, mas o
 * projeto ja utiliza port_lock como barreira geral de thread-safety).
 */
static void refresh_file_list_post_finish(void) {
  if (lvgl_port_lock(500)) {
    lv_async_call(refresh_file_list_finish_cb, nullptr);
    lvgl_port_unlock();
  } else {
    /* Fallback: se o lock LVGL nao pode ser obtido, libera a flag para evitar
     * ficar preso; o proximo clique volta a tentar. */
    s_refresh_in_flight = false;
  }
}

/**
 * Executa toda a listagem num unico job sd_io. Evita recursao:
 * `sd_access_async` executa inline quando o chamador ja esta na task sd_io,
 * logo re-enfileirar a partir de si proprio converte-se em recursao directa
 * e estoura a stack. Em vez disso fazemos um loop simples e intercalamos
 * vTaskDelay(1) a cada algumas entradas para ceder CPU a async_tcp/FTP
 * (evita TWDT). `kMaxListEntries`=48 mantem o tempo total < 1s tipico.
 */
static void refresh_scan_worker(void) {
  /**
   * VFS Arduino abre um FILE* (newlib fopen) por cada entrada listada, e cada
   * fopen aloca um mutex recursivo via xSemaphoreCreateRecursiveMutex. Se o
   * heap interno estiver baixo, o newlib chama abort() em lock_init_generic.
   * Rejeitamos o scan com aviso quando ha menos de kMinInternalHeapBytes livres.
   */
  static constexpr uint32_t kMinInternalHeapBytes = 12U * 1024U;
  const uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  Serial.printf("[fb] scan start: heap_int_free=%u\n", (unsigned)heap_free);
  if (heap_free < kMinInternalHeapBytes) {
    app_log_writef("WARN",
                   "File browser: heap interno baixo (%u B), scan abortado",
                   (unsigned)heap_free);
    s_async_dir_ok = false;
    refresh_file_list_post_finish();
    return;
  }

  File dir = SD.open(s_current_path);
  if (!dir || !dir.isDirectory()) {
    s_async_dir_ok = false;
    if (dir) {
      dir.close();
    }
    refresh_file_list_post_finish();
    return;
  }
  s_async_dir_ok = true;

  while (s_async_scanned < kMaxListEntries) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    s_async_consumed++;

    const char *fullpath = entry.path();
    const char *name = entry.name();
    if (fullpath == nullptr || name == nullptr || name[0] == '\0' ||
        strcmp(fullpath, s_current_path) == 0 || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) {
      entry.close();
      continue;
    }
    strlcpy(s_entry_paths[s_async_scanned], fullpath, sizeof(s_entry_paths[0]));
    s_entry_is_dir[s_async_scanned] = entry.isDirectory();
    char short_name[48];
    strlcpy(short_name, name, sizeof(short_name));
    const time_t last_write = entry.getLastWrite();
    char date_buf[20];
    format_entry_datetime(last_write, date_buf, sizeof(date_buf));
    /* Icone passa a ser parametro separado de lv_list_add_btn (permite recolor). */
    if (entry.isDirectory()) {
      snprintf(s_entry_lines[s_async_scanned], sizeof(s_entry_lines[0]),
               "%-22s  <DIR>  %s", short_name, date_buf);
    } else {
      snprintf(s_entry_lines[s_async_scanned], sizeof(s_entry_lines[0]),
               "%-22s  %8lu B  %s", short_name,
               (unsigned long)entry.size(), date_buf);
    }
    entry.close();
    s_async_scanned++;

    /* Ceder CPU periodicamente para async_tcp/FTP/LVGL em outros cores. */
    if ((s_async_scanned & 0x03U) == 0U) {
      vTaskDelay(1);
    }
  }
  dir.close();
  refresh_file_list_post_finish();
}

static void refresh_file_list(bool show_loading_overlay) {
  if (s_list == nullptr || s_breadcrumb == nullptr) {
    return;
  }

  bool expected = false;
  if (!s_refresh_in_flight.compare_exchange_strong(expected, true)) {
    /* Refresh ja em curso — ignorar para evitar duplicar scan. */
    return;
  }

  s_async_scanned = 0;
  s_async_consumed = 0;
  s_async_dir_ok = false;
  s_async_show_overlay = show_loading_overlay;

  if (show_loading_overlay) {
    ui_loading_show(file_browser_loading_parent(), "Carregando arquivos da pasta...");
    ui_loading_flush_display();
  }

  /**
   * Scan enfileirado via async: a task LVGL retorna imediatamente e continua
   * a redesenhar o overlay / responder a toque enquanto a task sd_io executa
   * o worker. vTaskDelay(1) periodico dentro do worker evita TWDT noutras
   * tasks (async_tcp / FTP).
   */
  sd_access_async([] { refresh_scan_worker(); });
}

void file_browser_detach_stale_widgets(void) {
  if (s_rs485_ui_follow_arm_timer != nullptr) {
    lv_timer_del(s_rs485_ui_follow_arm_timer);
    s_rs485_ui_follow_arm_timer = nullptr;
  }
  /* Nao desativar s_line_to_ui_follow: o sinal RS485 deve continuar ativo mesmo quando
   * o file browser nao esta visivel, para que o auto_open_cb possa mudar de view. */
  s_viewer_table = nullptr;
  s_overlay = nullptr;
  s_list = nullptr;
  s_path_label = nullptr;
  s_breadcrumb = nullptr;
  s_date_goto_btn = nullptr;
  s_root = nullptr;
  s_entry_count = 0;
  viewer_free_state();
}

void file_browser_goto(const char *path) {
  if (path == nullptr || path[0] == '\0' || s_root == nullptr) {
    return;
  }
  strlcpy(s_current_path, path, sizeof(s_current_path));
  refresh_file_list(true);
}

void file_browser_refresh(void) {
  refresh_file_list(true);
}

void file_browser_refresh_silent(void) {
  refresh_file_list(false);
}

bool file_browser_should_skip_auto_list_refresh(void) {
  return s_suppress_auto_file_list_refresh > 0 || s_overlay != nullptr;
}

uint32_t file_browser_last_refresh_ms(void) { return s_last_refresh_ms; }

void file_browser_apply_font(const lv_font_t *font) {
  if (font != nullptr) {
    s_active_ui_font = font;
  }
  if (s_root != nullptr && font != nullptr) {
    apply_font_tree(s_root, font);
  }
}

/** Apos o atraso pos-explorador, permite que linhas RS485 abram/atualizem o .txt do dia na UI. */
static void rs485_ui_follow_arm_timer_cb(lv_timer_t *t) {
  cycles_rs485_set_line_to_ui_follow(true);
  lv_timer_del(t);
  s_rs485_ui_follow_arm_timer = nullptr;
}

bool file_browser_init(lv_obj_t *parent) {
  if (parent == nullptr || !sd_access_is_mounted()) {
    return false;
  }

  if (s_rs485_ui_follow_arm_timer != nullptr) {
    lv_timer_del(s_rs485_ui_follow_arm_timer);
    s_rs485_ui_follow_arm_timer = nullptr;
  }

  if (s_overlay != nullptr) {
    s_viewer_table = nullptr;
    lv_obj_del(s_overlay);
    s_overlay = nullptr;
    viewer_free_state();
  }

  if (s_root != nullptr) {
    lv_obj_del(s_root);
    s_root = nullptr;
    s_path_label = nullptr;
    s_breadcrumb = nullptr;
    s_date_goto_btn = nullptr;
    s_list = nullptr;
  }

  strlcpy(s_current_path, kFsRoot, sizeof(s_current_path));

  s_root = lv_obj_create(parent);
  lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_layout(s_root, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(s_root, 4, 0);
  lv_obj_set_style_pad_row(s_root, 4, 0);

  /** Caminho: barra de chips + botao "Ir para data" (em `nav_row`). */
  s_path_label = nullptr;
  lv_obj_t *nav_row = lv_obj_create(s_root);
  lv_obj_set_width(nav_row, LV_PCT(100));
  lv_obj_set_height(nav_row, LV_SIZE_CONTENT);
  lv_obj_set_layout(nav_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(nav_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(nav_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(nav_row, 0, 0);
  lv_obj_set_style_pad_column(nav_row, 6, 0);
  lv_obj_set_style_border_width(nav_row, 0, 0);
  lv_obj_set_style_bg_opa(nav_row, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(nav_row, LV_OBJ_FLAG_SCROLLABLE);

  s_breadcrumb = lv_obj_create(nav_row);
  lv_obj_set_flex_grow(s_breadcrumb, 1);
  lv_obj_set_height(s_breadcrumb, LV_SIZE_CONTENT);
  lv_obj_set_layout(s_breadcrumb, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_breadcrumb, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_breadcrumb, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(s_breadcrumb, 4, 0);
  lv_obj_set_style_pad_column(s_breadcrumb, 6, 0);
  lv_obj_set_style_border_width(s_breadcrumb, 0, 0);
  lv_obj_set_style_bg_opa(s_breadcrumb, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(s_breadcrumb, LV_OBJ_FLAG_SCROLLABLE);

  s_date_goto_btn = lv_btn_create(nav_row);
  lv_obj_set_size(s_date_goto_btn, LV_SIZE_CONTENT, 36);
  lv_obj_set_style_bg_color(s_date_goto_btn, UI_COLOR_PRIMARY, 0);
  lv_obj_set_style_pad_hor(s_date_goto_btn, 12, 0);
  lv_obj_t *date_lbl = lv_label_create(s_date_goto_btn);
  lv_label_set_text(date_lbl, LV_SYMBOL_LIST " Ir para data");
  lv_obj_center(date_lbl);
  lv_obj_add_event_cb(
      s_date_goto_btn,
      [](lv_event_t * /*e*/) { ui_date_goto_show(); },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(s_date_goto_btn, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *hdr = lv_obj_create(s_root);
  lv_obj_set_width(hdr, LV_PCT(100));
  lv_obj_set_height(hdr, LV_SIZE_CONTENT);
  lv_obj_set_layout(hdr, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *h1 = lv_label_create(hdr);
  lv_label_set_text(h1, "Nome");
  lv_obj_t *h2 = lv_label_create(hdr);
  lv_label_set_text(h2, "Tamanho / Data");

  s_list = lv_list_create(s_root);
  lv_obj_set_width(s_list, LV_PCT(100));
  lv_obj_set_flex_grow(s_list, 1);

  /** Sem overlay no 1.o arranque: o setup corre com mutex LVGL; SD aqui libertava a tarefa LVGL. */
  refresh_file_list(false);

  if (s_cycles_live_timer == nullptr) {
    s_cycles_live_timer = lv_timer_create(cycles_live_timer_cb, 800, nullptr);
  }
  if (s_rs485_open_timer == nullptr) {
    /* 500 ms: refresh do viewer desacoplado da taxa de chegada de linhas,
     * evitando saturar o SD e starvar a escrita de novas linhas pelo reader RS485. */
    s_rs485_open_timer = lv_timer_create(rs485_open_timer_cb, 500, nullptr);
  }

  if (s_rs485_ui_follow_arm_timer == nullptr) {
    s_rs485_ui_follow_arm_timer = lv_timer_create(rs485_ui_follow_arm_timer_cb,
                                                   kRs485UiFollowArmDelayMs, nullptr);
  }

  return true;
}

bool file_browser_open_cycle_by_date(int year, int month, int day) {
  if (!sd_access_is_mounted() || s_root == nullptr) {
    return false;
  }
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) {
    return false;
  }
  char path[64];
  (void)snprintf(path, sizeof(path), "/CICLOS/%04d/%02d/%04d%02d%02d.txt", year, month, year, month, day);
  bool exists = false;
  lvgl_port_unlock();
  sd_access_sync([&path, &exists]() {
    if (SD.exists(path)) {
      File probe = SD.open(path, FILE_READ);
      if (probe && !probe.isDirectory()) {
        exists = true;
      }
      if (probe) {
        probe.close();
      }
    }
  });
  (void)lvgl_port_lock(-1);
  if (!exists) {
    return false;
  }
  show_text_file(path, false, true);
  return true;
}

void file_browser_open_today_cycles_txt(void) {
  if (!sd_access_is_mounted() || s_root == nullptr) {
    return;
  }
  char path[256];
  if (!cycles_rs485_format_today_path(path, sizeof path)) {
    return;
  }
  bool exists = false;
  lvgl_port_unlock();
  sd_access_sync([&path, &exists]() {
    if (SD.exists(path)) {
      File probe = SD.open(path, FILE_READ);
      if (probe && !probe.isDirectory()) {
        exists = true;
      }
      if (probe) {
        probe.close();
      }
    }
  });
  (void)lvgl_port_lock(-1);
  if (exists) {
    show_text_file(path, false, true);
  }
}

void file_browser_on_rs485_line_saved(void) {
  /* Contexto sd_io / RS485: apenas sinalizar; LVGL em rs485_open_timer_cb. */
  s_rs485_saved_seq.fetch_add(1u, std::memory_order_relaxed);
}

void file_browser_set_auto_open_cb(void (*cb)(void)) {
  s_auto_open_cb = cb;
}

void file_browser_rs485_follow_start(void) {
  if (s_rs485_open_timer == nullptr) {
    s_rs485_open_timer = lv_timer_create(rs485_open_timer_cb, 500, nullptr);
  }
  /* Arm timer: apos estabilizacao do boot, ativa s_line_to_ui_follow.
   * Apenas se ainda nao estiver ativo (evitar duplicado se file_browser_init correu antes). */
  if (s_rs485_ui_follow_arm_timer == nullptr) {
    s_rs485_ui_follow_arm_timer = lv_timer_create(rs485_ui_follow_arm_timer_cb,
                                                   kRs485UiFollowArmDelayMs, nullptr);
  }
}
