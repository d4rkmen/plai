/**
 * @file sdcard.cpp
 * @author Anderson Antunes
 * @brief
 * @version 0.1
 * @date 2024-01-14
 *
 * @copyright Copyright (c) 2024
 *
 */
#include <string.h>
#include <format>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sdcard.h"

#define PIN_NUM_MISO 39
#define PIN_NUM_MOSI 14
#define PIN_NUM_CLK 40
#define PIN_NUM_CS GPIO_NUM_12

static const char* MOUNT_POINT = "/sdcard";
static const char* TAG = "SDCARD";

bool SDCard::mount(bool format_if_mount_failed)
{
    if (_is_mounted)
    {
        ESP_LOGI(TAG, "SD card already mounted");
        return true;
    }
    ESP_LOGI(TAG, "Mounting SD card");

    spi_bus_config_t bus_cfg;
    bus_cfg.mosi_io_num = PIN_NUM_MOSI;
    bus_cfg.miso_io_num = PIN_NUM_MISO;
    bus_cfg.sclk_io_num = PIN_NUM_CLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.data4_io_num = -1, bus_cfg.data5_io_num = -1, bus_cfg.data6_io_num = -1, bus_cfg.data7_io_num = -1,
    bus_cfg.max_transfer_sz = 4092;
    bus_cfg.flags = (SPICOMMON_BUSFLAG_SCLK | SPICOMMON_BUSFLAG_MOSI);
    bus_cfg.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO;
    bus_cfg.intr_flags = 0;
    esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return false;
    }
    if (ret == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGD(TAG, "SPI bus already initialized (likely by radio or display), slot=%d", host.slot);
    }
    else
    {
        ESP_LOGD(TAG, "SPI bus initialized for SD card, slot=%d", host.slot);
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = (spi_host_device_t)host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {.format_if_mount_failed = format_if_mount_failed,
                                                     .max_files = 5,
                                                     .allocation_unit_size = 16 * 1024,
                                                     .disk_status_check_enable = false,
                                                     .use_one_fat = false};

    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK)
    {
        card = nullptr;
        spi_bus_free((spi_host_device_t)host.slot);
        ESP_LOGE(TAG, "Failed to mount filesystem");
        return false;
    };

    sdmmc_card_print_info(stdout, card);
    _is_mounted = true;

    return true;
}

bool SDCard::eject()
{
    if (!_is_mounted)
    {
        ESP_LOGI(TAG, "SD card not mounted");
        return true;
    }
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to unmount SD card");
        return false;
    }
    card = nullptr;
    spi_bus_free((spi_host_device_t)host.slot);
    _is_mounted = false;

    return true;
}

bool SDCard::is_mounted() { return _is_mounted; }

char* SDCard::get_mount_point() { return (char*)MOUNT_POINT; }

std::string SDCard::get_manufacturer()
{
    if (!_is_mounted || card == nullptr)
    {
        return "";
    }
    return std::format("0x{:02x}", card->cid.mfg_id);
}

std::string SDCard::get_device_name()
{
    if (!_is_mounted || card == nullptr)
    {
        return "";
    }
    return std::string((const char*)&card->cid.name);
}

uint64_t SDCard::get_capacity()
{
    if (!_is_mounted || card == nullptr)
    {
        return 0;
    }
    return ((uint64_t)card->csd.capacity) * card->csd.sector_size;
}