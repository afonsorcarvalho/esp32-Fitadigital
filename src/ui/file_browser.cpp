/**
 * @file file_browser.cpp
 * @brief Explorador tipo lista (Nome | Tamanho | Data). SD.open() usa raiz '/' (ver VFS Arduino).
 *
 * Texto do visualizador .txt/.log: fonte monoespaçada nativa LVGL (Unscii 8/16), p.ex. leitura de logs.
 * Courier New em vários px requer gerar .c com https://lvgl.io/tools/fontconverter a partir de cour.ttf.
 */
#include "file_browser.h"
#include <Arduino.h>
#include <SD.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "lvgl_port_v8.h"
#include "app_settings.h"
#include "app_log.h"
#include "sd_access.h"
#include "ui/ui_loading.h"

static constexpr uintptr_t kUserDataParentDir = 0xFFFFFFFFu;
static constexpr unsigned kMaxListEntries = 48;
static constexpr size_t kTxtMaxBytes = 8 * 1024;
/** Linhas no visualizador .txt: tabela com numeração; limite evita picos de RAM/tempo na abertura. */
static constexpr unsigned kMaxViewerLines = 220;
static constexpr unsigned kMaxCellChars = 120;
static constexpr char kFsRoot[] = "/";

static lv_obj_t *s_root = nullptr;
/** Fonte ativa (definições UI); nullptr usa LV_FONT_DEFAULT no visualizador. */
static const lv_font_t *s_active_ui_font = nullptr;
static lv_obj_t *s_path_label = nullptr;
static lv_obj_t *s_list = nullptr;
static char s_current_path[256];
static char s_entry_paths[kMaxListEntries][192];
static bool s_entry_is_dir[kMaxListEntries];
/** Texto da linha na lista (preenchido durante scan SD sem mutex LVGL). */
static char s_entry_lines[kMaxListEntries][128];
static unsigned s_entry_count = 0;
/** Timestamp (millis) da ultima vez que a lista de ficheiros foi atualizada na UI. */
static uint32_t s_last_refresh_ms = 0;
static lv_obj_t *s_overlay = nullptr;
/** Tabela do visualizador .txt (scroll); limpo ao fechar overlay. */
static lv_obj_t *s_viewer_table = nullptr;

/** Largura fixa da coluna de botões à direita do visualizador. */
static constexpr lv_coord_t kViewerBtnColW = 62;
/** Altura de cada botão de navegação (toque confortável). */
static constexpr lv_coord_t kViewerBtnH = 48;
/** Margem entre botões (flex column + pad). */
static constexpr lv_coord_t kViewerBtnPad = 6;

static constexpr intptr_t kViewerNavUp = 1;
static constexpr intptr_t kViewerNavDown = 2;
static constexpr intptr_t kViewerNavTop = 3;
static constexpr intptr_t kViewerNavEnd = 4;

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

/** Estilo “Voltar” tipo iOS: fundo transparente, texto azul, chevron + rótulo (símbolos FontAwesome na fonte UI). */
static void viewer_style_ios_back_button(lv_obj_t *btn, lv_obj_t *lbl, const lv_font_t *ui_font) {
  lv_obj_remove_style_all(btn);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(btn, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(btn, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
  lv_label_set_text(lbl, LV_SYMBOL_LEFT " Voltar");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x007AFF), LV_PART_MAIN);
  if (ui_font != nullptr) {
    lv_obj_set_style_text_font(lbl, ui_font, LV_PART_MAIN);
  }
  lv_obj_center(lbl);
}

static void viewer_nav_btn_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_viewer_table == nullptr) {
    return;
  }
  const intptr_t act = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
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
 * (ex.: lh/2) cortava descendentes e linhas com mais de uma linha visual (ver foto).
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
  /* Espaço para ~4 dígitos sem quebra vertical (depende da fonte nas definições). */
  const lv_coord_t num_w = 96;
  if (cw < num_w + 48) {
    return;
  }
  lv_table_set_col_width(table, 0, num_w);
  lv_table_set_col_width(table, 1, cw - num_w);
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

