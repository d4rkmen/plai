/**
 * @file launcher.h
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-09-18
 *
 * @copyright Copyright (c) 2023
 *
 */
#pragma once
#include "hal/hal.h"
#include <mooncake.h>
#include "app/app.h"
#include "../utils/smooth_menu/src/smooth_menu.h"
#include <stdint.h>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

namespace MOONCAKE
{
    namespace APPS
    {
        /**
         * @brief Launcher
         *
         */
        class Launcher : public APP_BASE
        {
        private:
            struct SystemState_t
            {
                // 1 ~ 5
                #if HAL_USE_WIFI
                HAL::wifi_status_t wifi_status = HAL::WIFI_STATUS_IDLE;
                #endif
                // 1 ~ 4
                uint8_t bat_state = 1;
                std::string time = "12:34";
                float voltage = 4.15;
                uint8_t bat_level = 100;
            };

            struct Data_t
            {
                HAL::Hal* hal = nullptr;
                bool* system_bar_force_update_flag = nullptr;
                // View
                LGFX_Sprite* progress_bar = nullptr;
                // Menu
                uint32_t menu_update_preiod = 10;
                uint32_t menu_update_count = 0;
                SMOOTH_MENU::Simple_Menu* menu = nullptr;
                SMOOTH_MENU::SimpleMenuCallback_t* menu_render_cb = nullptr;
                APP_BASE* _opened_app = nullptr;

                // System bar
                uint32_t system_bar_update_preiod = 1000;
                uint32_t system_bar_update_count = 0;
                SystemState_t system_state;

                // USB
                bool usb_connected = false;
            };
            Data_t _data;

            void _boot_anim();

            void _start_menu();
            void _update_menu();

            void _start_system_bar();
            void _update_system_bar();

            // Platform port
            void _wait_enter();
            void _boot_message(const std::string& message);
            void _init_progress_bar();
            void _delete_progress_bar();
            void _render_countdown_progress(int percent);
            void _render_wait_progress();
            bool _check_next_pressed();
            bool _check_last_pressed();
            bool _check_enter_pressed();
            bool _check_info_pressed();
            void _stop_repeat();
            void _update_keyboard_state();
            void _update_system_state();

        public:
            void onCreate() override;
            void onRunning() override;
            void onRunningBG() override;
            void onResume() override;
        };

        class Launcher_Packer : public APP_PACKER_BASE
        {
            std::string getAppName() override { return "Launcher"; }
            void* newApp() override { return new Launcher; }
            void deleteApp(void* app) override { delete (Launcher*)app; }
        };
    } // namespace APPS
} // namespace MOONCAKE
