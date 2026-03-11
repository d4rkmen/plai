/**
 * @file theme_define.h
 * @author Forairaaaaa
 * @brief
 * @version 0.6
 * @date 2023-09-20
 *
 * @copyright Copyright (c) 2023
 *
 */
#pragma once

// #include "ascii_fonts.hpp"
#include "unicode_fonts.hpp"

#define THEME_COLOR_BG (uint32_t)(0x333333)
#define THEME_COLOR_BG_DARK (uint32_t)(0x191919)
#define THEME_COLOR_SYSTEM_BAR (uint32_t)(0xD3D3D3) //(0x99FF00)
#define THEME_COLOR_SYSTEM_BAR_TEXT TFT_BLACK
#define THEME_COLOR_ICON (uint32_t)(0xE6E6E6)
#define THEME_COLOR_ICON_16 (uint16_t)(0x3CE7)
#define THEME_COLOR_BG_SELECTED (uint32_t)(0xD3D3D3)
#define THEME_COLOR_BG_SELECTED_DARK (uint32_t)(0xB2B2B2)
#define THEME_COLOR_SELECTED (uint32_t)(0x000000)
#define THEME_COLOR_UNSELECTED (uint32_t)(0xFFFFFF)
#define THEME_COLOR_TRANSPARENT (uint32_t)(0x4CFF00)
#define THEME_COLOR_SIGNAL_NONE (uint32_t)(0xFC0025)
#define THEME_COLOR_SIGNAL_BAD (uint32_t)(0xFF6A00)
#define THEME_COLOR_SIGNAL_FAIR (uint32_t)(0xFFDD00)
#define THEME_COLOR_SIGNAL_GOOD (uint32_t)(0x09B51A)
#define THEME_COLOR_SIGNAL_TEXT (uint32_t)(0xB5B5B5)
#define THEME_COLOR_FAVORITE (uint32_t)(0xFFC700)
#define THEME_COLOR_IGNORED (uint32_t)(0xFF0000)
#define THEME_COLOR_BATTERY_LOW (uint32_t)(0xFF1D00)
#define THEME_COLOR_BATTERY_PWR (uint32_t)(0x00DDFF)
#define THEME_COLOR_KEYBOARD_LOWER (uint32_t)(0xA0A0A0)
#define THEME_COLOR_KEYBOARD_UPPER (uint32_t)(0x0074FF)
#define THEME_COLOR_KEYBOARD_FN (uint32_t)(0xFF6A00)
#define THEME_COLOR_CHANNEL_HASH (uint32_t)(0x00FFFF)
#define ICON_WIDTH 48
#define ICON_GAP 20
#define ICON_MARGIN_TOP 20
#define ICON_TAG_MARGIN_TOP 5
#define ICON_SELECTED_WIDTH 64
#define FONT_HEIGHT 16
#define FONT_WIDTH 8

#define THEME_COLOR_KB_CAPS_LOCK TFT_SKYBLUE
#define THEME_COLOR_KB_ALT TFT_YELLOW
#define THEME_COLOR_KB_CTRL TFT_PINK
#define THEME_COLOR_KB_FN TFT_ORANGE
#define THEME_COLOR_KB_OPT TFT_DARKGREEN

#define THEME_COLOR_REPL_TEXT TFT_WHITE
#define FONT_8 &Font0
#define FONT_6 &TomThumb
#define FONT_10 &efont_unicode_10
#define FONT_12 &efont_unicode_12
#define FONT_16 &efont_unicode_16