static void update_path_label(void) {
  if (s_path_label == nullptr) {
    return;
  }
  if (strcmp(s_current_path, kFsRoot) == 0) {
    lv_label_set_text(s_path_label, LV_SYMBOL_DRIVE " /sd");
  } else {
    lv_label_set_text_fmt(s_path_label, LV_SYMBOL_DRIVE " /sd%s", s_current_path);
  }
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

static void close_overlay_cb(lv_event_t *e) {
  (void)e;
  s_viewer_table = nullptr;
  if (s_overlay != nullptr) {
    lv_obj_del(s_overlay);
    s_overlay = nullptr;
  }
}

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
 * @param truncated true se o ficheiro tiver mais linhas do que max_lines.
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

static void show_text_file(const char *full_path) {
  static char text_buf[kTxtMaxBytes];
  static char *line_ptrs[kMaxViewerLines];

  /** Mutex ja' detido pela tarefa LVGL (evento de clique). */

  const lv_font_t *ui_font = s_active_ui_font != nullptr ? s_active_ui_font : (const lv_font_t *)LV_FONT_DEFAULT;
  const lv_font_t *mono_font = viewer_monospace_font_for_settings();

  /* Overlay no mesmo parent que o explorador (area abaixo da barra): barra de estado mantém-se visivel. */
  lv_obj_t *const host = (s_root != nullptr) ? lv_obj_get_parent(s_root) : nullptr;
  lv_obj_t *const ov_parent = (host != nullptr) ? host : lv_layer_top();

  ui_loading_show(ov_parent, "A ler ficheiro...");
  ui_loading_flush_display();

  lvgl_port_unlock();

  memset(text_buf, 0, sizeof(text_buf));
  bool file_truncated = false;
  bool open_fail = false;
  bool is_dir_file = false;
  size_t nread = 0;
  size_t total = 0;

  const uint32_t t0 = millis();
  sd_access_sync([&] {
    File f = SD.open(full_path, FILE_READ);
    if (!f) {
      open_fail = true;
    } else if (f.isDirectory()) {
      is_dir_file = true;
      f.close();
    } else {
      size_t room = sizeof(text_buf) - 64;
      while (nread < room) {
        const size_t chunk = ((room - nread) > 512U) ? 512U : (room - nread);
        const int got = f.read((uint8_t *)(text_buf + nread), chunk);
        if (got <= 0) {
          break;
        }
        nread += (size_t)got;
      }
      total = f.size();
      f.close();
      text_buf[nread] = '\0';
      normalize_text_buffer(text_buf);
      if (total > nread) {
        file_truncated = true;
        snprintf(text_buf + nread, sizeof(text_buf) - nread, "\n[... +%u bytes nao lidos ...]\n",
                   (unsigned)(total - (size_t)nread));
      }
    }
  });
  const uint32_t dt_ms = millis() - t0;
  if (open_fail) {
    app_log_writef("WARN", "Leitura ficheiro texto falhou: %s", full_path);
  } else if (!is_dir_file) {
    app_log_writef("INFO", "Leitura ficheiro texto: %s (%u/%u bytes, %ums)", full_path, (unsigned)nread,
                   (unsigned)total, (unsigned)dt_ms);
  }

  (void)lvgl_port_lock(-1);

  ui_loading_hide();

  s_overlay = lv_obj_create(ov_parent);
  lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(s_overlay, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_overlay, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(s_overlay, 4, 0);
  lv_obj_set_style_pad_row(s_overlay, 4, 0);

  lv_obj_t *back_row = lv_obj_create(s_overlay);
  lv_obj_remove_style_all(back_row);
  lv_obj_set_width(back_row, LV_PCT(100));
  lv_obj_set_height(back_row, LV_SIZE_CONTENT);
  lv_obj_clear_flag(back_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *back = lv_btn_create(back_row);
  lv_obj_t *back_lbl = lv_label_create(back);
  viewer_style_ios_back_button(back, back_lbl, ui_font);
  lv_obj_add_event_cb(back, close_overlay_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);

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

  /* Início: LV_SYMBOL_HOME (doc lv_symbol_def.h). Fim: LV_SYMBOL_NEXT (último segmento). */
  viewer_make_nav_btn(btn_col, LV_SYMBOL_UP, kViewerNavUp, ui_font);
  viewer_make_nav_btn(btn_col, LV_SYMBOL_DOWN, kViewerNavDown, ui_font);
  viewer_make_nav_btn(btn_col, LV_SYMBOL_HOME, kViewerNavTop, ui_font);
  viewer_make_nav_btn(btn_col, LV_SYMBOL_NEXT, kViewerNavEnd, ui_font);

  if (open_fail) {
    lv_table_set_row_cnt(table, 1);
    lv_table_set_cell_value(table, 0, 0, "-");
    lv_table_set_cell_value(table, 0, 1, "(Nao foi possivel abrir o ficheiro.)");
  } else if (is_dir_file) {
    lv_table_set_row_cnt(table, 1);
    lv_table_set_cell_value(table, 0, 0, "-");
    lv_table_set_cell_value(table, 0, 1, "(E uma pasta, nao um ficheiro.)");
  } else {
    bool lines_truncated = false;
    unsigned nlines = split_lines_inplace(text_buf, line_ptrs, kMaxViewerLines, &lines_truncated);
    if (nlines == 0) {
      lv_table_set_row_cnt(table, 1);
      lv_table_set_cell_value(table, 0, 0, "1");
      lv_table_set_cell_value(table, 0, 1, "(vazio)");
    } else {
      unsigned extra_rows = (file_truncated || lines_truncated) ? 1u : 0u;
      lv_table_set_row_cnt(table, (uint16_t)(nlines + extra_rows));
      char num[12];
      for (unsigned i = 0; i < nlines; i++) {
        truncate_line_for_cell(line_ptrs[i]);
        snprintf(num, sizeof(num), "%u", i + 1);
        lv_table_set_cell_value(table, i, 0, num);
        lv_table_set_cell_value(table, i, 1, line_ptrs[i]);
      }
      if (extra_rows) {
        lv_table_set_cell_value(table, nlines, 0, "!");
        const char *msg = lines_truncated
                              ? "(Mais linhas no ficheiro; mostradas apenas as primeiras.)"
                              : "(Conteudo do ficheiro foi truncado na leitura.)";
        lv_table_set_cell_value(table, nlines, 1, msg);
      }
    }
  }

  lv_obj_update_layout(s_overlay);
  viewer_table_fit_width(table);
}

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

static void refresh_file_list(bool show_loading_overlay) {
  if (s_list == nullptr || s_path_label == nullptr) {
    return;
  }

  if (show_loading_overlay) {
    ui_loading_show(file_browser_loading_parent(), "A carregar ficheiros da pasta...");
    ui_loading_flush_display();
  }

  lv_obj_clean(s_list);
  s_entry_count = 0;

  lvgl_port_unlock();

  unsigned scanned = 0;
  bool dir_ok = false;
  sd_access_sync([&] {
    File dir = SD.open(s_current_path);
    if (dir && dir.isDirectory()) {
      dir_ok = true;
      for (;;) {
        File entry = dir.openNextFile();
        if (!entry) {
          break;
        }
        if (scanned >= kMaxListEntries) {
          entry.close();
          break;
        }
        const char *fullpath = entry.path();
        const char *name = entry.name();
        if (fullpath == nullptr || name == nullptr || name[0] == '\0') {
          entry.close();
          continue;
        }
        if (strcmp(fullpath, s_current_path) == 0) {
          entry.close();
          continue;
        }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
          entry.close();
          continue;
        }
        strlcpy(s_entry_paths[scanned], fullpath, sizeof(s_entry_paths[0]));
        s_entry_is_dir[scanned] = entry.isDirectory();
        char short_name[48];
        strlcpy(short_name, name, sizeof(short_name));
        const time_t last_write = sd_access_fat_mtime(fullpath);
        char date_buf[20];
        format_entry_datetime(last_write, date_buf, sizeof(date_buf));
        if (entry.isDirectory()) {
          snprintf(s_entry_lines[scanned], sizeof(s_entry_lines[0]), "%s %-22s  <DIR>  %s", LV_SYMBOL_DIRECTORY,
                   short_name, date_buf);
        } else {
          snprintf(s_entry_lines[scanned], sizeof(s_entry_lines[0]), "%s %-22s  %8lu B  %s", LV_SYMBOL_FILE,
                   short_name, (unsigned long)entry.size(), date_buf);
        }
        entry.close();
        scanned++;
      }
    }
    if (dir) {
      dir.close();
    }
  });

  (void)lvgl_port_lock(-1);

  if (!dir_ok) {
    app_log_writef("WARN", "Falha ao abrir diretorio no SD: %s", s_current_path);
    if (strcmp(s_current_path, kFsRoot) == 0) {
      lv_label_set_text(s_path_label, "/sd (erro ao abrir)");
    } else {
      lv_label_set_text_fmt(s_path_label, "/sd%s (erro)", s_current_path);
    }
    if (show_loading_overlay) {
      ui_loading_hide();
    }
    return;
  }

  if (strcmp(s_current_path, kFsRoot) != 0) {
    lv_obj_t *up = lv_list_add_btn(s_list, LV_SYMBOL_UP, "..  <DIR>");
    lv_obj_add_event_cb(up, on_list_btn_clicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(kUserDataParentDir));
  }

  for (unsigned i = 0; i < scanned; i++) {
    lv_obj_t *btn = lv_list_add_btn(s_list, nullptr, s_entry_lines[i]);
    lv_obj_add_event_cb(btn, on_list_btn_clicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }
  s_entry_count = scanned;

  update_path_label();
  s_last_refresh_ms = millis();
  if (s_last_refresh_ms == 0) {
    s_last_refresh_ms = 1;
  }
  if (show_loading_overlay) {
    ui_loading_hide();
  }
}

void file_browser_refresh(void) {
  refresh_file_list(true);
}

void file_browser_refresh_silent(void) {
  refresh_file_list(false);
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

bool file_browser_init(lv_obj_t *parent) {
  if (parent == nullptr || !sd_access_is_mounted()) {
    return false;
  }

  if (s_root != nullptr) {
    lv_obj_del(s_root);
    s_root = nullptr;
    s_path_label = nullptr;
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

  s_path_label = lv_label_create(s_root);
  lv_label_set_long_mode(s_path_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(s_path_label, LV_PCT(100));

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
  return true;
}
