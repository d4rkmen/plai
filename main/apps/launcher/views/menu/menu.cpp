/**
 * @file menu.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-09-18
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "apps/launcher/launcher.h"
#include "mc_conf_internal.h"
#include "esp_log.h"
#include "menu_render_callback.hpp"
#include <string>
#include <vector>
#include "common_define.h"
#include "apps/utils/ui/dialog.h"

static const char* TAG = "APP_LAUNCHER";

// Define the global shared menu instance
SMOOTH_MENU::Simple_Menu* g_shared_menu = nullptr;

using namespace MOONCAKE::APPS;

void Launcher::_start_menu()
{
    /* Create a menu to handle selector */
    _data.menu = new SMOOTH_MENU::Simple_Menu;
    g_shared_menu = _data.menu;
    _data.menu_render_cb = new LauncherRender_CB_t(_data.hal);

    _data.menu->init(_data.hal->canvas()->width(), _data.hal->canvas()->height());
    _data.menu->setRenderCallback(_data.menu_render_cb);

    /* Set selector anim */
    auto cfg_selector = _data.menu->getSelector()->config();
    cfg_selector.animPath_x = LVGL::ease_out;
    cfg_selector.animPath_y = LVGL::ease_out;
    cfg_selector.animTime_x = 400;
    cfg_selector.animTime_y = 400;
    _data.menu->getSelector()->config(cfg_selector);

    /* Set menu looply */
    _data.menu->setMenuLoopMode(true);
    // Get install app list
    ESP_LOGD(TAG, "installed apps num: %d", mcAppGetFramework()->getAppRegister().getInstalledAppNum());
    int i = 0;
    for (const auto& app : mcAppGetFramework()->getAppRegister().getInstalledAppList())
    {
        // Skip launcher itself
        if (app->getAddr() == getAppPacker())
            continue;

        // Push items
        _data.menu->getMenu()->addItem(app->getAppName(),
                                       ICON_GAP + i * (ICON_WIDTH + ICON_GAP),
                                       ICON_MARGIN_TOP,
                                       ICON_WIDTH,
                                       ICON_WIDTH,
                                       app->getAppIcon());
        i++;
    }
}

void Launcher::_update_menu()
{
    uint32_t now = millis();
    if ((now - _data.menu_update_count) > _data.menu_update_preiod)
    {
        // _data.hal->keyboard()->updateKeyList();
        // _data.hal->keyboard()->updateKeysState();
        // Navigate
        if (_data.hal->keyboard()->isPressed())
        {
            if (_check_last_pressed())
            {
                _data.menu_render_cb->resetScroll();
                _data.menu->goLast();
            }
            else if (_check_next_pressed())
            {
                _data.menu_render_cb->resetScroll();
                _data.menu->goNext();
            }
            // If pressed enter
            if (_check_enter_pressed())
            {
                auto selected_item = _data.menu->getSelector()->getTargetItem();
                selected_item++;
                // Create app
                _data._opened_app = mcAppGetFramework()->createApp(mcAppGetFramework()->getInstalledAppList()[selected_item]);
                // Start app
                mcAppGetFramework()->startApp(_data._opened_app);
                // Stack launcher into background
                closeApp();
            }
            // If pressed info
            if (_check_info_pressed())
            {
                auto selected_item = _data.menu->getSelector()->getTargetItem();
                selected_item++;

                // Create app
                auto app = mcAppGetFramework()->getInstalledAppList()[selected_item];
                std::string name = app->getAppName();
                std::string desc = app->getAppDesc();
                ESP_LOGD(TAG, "app: %s desc: %s", name.c_str(), desc.c_str());
                UTILS::UI::show_message_dialog(_data.hal, name.c_str(), desc.c_str(), 0);
            }
        }
        else
        {
            _stop_repeat();
        }

        // Update menu
        _data.menu->update(now);

        _data.hal->canvas_update();

        // Reset flag
        _data.menu_update_count = now;
    }
}
