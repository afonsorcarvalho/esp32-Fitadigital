#pragma once
#include <lvgl.h>

/* Primary brand — green */
#define UI_COLOR_PRIMARY         lv_color_hex(0x449D48)
#define UI_COLOR_PRIMARY_DARK    lv_color_hex(0x2A6B2E)
#define UI_COLOR_PRIMARY_LIGHT   lv_color_hex(0xE8F1E9)
#define UI_COLOR_PRIMARY_PRESSED lv_color_hex(0xC5DEC7)
#define UI_COLOR_PRIMARY_DARKER  lv_color_hex(0x2E7D32) /* pill OK / net monitor connected */

/* Blue — WireGuard / secondary action */
#define UI_COLOR_BLUE            lv_color_hex(0x1976D2)

/* Error / destructive */
#define UI_COLOR_ERROR           lv_color_hex(0xCC3333)
#define UI_COLOR_ERROR_BG        lv_color_hex(0xC62828)
#define UI_COLOR_ERROR_PRESSED   lv_color_hex(0xB71C1C)

/* Warning */
#define UI_COLOR_WARN_ARMED      lv_color_hex(0xE65100)
#define UI_COLOR_WARN_AMBER      lv_color_hex(0xF5B841)
#define UI_COLOR_COUNTDOWN       lv_color_hex(0xCC6600)

/* Surfaces */
#define UI_COLOR_SURFACE         lv_color_hex(0xFAFAFA)
#define UI_COLOR_WHITE           lv_color_hex(0xFFFFFF)
#define UI_COLOR_BLACK           lv_color_hex(0x000000)

/* Text / neutral */
#define UI_COLOR_TEXT_DARK       lv_color_hex(0x333333)
#define UI_COLOR_TEXT_MED        lv_color_hex(0x606060)
#define UI_COLOR_TEXT_MUTED      lv_color_hex(0x888888)
#define UI_COLOR_TEXT_SUBTLE     lv_color_hex(0x808080)
#define UI_COLOR_BORDER          lv_color_hex(0xCCCCCC)

/* Boot screen — terminal palette (only used in boot_screen.cpp) */
#define UI_COLOR_BOOT_BG         lv_color_hex(0x000000) /* same as BLACK */
#define UI_COLOR_BOOT_TITLE      lv_color_hex(0x00FF88)
#define UI_COLOR_BOOT_SUBTITLE   lv_color_hex(0x88AA99)
#define UI_COLOR_BOOT_STEP_OK    lv_color_hex(0x00CC66)
#define UI_COLOR_BOOT_STEP_ERROR lv_color_hex(0xFF6644)
#define UI_COLOR_BOOT_STEP_WARN  lv_color_hex(0xFFCC33)
#define UI_COLOR_BOOT_STEP_IDLE  lv_color_hex(0x669988)
#define UI_COLOR_BOOT_DOTS       lv_color_hex(0x508878)
#define UI_COLOR_BOOT_FOOTER     lv_color_hex(0x558877)

/* Text viewer — gutter (line-number column) */
#define UI_COLOR_VIEWER_GUTTER_BG lv_color_hex(0xEEEEEE) /* light gray, full-cell background */
