#pragma once
#include <lvgl.h>

/* Primary brand — green */
#define UI_COLOR_PRIMARY         lv_color_hex(0x449D48)
#define UI_COLOR_PRIMARY_DARK    lv_color_hex(0x2A6B2E)
#define UI_COLOR_PRIMARY_LIGHT   lv_color_hex(0xE8F1E9)
#define UI_COLOR_PRIMARY_PRESSED lv_color_hex(0xC5DEC7)

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
