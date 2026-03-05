/**
 * @file app_monitor.h
 * @author d4rkmen
 * @brief Monitor widget - live radio packet feed with detail view
 * @version 2.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include "../apps.h"
#include <string>

#include "apps/utils/theme/theme_define.h"
#include "apps/utils/anim/anim_define.h"
#include "apps/utils/icon/icon_define.h"
#include "apps/utils/anim/hl_text.h"
#include "mesh/mesh_data.h"

#include "assets/app_monitor.h"

namespace MOONCAKE::APPS
{

    class AppMonitor : public APP_BASE
    {
    public:
        enum class ViewState
        {
            PACKET_LIST,
            PACKET_DETAIL
        };

    private:
        struct
        {
            HAL::Hal* hal;
            ViewState view_state;

            // List state
            int selected_index;
            int scroll_offset;
            uint32_t last_log_size;
            uint32_t focused_pkt_id;
            bool focused_at_bottom;
            bool update_list;

            // Detail state
            Mesh::PacketLogEntry detail_pkt;
            int detail_scroll;

            // Animation
            UTILS::HL_TEXT::HLTextContext_t hint_hl_ctx;
        } _data;

        // Rendering
        bool _render_packet_list();
        bool _render_packet_detail();
        bool _render_list_hint();
        bool _render_detail_hint();

        // Input
        void _handle_list_input();
        void _handle_detail_input();

        // Helpers
        const char* _port_name(uint8_t port);
        const char* _direction_str(const Mesh::PacketLogEntry& pkt);
        uint32_t _direction_color(const Mesh::PacketLogEntry& pkt);

    public:
        void onCreate() override;
        void onResume() override;
        void onRunning() override;
        void onDestroy() override;
    };

    class AppMonitor_Packer : public APP_PACKER_BASE
    {
        std::string getAppName() override { return "MONITOR"; }
        std::string getAppDesc() override { return "Radio packet monitor"; }
        void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_app_monitor, nullptr)); }
        void* newApp() override { return new AppMonitor; }
        void deleteApp(void* app) override { delete (AppMonitor*)app; }
    };

} // namespace MOONCAKE::APPS
