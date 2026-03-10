/**
 * @file boot_anim.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-09-18
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "apps/launcher/launcher.h"
#include "lgfx/v1/misc/enum.hpp"
#include "esp_log.h"
#include "apps/utils/theme/theme_define.h"
#include "common_define.h"

using namespace MOONCAKE::APPS;

extern const uint8_t boot_logo_start[] asm("_binary_boot_logo_png_start");
extern const uint8_t boot_logo_end[] asm("_binary_boot_logo_png_end");
extern const uint8_t boot_sound_wav_start[] asm("_binary_boot_sound_wav_start");
extern const uint8_t boot_sound_wav_end[] asm("_binary_boot_sound_wav_end");

void Launcher::_boot_anim()
{
    // Show logo
    _data.hal->display()->clear();
    _data.hal->display()->drawPng(boot_logo_start, boot_logo_end - boot_logo_start);
    // set brightness
    _data.hal->display()->setBrightness(_data.hal->settings()->getNumber("system", "brightness"));
    bool has_boot_sound = _data.hal->settings()->getBool("system", "boot_sound");
    if (has_boot_sound)
        _data.hal->speaker()->playWav(boot_sound_wav_start, boot_sound_wav_end - boot_sound_wav_start);
    // Show version
    const int32_t pos_x = _data.hal->display()->width() - 4;
    const int32_t pos_y = _data.hal->display()->height() / 2;
    _data.hal->display()->setFont(FONT_12);
    _data.hal->display()->setTextColor(TFT_DARKGREY, TFT_BLACK);
    _data.hal->display()->drawRightString(_data.hal->type().c_str(), pos_x, pos_y);
    _data.hal->display()->setFont(FONT_16);
    _data.hal->display()->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _data.hal->display()->drawRightString("Plai v" BUILD_NUMBER, pos_x, pos_y + 14);
    // waiting for user to release keys
    _data.hal->display()->setFont(FONT_16);
    _data.hal->display()->setTextSize(1);
    _data.hal->display()->setTextColor(TFT_LIGHTGREY);
    // not works on ADV
    _data.hal->keyboard()->updateKeyList();
    if (_data.hal->keyboard()->isPressed())
    {
        _data.hal->playErrorSound();
        while (_data.hal->keyboard()->isPressed())
        {
            delay(100);
            _data.hal->keyboard()->updateKeyList();
        }
    }
    else
    {
        delay(2000);
    }

    return;
}
