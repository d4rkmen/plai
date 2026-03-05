/**
 * @file system_bar.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-09-18
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "../../launcher.h"
#include "../menu/menu_render_callback.hpp"
#include "apps/app_nodes/app_nodes.h"
#include "common_define.h"
#include "mesh/mesh_service.h"
#include "apps/utils/ui/draw_helper.h"

#include "assets/bat1.h"
#include "assets/bat2.h"
#include "assets/bat3.h"
#include "assets/bat4.h"
#if HAL_USE_WIFI
#include "assets/wifi1.h"
#include "assets/wifi2.h"
#include "assets/wifi3.h"
#include "assets/wifi4.h"
#include "assets/wifi5.h"
#include "assets/wifi6.h"
#endif
#include "assets/usb1.h"
#include "meshtastic/config.pb.h"

using namespace MOONCAKE::APPS;

static const char* getModemPresetShortName(meshtastic_Config_LoRaConfig_ModemPreset preset)
{
    switch (preset)
    {
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
        return "LongF";
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        return "LongS";
    case meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW:
        return "VLongS";
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        return "MedS";
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        return "MedF";
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        return "ShrtS";
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        return "ShrtF";
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        return "LongM";
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        return "ShrtT";
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO:
        return "LongT";
    default:
        return "?";
    }
}

#define PADDING_X 4


void Launcher::_start_system_bar()
{
    // _data.hal->canvas_system_bar()->fillScreen(TFT_BLUE);
}

void Launcher::_update_system_bar()
{
    bool system_bar_force_update = _data.system_bar_force_update_flag && *_data.system_bar_force_update_flag;
    if (((millis() - _data.system_bar_update_count) > _data.system_bar_update_preiod) || system_bar_force_update)
    {
        // Reset force update flag
        if (system_bar_force_update)
        {
            *_data.system_bar_force_update_flag = false;
        }

        // Update state
        _update_system_state();

        // Backgound
        _data.hal->canvas_system_bar()->fillScreen(THEME_COLOR_BG);
        int margin_x = 0;
        int margin_y = 0;
        _data.hal->canvas_system_bar()->fillRect(margin_x,
                                                 margin_y,
                                                 _data.hal->canvas_system_bar()->width() - margin_x * 2,
                                                 _data.hal->canvas_system_bar()->height() - margin_y * 2,
                                                 THEME_COLOR_SYSTEM_BAR);

        int x = 4;
        int y = 3;

        // Draw colored node identifier from mesh config
        if (_data.hal->mesh())
        {
            const auto& config = _data.hal->mesh()->getConfig();
            const char* short_name = config.short_name;

            // Use short_name if available, otherwise use last 4 hex digits of node_id
            std::string display_name;
            if (short_name[0] != '\0')
            {
                display_name = short_name;
            }
            else
            {
                display_name = std::format("{:04x}", config.node_id & 0xFFFF);
            }

            uint32_t nc = UTILS::UI::node_color(config.node_id);
            uint32_t ntc = UTILS::UI::node_text_color(config.node_id);

            int short_width = display_name.length() * 6 + 6;
            _data.hal->canvas_system_bar()->fillRoundRect(x, y + 1, short_width + 30 + PADDING_X, 14, 4, nc);
            _data.hal->canvas_system_bar()->setFont(FONT_12);
            _data.hal->canvas_system_bar()->setTextColor(ntc, nc);
            _data.hal->canvas_system_bar()->drawCenterString(display_name.c_str(), x + short_width / 2, y + 1);
            x += short_width + PADDING_X;

            // Mesh frequency and modem preset
            float freq = _data.hal->mesh()->getFrequency();

            _data.hal->canvas_system_bar()->setFont(FONT_6);

            // Top line: frequency
            if (freq > 0)
            {
                _data.hal->canvas_system_bar()->drawString(std::format("{:07.3f}", freq).c_str(), x, y + 2);
            }

            // Bottom line: preset short name (only when use_preset is on)
            if (config.lora_config.use_preset)
            {
                const char* preset_name = getModemPresetShortName(config.lora_config.modem_preset);
                _data.hal->canvas_system_bar()->drawCenterString(preset_name, x + 15, y + 8);
            }

            x += 30 + PADDING_X;

            // Channel utilization / Air utilization TX (2 lines, FONT_6)
            float ch_util = _data.hal->mesh()->getChannelUtilization();
            float air_util = _data.hal->mesh()->getAirUtilTx();
            _data.hal->canvas_system_bar()->setFont(FONT_6);
            _data.hal->canvas_system_bar()->setTextColor(THEME_COLOR_SYSTEM_BAR_TEXT, THEME_COLOR_SYSTEM_BAR);
            _data.hal->canvas_system_bar()->drawString(std::format("{:2.0f}%", ch_util).c_str(), x, y + 2);
            _data.hal->canvas_system_bar()->drawString(std::format("{:2.0f}%", air_util).c_str(), x, y + 9);
            x += 18 + PADDING_X;

            _data.hal->canvas_system_bar()->setFont(FONT_12);
        }
        // USB
        #if HAL_USE_USB
        bool usb_connected = _data.hal->usb()->is_connected();
        if (usb_connected)
        {
            _data.hal->canvas_system_bar()->pushImage(x, y, 26, 16, image_data_usb1, THEME_COLOR_ICON_16);
            x += 26 + PADDING_X;
        }
        #endif
        _data.hal->canvas_system_bar()->setFont(FONT_16);
        // Time
        bool show_time = _data.hal->settings()->getBool("system", "show_time");
        if (show_time)
        {
            if (_data.hal->isGPSAdjusted())
            {
                // show gps icon before the text
                _data.hal->canvas_system_bar()->pushImage(_data.hal->canvas_system_bar()->width() / 2 - 8 - 33,
                                                          y + 2,
                                                          12,
                                                          12,
                                                          image_data_pos_external,
                                                          TFT_WHITE);
            }
            _data.hal->canvas_system_bar()->setTextColor(THEME_COLOR_SYSTEM_BAR_TEXT);
            _data.hal->canvas_system_bar()->drawCenterString(_data.system_state.time.c_str(),
                                                             _data.hal->canvas_system_bar()->width() / 2 - 8,
                                                             _data.hal->canvas_system_bar()->height() / 2 - FONT_HEIGHT / 2);
        }
        // total new messages badge (DM+CHANNELS)
        uint32_t total_new_messages = Mesh::MeshDataStore::getInstance().getTotalUnreadDMCount() +
                                      Mesh::MeshDataStore::getInstance().getTotalUnreadChannelCount();
        if (total_new_messages > 0)
        {
            std::string total_new_messages_str = total_new_messages < 100 ? std::format("{}", total_new_messages) : "99+";
            int total_new_messages_width = _data.hal->canvas_system_bar()->textWidth(total_new_messages_str.c_str());
            _data.hal->canvas_system_bar()->setFont(FONT_12);
            x = _data.hal->canvas_system_bar()->width() / 2 + 20;
            _data.hal->canvas_system_bar()->fillRoundRect(x, y + 1, total_new_messages_width + 6, 14, 4, TFT_RED);
            // _data.hal->canvas_system_bar()->setFont(FONT_6);
            _data.hal->canvas_system_bar()->setTextColor(TFT_WHITE, TFT_RED);
            _data.hal->canvas_system_bar()->drawCenterString(total_new_messages_str.c_str(),
                                                             x + total_new_messages_width / 2 + 3,
                                                             y + 1);
            _data.hal->canvas_system_bar()->setFont(FONT_16);
        }
        // Bat shit, comes last
        x = _data.hal->canvas_system_bar()->width() - 36;

        // Voltage
        bool show_voltage = _data.hal->settings()->getBool("system", "show_bat_volt");
        if (show_voltage)
        {
            _data.hal->canvas_system_bar()->setTextColor(TFT_BLACK);
            _data.hal->canvas_system_bar()->drawRightString(std::format("{:.1f}V", _data.system_state.voltage).c_str(),
                                                            x - 4,
                                                            _data.hal->canvas_system_bar()->height() / 2 - FONT_HEIGHT / 2);
        }

        const uint16_t* images_bat[] = {image_data_bat1, image_data_bat2, image_data_bat3, image_data_bat4};
        uint16_t* image_bat = nullptr;
        if (_data.system_state.bat_state > 0 && _data.system_state.bat_state <= 4)
        {
            image_bat = (uint16_t*)images_bat[_data.system_state.bat_state - 1];
        }
        else
        {
            image_bat = (uint16_t*)images_bat[0];
        }
        _data.hal->canvas_system_bar()->pushImage(x, y, 32, 16, image_bat, THEME_COLOR_ICON_16);

        // Push
        _data.hal->canvas_system_bar_update();

        // Reset flag
        _data.system_bar_update_count = millis();
    }
}
