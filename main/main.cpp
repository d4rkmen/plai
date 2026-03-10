/**
 * @file cardputer.cpp
 * @author d4rkmen
 * @brief Meshtastic client application for M5Stack CardPuter
 * @version 2.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */
#include <stdio.h>
#include "mooncake.h"
#include "hal/hal_cardputer.h"
#include "settings/settings.h"
#include "apps/apps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "apps/utils/flash/flash_tools.h"

static const char* TAG = "MAIN";

using namespace HAL;
using namespace SETTINGS;
using namespace MOONCAKE;

Settings settings;
HalCardputer hal(&settings);
Mooncake mooncake;

void _data_base_setup_callback(SIMPLEKV::SimpleKV& db)
{
    db.Add<HAL::Hal*>("HAL", &hal);
    db.Add<SETTINGS::Settings*>("SETTINGS", &settings);
}

extern "C" void app_main(void)
{
    // Settings init
    settings.init();
    settings.setHal(&hal);
    Settings::applyTimezone(settings.getString("system", "timezone"));

    // Init hal (includes radio and mesh service initialization)
    hal.init();

    // Init framework
    mooncake.setDatabaseSetupCallback(_data_base_setup_callback);
    mooncake.init();

    // Install launcher
    auto launcher = new APPS::Launcher_Packer;
    mooncake.installApp(launcher);

    // Install apps - Settings first
    mooncake.installApp(new APPS::AppSettings_Packer);
    // mooncake.installApp(new APPS::AppDevices_Packer);

    // Install Meshtastic widgets
    mooncake.installApp(new APPS::AppNodes_Packer);
    mooncake.installApp(new APPS::AppChannels_Packer);
    mooncake.installApp(new APPS::AppMonitor_Packer);
#if 0
    mooncake.installApp(new APPS::AppStats_Packer);
    mooncake.installApp(new APPS::AppGraphs_Packer);
#endif

    // Mount SD card if available
    if (hal.sdcard())
    {
        hal.sdcard()->mount(false);
        if (hal.sdcard()->is_mounted())
        {
            // Start mesh service (LoRa radio)
            if (!hal.startMesh())
            {
                ESP_LOGW(TAG, "Mesh service failed to start, continuing without radio");
            }
            else
            {
                ESP_LOGI(TAG, "Meshtastic client ready");
            }
        }
    }
    else
    {
        ESP_LOGW(TAG, "SD card not found, continuing without radio");
    }
    // Create launcher
    mooncake.createApp(launcher);

    // Main loop
    while (1)
    {

        // Update mesh service (process BLE/radio events)
        hal.updateMesh();
        if (hal.isDisplaySleeping())
        {
            hal.keyboard()->updateKeyList();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        else
        {
            // Update UI framework
            mooncake.update();
            vTaskDelay(1);
        }
    }
}
