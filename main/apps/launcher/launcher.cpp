/**
 * @file launcher.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-09-18
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "launcher.h"
#include "hal/hal.h"
#include "mc_conf_internal.h"
#include "esp_log.h"
#include "apps/utils/theme/theme_define.h"
#include "apps/utils/anim/anim_define.h"
#include "common_define.h"
#include "apps/utils/flash/flash_tools.h"
#include "apps/utils/ui/dialog.h"
#include "apps/utils/screenshot/screenshot_tools.h"
#include "apps/utils/ui/key_repeat.h"
#include <sys/time.h>
#include "esp_partition.h"
#if HAL_USE_WIFI
#include "wifi/wifi.h"
#endif
#include <format>
#include <ctime>

static const char* TAG = "APP_LAUNCHER";

#define BAT_UPDATE_INTERVAL 30000

static bool is_repeat = false;
static uint32_t next_fire_ts = 0;

using namespace MOONCAKE::APPS;

void Launcher::onCreate()
{
    // Get hal
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
    _data.system_bar_force_update_flag = mcAppGetDatabase()->Get("SYSTEM_BAR_FORCE_UPDATE")->value<bool*>();
    // settings
    _data.hal->display()->setBrightness(_data.hal->settings()->getNumber("system", "brightness"));
    _data.hal->speaker()->setVolume(_data.hal->settings()->getNumber("system", "volume"));
    _data.hal->keyboard()->setDimmed(false);
    _data.hal->keyboard()->set_dim_time(_data.hal->settings()->getNumber("system", "dim_time") * 1000);

    // React to dimmed state changes (e.g. restore brightness when undimmed)
    _data.hal->keyboard()->setDimmedCallback(
        [this](bool dimmed)
        {
            if (!dimmed)
            {
                ESP_LOGD(TAG, "Screen on");
                _data.hal->display()->setBrightness(_data.hal->settings()->getNumber("system", "brightness"));
            }
            else
            {
                ESP_LOGD(TAG, "Screen off");
                // slowly dim the screen
            }
        });

// Initialize WiFi module
#if HAL_USE_WIFI
    if (_data.hal->wifi()->init())
    {
        // Connect to WiFi if enabled
        if (_data.hal->settings()->getBool("wifi", "enabled"))
        {
            _data.hal->wifi()->connect();
        }
    }
#endif
    // Init
    _boot_anim();
    // check radio present
    if (!_data.hal->radio())
    {
        UTILS::UI::show_error_dialog(_data.hal, "Error", "LoRa radio not found", "REBOOT");
        _data.hal->reboot();
        return;
    }
    // check mesh profile
    if (!_data.hal->sdcard() || !_data.hal->sdcard()->is_mounted())
    {
        // show error dialog
        UTILS::UI::show_error_dialog(_data.hal, "Error", "SD card is not found", "REBOOT");
        // reboot
        _data.hal->reboot();
        return;
    }
    _start_menu();
    _start_system_bar();
    // Allow background running
    setAllowBgRunning(true);
    // Auto start
    startApp();
}
// onResume
void Launcher::onResume() { _stop_repeat(); }

// onRunning
void Launcher::onRunning()
{
    _update_menu();
    _update_system_bar();
    _update_keyboard_state();
}

void Launcher::onRunningBG()
{
    // If only launcher standing still
    if (mcAppGetFramework()->getAppManager().getCreatedAppNum() == 1)
    {
        // Close anim
        ANIM_APP_CLOSE();

        // Back to business
        mcAppGetFramework()->startApp(this);
    }

    _update_system_bar();
    _update_keyboard_state();
}

void Launcher::_init_progress_bar()
{
    _data.progress_bar = new LGFX_Sprite(_data.hal->display());
    _data.progress_bar->createSprite(_data.hal->display()->width(), 16);
}

void Launcher::_delete_progress_bar()
{
    _data.progress_bar->deleteSprite();
    delete _data.progress_bar;
    _data.progress_bar = nullptr;
}

void Launcher::_render_countdown_progress(int percent)
{
    // Progress bar dimensions - full width at bottom of screen
    int bar_h = _data.progress_bar->height();               // Height of the progress bar
    int bar_w = _data.progress_bar->width();                // Full width
    int bar_y = _data.hal->display()->height() - bar_h - 1; // Position at bottom

    // Draw progress bar frame
    _data.progress_bar->drawRect(0, 0, bar_w, bar_h, THEME_COLOR_BG_SELECTED);

    // Calculate fill width based on percentage
    int fill_width = (percent * bar_w) / 100;
    if (fill_width > 0)
    {
        _data.progress_bar->fillRect(0, 1, fill_width, bar_h - 2, THEME_COLOR_BG_SELECTED);
    }

    _data.progress_bar->pushSprite(_data.hal->display(), 0, bar_y);
}

void Launcher::_render_wait_progress()
{
    // Progress bar dimensions - full width at bottom of screen
    int bar_h = _data.progress_bar->height();               // Height of the progress bar
    int bar_w = _data.progress_bar->width();                // Full width
    int bar_y = _data.hal->display()->height() - bar_h - 1; // Position at bottom

    // Draw progress bar frame
    _data.progress_bar->fillRect(0, 0, bar_w, bar_h, TFT_BLACK);
    _data.progress_bar->drawRect(0, 0, bar_w, bar_h, THEME_COLOR_BG_SELECTED);

    // Create a pattern for the pending animation
    static int pattern_offset = 0;
    pattern_offset = (pattern_offset + 1) % bar_h;

    // Draw the pattern (diagonal stripes)
    for (int x = -bar_h; x < bar_w; x += bar_h)
    {
        for (int w = 0; w < 4; w++)
            _data.progress_bar->drawLine(x + pattern_offset + w,
                                         1,
                                         x + pattern_offset + w + bar_h - 2,
                                         1 + bar_h - 2,
                                         THEME_COLOR_BG_SELECTED);
    }

    _data.progress_bar->pushSprite(_data.hal->display(), 0, bar_y);
}

void Launcher::_boot_message(const std::string& message)
{
    _data.hal->display()->drawCenterString(message.c_str(),
                                           _data.hal->display()->width() / 2,
                                           _data.hal->display()->height() - 32 - 2);
}

void Launcher::_wait_enter()
{
    // init display
}

void Launcher::_stop_repeat()
{
    is_repeat = false;
    next_fire_ts = 0;
}

bool Launcher::_check_next_pressed()
{
    bool pressed = _data.hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT) || _data.hal->keyboard()->isKeyPressing(KEY_NUM_DOWN);
    if (pressed)
    {
        uint32_t now = millis();
        if (key_repeat_check(is_repeat, next_fire_ts, now))
        {
            _data.hal->playNextSound();
            return true;
        }
    }
    return false;
}

bool Launcher::_check_last_pressed()
{
    bool pressed = _data.hal->keyboard()->isKeyPressing(KEY_NUM_LEFT) || _data.hal->keyboard()->isKeyPressing(KEY_NUM_UP);
    if (pressed)
    {
        uint32_t now = millis();
        if (key_repeat_check(is_repeat, next_fire_ts, now))
        {
            _data.hal->playNextSound();
            return true;
        }
    }
    return false;
}

bool Launcher::_check_info_pressed()
{
    if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_I))
    {
        // Hold till release
        while (_data.hal->keyboard()->isKeyPressing(KEY_NUM_I))
        {
            _data.menu->update(millis());
            _data.hal->canvas_update();
            _data.hal->keyboard()->updateKeyList();
        }
        return true;
    }

    return false;
}

bool Launcher::_check_enter_pressed()
{
    if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
    {
        _data.hal->playLastSound();
        // Hold till release
        while (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
        {
            _data.menu->update(millis());
            _data.hal->canvas_update();
            _data.hal->keyboard()->updateKeyList();
        }

        return true;
    }

    return false;
}

void Launcher::_update_keyboard_state()
{
    // Update key list (keyboard owns dim timeout logic internally)
    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    // Dimming slowly
    uint32_t now = millis();
    static uint32_t last_dim_step_time = 0;
    if (now - last_dim_step_time > 50)
    {
        last_dim_step_time = now;
        auto brightness = _data.hal->display()->getBrightness();
        if (_data.hal->keyboard()->isDimmed() && brightness > 0)
        {
            uint8_t dim_step = 5;
            _data.hal->display()->setBrightness(brightness > dim_step ? brightness - dim_step : 0);
        }
    }
    // Check for screenshot key combination: CTRL + SPACE
    UTILS::SCREENSHOT_TOOLS::check_and_handle_screenshot(_data.hal, _data.system_bar_force_update_flag);
}

uint32_t _bat_update_time_count = 0;

void Launcher::_update_system_state()
{
    // Refresh dim_time from settings when changed
    _data.hal->keyboard()->set_dim_time(_data.hal->settings()->getNumber("system", "dim_time") * 1000);

    // brightness
    int32_t brightness = _data.hal->display()->getBrightness();
    int32_t new_brightness = _data.hal->settings()->getNumber("system", "brightness");
    if (!_data.hal->keyboard()->isDimmed() && brightness != new_brightness)
    {
        _data.hal->display()->setBrightness(new_brightness);
    }
    // volume
    int32_t volume = _data.hal->speaker()->getVolume();
    int32_t new_volume = _data.hal->settings()->getNumber("system", "volume");
    if (volume != new_volume)
    {
        _data.hal->speaker()->setVolume(new_volume);
    }
// USB host
#if HAL_USE_USB
    bool usb_host_enabled = _data.hal->usb()->is_initialized();
    bool new_usb_host_enabled = _data.hal->settings()->getBool("system", "usb_host");
    if (usb_host_enabled != new_usb_host_enabled)
    {
        if (new_usb_host_enabled)
        {
            _data.hal->usb()->init();
        }
        else
        {
            _data.hal->usb()->deinit();
        }
    }
#endif
    // Apply timezone from settings (once, or when changed)
    static std::string last_tz;
    std::string tz = _data.hal->settings()->getString("system", "timezone");
    if (tz != last_tz)
    {
        // Lookup table: human-readable label -> POSIX TZ string
        // POSIX sign convention is inverted: UTC+2 = "<GMT+2>-2"
        static const struct
        {
            const char* label;
            const char* posix;
        } tz_table[] = {
            {"GMT-12", "<GMT-12>12"},        {"GMT-11", "<GMT-11>11"},        {"GMT-10", "<GMT-10>10"},
            {"GMT-9:30", "<GMT-9:30>9:30"},  {"GMT-9", "<GMT-9>9"},           {"GMT-8", "<GMT-8>8"},
            {"GMT-7", "<GMT-7>7"},           {"GMT-6", "<GMT-6>6"},           {"GMT-5", "<GMT-5>5"},
            {"GMT-4", "<GMT-4>4"},           {"GMT-3:30", "<GMT-3:30>3:30"},  {"GMT-3", "<GMT-3>3"},
            {"GMT-2", "<GMT-2>2"},           {"GMT-1", "<GMT-1>1"},           {"GMT+0", "GMT0"},
            {"GMT+1", "<GMT+1>-1"},          {"GMT+2", "<GMT+2>-2"},          {"GMT+3", "<GMT+3>-3"},
            {"GMT+3:30", "<GMT+3:30>-3:30"}, {"GMT+4", "<GMT+4>-4"},          {"GMT+4:30", "<GMT+4:30>-4:30"},
            {"GMT+5", "<GMT+5>-5"},          {"GMT+5:30", "<GMT+5:30>-5:30"}, {"GMT+5:45", "<GMT+5:45>-5:45"},
            {"GMT+6", "<GMT+6>-6"},          {"GMT+6:30", "<GMT+6:30>-6:30"}, {"GMT+7", "<GMT+7>-7"},
            {"GMT+8", "<GMT+8>-8"},          {"GMT+8:45", "<GMT+8:45>-8:45"}, {"GMT+9", "<GMT+9>-9"},
            {"GMT+9:30", "<GMT+9:30>-9:30"}, {"GMT+10", "<GMT+10>-10"},       {"GMT+10:30", "<GMT+10:30>-10:30"},
            {"GMT+11", "<GMT+11>-11"},       {"GMT+12", "<GMT+12>-12"},       {"GMT+13", "<GMT+13>-13"},
            {"GMT+14", "<GMT+14>-14"},
        };

        const char* posix_tz = "GMT0"; // fallback to UTC
        for (const auto& entry : tz_table)
        {
            if (tz == entry.label)
            {
                posix_tz = entry.posix;
                break;
            }
        }

        setenv("TZ", posix_tz, 1);
        tzset();
        last_tz = tz;
        ESP_LOGW(TAG, "Timezone set: %s -> %s", tz.c_str(), posix_tz);
        // current dattetime is
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        ESP_LOGW(TAG, "Current date and time: %s", asctime(&timeinfo));
    }

    // Sync system time from GPS when available (initial + refresh every hour)
    static uint32_t last_gps_sync_ms = 0;
    constexpr uint32_t GPS_SYNC_INTERVAL_MS = 3600000; // 1 hour
    bool gps_needs_sync =
        !_data.hal->isGPSAdjusted() || (last_gps_sync_ms > 0 && (millis() - last_gps_sync_ms) >= GPS_SYNC_INTERVAL_MS);
    if (gps_needs_sync && _data.hal->gps() && _data.hal->gps()->hasFix())
    {
        auto gps_data = _data.hal->gps()->getData();
        if (gps_data.year >= 2026 && gps_data.time > 0)
        {
            struct timeval tv = {.tv_sec = (time_t)gps_data.time, .tv_usec = 0};
            settimeofday(&tv, nullptr);
            if (!_data.hal->isGPSAdjusted())
            {
                _data.hal->playNotificationSound(HAL::Hal::NotificationSound::GPS);
                _data.hal->setGPSAdjusted(true);
            }
            last_gps_sync_ms = millis();
            ESP_LOGW(TAG, "System time set from GPS: %lu", (unsigned long)gps_data.time);
        }
    }

    // Time display
    // if (_data.hal->isGPSAdjusted())
    {
        static time_t now;
        static struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        _data.system_state.time = std::format("{:02d}:{:02d}", timeinfo.tm_hour, timeinfo.tm_min);
    }
    // else
    // {
    //     // Fake time (uptime-based)
    //     _data.system_state.time =
    //         std::format("{:02d}:{:02d}", (int)((millis() / 3600000) % 60), (int)((millis() / 60000) % 60));
    // }

    // Bat shit
    if ((millis() - _bat_update_time_count) > BAT_UPDATE_INTERVAL || _bat_update_time_count == 0)
    {
        _data.system_state.voltage = _data.hal->getBatVoltage();
        _data.system_state.bat_level = _data.hal->getBatLevel(_data.system_state.voltage);
        if (_data.system_state.bat_level >= 75)
            _data.system_state.bat_state = 1;
        else if (_data.system_state.bat_level >= 50)
            _data.system_state.bat_state = 2;
        else if (_data.system_state.bat_level >= 25)
            _data.system_state.bat_state = 3;
        else
            _data.system_state.bat_state = 4;

        _bat_update_time_count = millis();
    }
#if HAL_USE_WIFI
    // wifi
    _data.system_state.wifi_status = _data.hal->wifi()->get_status();
#endif
}
