/**
 * @file app_channels.h
 * @author d4rkmen
 * @brief Channels widget - group chat messaging
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include "../apps.h"
#include <vector>
#include <string>

#include "apps/utils/theme/theme_define.h"
#include "apps/utils/anim/anim_define.h"
#include "apps/utils/icon/icon_define.h"
#include "apps/utils/anim/hl_text.h"
#include "apps/utils/anim/scroll_text.h"
#include "mesh/node_db.h"
#include "mesh/mesh_data.h"

#include "assets/app_channels.h"

namespace MOONCAKE::APPS
{

    class AppChannels : public APP_BASE
    {
    public:
        enum class ViewState
        {
            CHANNEL_LIST,
            CHANNEL_CHAT,
            CHANNEL_EDIT
        };

    private:
        struct
        {
            HAL::Hal* hal;
            ViewState view_state;

            // Channel list state
            std::vector<Mesh::ChannelInfo> channels;
            int selected_index;
            int scroll_offset;
            bool update_list;

            // Chat state (lazy rendering - only visible messages loaded)
            uint8_t selected_channel;
            std::string selected_channel_name;
            uint32_t chat_msg_count;
            std::vector<uint16_t> chat_line_counts;
            int chat_cur_line;
            int chat_total_lines;
            int chat_chars_per_line;
            bool chat_ctrl;

            // Edit state
            meshtastic_Channel edit_channel;
            Mesh::ChannelGreeting edit_greeting;
            uint8_t edit_channel_index;
            bool edit_is_new;
            int edit_selected;
            int edit_scroll;

            // Animation contexts
            UTILS::HL_TEXT::HLTextContext_t hint_hl_ctx;
            UTILS::SCROLL_TEXT::ScrollTextContext_t desc_scroll_ctx;

            // Change detection
            uint32_t last_msgstore_change;
        } _data;

        // Rendering
        bool _render_channel_list();
        bool _render_channel_chat();
        bool _render_channel_edit();
        bool _render_list_hint();
        bool _render_chat_hint();
        bool _render_edit_hint();
        bool _render_edit_desc();

        // Input handling
        void _handle_channel_list_input();
        void _handle_channel_chat_input();
        void _handle_channel_edit_input();

        // Helpers
        void _refresh_channels();
        void _refresh_messages();
        void _refresh_chat_line_counts();
        void _send_message(const std::string& text);
        void _enter_channel_edit(uint8_t channel_index, bool is_new);
        void _save_channel_edit();
        std::string _get_sender_name(uint32_t node_id);
        uint32_t _get_node_color(uint32_t node_id);
        uint32_t _get_node_text_color(uint32_t node_id);

    public:
        HAL::Hal* get_hal() const { return _data.hal; }
        Mesh::ChannelGreeting& getEditGreeting() { return _data.edit_greeting; }
        void onCreate() override;
        void onResume() override;
        void onRunning() override;
        void onDestroy() override;
    };

    class AppChannels_Packer : public APP_PACKER_BASE
    {
        std::string getAppName() override { return "CHANNELS"; }
        std::string getAppDesc() override { return "Group chat channels"; }
        void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_app_channels, nullptr)); }
        void* newApp() override { return new AppChannels; }
        void deleteApp(void* app) override { delete (AppChannels*)app; }
    };

} // namespace MOONCAKE::APPS
