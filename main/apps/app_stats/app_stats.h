/**
 * @file app_stats.h
 * @author d4rkmen
 * @brief Statistics widget - network and radio metrics
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
#include "mesh/mesh_data.h"

#include "assets/app_stats.h"

namespace MOONCAKE::APPS
{

    class AppStats : public APP_BASE
    {
    private:
        struct
        {
            HAL::Hal* hal;
            int scroll_offset;
            uint32_t last_update_ms;
        } _data;

        void _render_stats();
        void _handle_input();
        std::string _format_uptime(uint32_t ms);
        std::string _format_bytes(uint32_t bytes);

    public:
        void onCreate() override;
        void onResume() override;
        void onRunning() override;
        void onDestroy() override;
    };

    class AppStats_Packer : public APP_PACKER_BASE
    {
        std::string getAppName() override { return "STATS"; }
        std::string getAppDesc() override { return "Network statistics"; }
        void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_app_stats, nullptr)); }
        void* newApp() override { return new AppStats; }
        void deleteApp(void* app) override { delete (AppStats*)app; }
    };

} // namespace MOONCAKE::APPS
