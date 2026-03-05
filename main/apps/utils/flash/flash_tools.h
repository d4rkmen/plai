/**
 * @file flash_tools.h
 * @brief Tools for flashing firmware to ESP32 devices
 * @version 0.1
 * @date 2024-06-19
 *
 * @copyright Copyright (c) 2024
 *
 */
#pragma once

#include <string>
#include <vector>
#include "esp_app_format.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "status.h"

#define TARGET_CHIP_ID ESP_CHIP_ID_ESP32S3

namespace UTILS
{
    namespace FLASH_TOOLS
    {
        /**
         * @brief Filesystem type for partitions
         */
        enum class FilesystemType
        {
            NONE = 0,
            SPIFFS,
            FATFS_SYS,
            FATFS_VFS,
            LFS
        };
        /**
         * @brief Format size to human readable string
         *
         * @param current Current size in bytes
         * @param total Total size in bytes
         * @return std::string Formatted size
         */
        std::string format_size(size_t current, size_t total);

        // /**
        //  * @brief Progress callback function type
        //  */
        // typedef void (*progress_callback_t)(int progress, const char* message, void* arg);

        /**
         * @brief Check if a partition is bootable
         *
         * @param partition Pointer to the partition to check
         * @return true if the partition is bootable, false otherwise
         */
        bool is_partition_bootable(const esp_partition_t* partition);

        /**
         * @brief Flash firmware file to device
         *
         * @param filepath Path to the firmware file
         * @param progress_cb Callback for progress updates
         * @param arg_cb Argument for progress callback
         * @return FlashStatus
         */
        FlashStatus flash_partition(const std::string& filepath,
                                    size_t offset,
                                    size_t size,
                                    esp_partition_info_t* pi,
                                    progress_callback_t progress_cb = nullptr,
                                    void* arg_cb = nullptr);

        /**
         * @brief Reboot the device
         */
        void reboot_device();

        /**
         * @brief Get string representation of flash status
         *
         * @param status Flash status enum value
         * @return const char* String representation
         */
        const char* flash_status_to_string(FlashStatus status);

    } // namespace FLASH_TOOLS
} // namespace UTILS
