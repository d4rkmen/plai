/**
 * @file app_graphs.h
 * @author d4rkmen
 * @brief Graphs widget - battery and channel activity graphs
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
#include "line_graph.h"
#include "mesh/mesh_data.h"

#include "assets/app_graphs.h"

namespace MOONCAKE::APPS
{

    class AppGraphs : public APP_BASE
    {
    public:
        enum class GraphType
        {
            BATTERY,
            CHANNEL_ACTIVITY,
            MENU
        };

    private:
        struct
        {
            HAL::Hal* hal;
            GraphType current_graph;
            LineGraph graph;
            int menu_selection;
            uint32_t last_update_ms;
        } _data;

        void _render_menu();
        void _render_graph();
        void _handle_menu_input();
        void _handle_graph_input();
        void _update_graph_data();

    public:
        void onCreate() override;
        void onResume() override;
        void onRunning() override;
        void onDestroy() override;
    };

    class AppGraphs_Packer : public APP_PACKER_BASE
    {
        std::string getAppName() override { return "GRAPHS"; }
        std::string getAppDesc() override { return "Data visualization"; }
        void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_app_graphs, nullptr)); }
        void* newApp() override { return new AppGraphs; }
        void deleteApp(void* app) override { delete (AppGraphs*)app; }
    };

} // namespace MOONCAKE::APPS
