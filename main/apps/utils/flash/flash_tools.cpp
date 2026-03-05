/**
 * @file flash_tools.cpp
 * @brief Implementation of tools for flashing firmware to ESP32 devices
 * @version 0.1
 * @date 2024-06-19
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "flash_tools.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "spi_flash_mmap.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <vector>
#include <format>

// Filesystem headers
// #include "esp_spiffs.h"
// #include "esp_vfs_fat.h"

static const char* TAG = "FLASH_TOOLS";

// Buffer size for reading/writing firmware in chunks
#define FLASH_BUFFER_SIZE (4 * 1024) // 4KB chunks
#define ENCRYPTED_BLOCK_SIZE 16      // First 16 bytes are special (magic byte, etc.)

namespace UTILS
{
    namespace FLASH_TOOLS
    {
        std::string format_size(size_t current, size_t total)
        {
            return std::format("{} / {} KB", (size_t)(current / 1024), (size_t)(total / 1024));
        };

        // Helper function to check if a partition is bootable by verifying the magic byte
        bool is_partition_bootable(const esp_partition_t* partition)
        {
            if (!partition)
            {
                return false;
            }

            uint8_t buffer[ENCRYPTED_BLOCK_SIZE];
            if (esp_partition_read(partition, 0, buffer, ENCRYPTED_BLOCK_SIZE) != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to read partition header");
                return false;
            }
            // dump to log as hex
            // ESP_LOGI(TAG, "Partition header:");
            // for (int i = 0; i < ENCRYPTED_BLOCK_SIZE; i++)
            // {
            //     printf("%02x ", buffer[i]);
            //     if ((i + 1) % 16 == 0 || i == ENCRYPTED_BLOCK_SIZE - 1)
            //     {
            //         printf("\n");
            //     }
            // }

            // ESP image header magic byte is 0xE9
            return (buffer[0] == ESP_IMAGE_HEADER_MAGIC);
        }

        // Helper function to check if a memory block contains all 0xFF (erased flash)
        bool is_block_empty(const uint8_t* data, size_t len)
        {
            if (!len || len % sizeof(uint32_t))
                return false;

            const uint32_t* data32 = (const uint32_t*)data;
            size_t count = len / sizeof(uint32_t);

            for (size_t i = 0; i < count; i++)
            {
                if (data32[i] != 0xFFFFFFFF) // Not erased
                    return false;
            }
            return true;
        }

        FlashStatus flash_partition(const std::string& filepath,
                                    size_t offset,
                                    size_t size,
                                    esp_partition_info_t* pi,
                                    progress_callback_t progress_cb,
                                    void* arg_cb)
        {
            if (!pi)
            {
                ESP_LOGE(TAG, "Partition info is NULL");
                return FlashStatus::ERROR_PARTITION_NOT_FOUND;
            }
            // log all pi fields
            // ESP_LOGI(TAG, "Partition info:");
            // ESP_LOGI(TAG, "  type: %d", pi->type);
            // ESP_LOGI(TAG, "  subtype: %d", pi->subtype);
            // ESP_LOGI(TAG, "  address: 0x%lx", pi->pos.offset);
            // ESP_LOGI(TAG, "  size: 0x%lx", pi->pos.size);
            // file info
            // ESP_LOGI(TAG, "File info:");
            // ESP_LOGI(TAG, "  filepath: %s", filepath.c_str());
            // ESP_LOGI(TAG, "  offset: 0x%lx", (uint32_t)offset);
            // ESP_LOGI(TAG, "  size: 0x%lx", (uint32_t)size);

            // Get file size
            size_t file_size = 0;
            struct stat st;
            if (stat(filepath.c_str(), &st) != 0)
            {
                ESP_LOGE(TAG, "Failed to get file size %s", filepath.c_str());
                return FlashStatus::ERROR_FILE_NOT_FOUND;
            }
            file_size = st.st_size;
            size_t flash_size = std::min(size, file_size);
            FILE* f = fopen(filepath.c_str(), "rb");
            if (f == NULL)
            {
                ESP_LOGE(TAG, "Failed to open file %s", filepath.c_str());
                return FlashStatus::ERROR_FILE_NOT_FOUND;
            }

            // Find a suitable OTA partition for the update
            const esp_partition_t update_partition = {.flash_chip = esp_flash_default_chip,
                                                      .type = (esp_partition_type_t)pi->type,
                                                      .subtype = (esp_partition_subtype_t)pi->subtype,
                                                      .address = pi->pos.offset,
                                                      .size = pi->pos.size,
                                                      .erase_size = SPI_FLASH_SEC_SIZE,
                                                      .label = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                                      .encrypted = false,
                                                      .readonly = false};
            strlcpy((char*)&update_partition.label, (const char*)&pi->label, sizeof(update_partition.label));

            ESP_LOGI(TAG,
                     "Writing to partition: %s at offset 0x%02lx--%02lx",
                     update_partition.label,
                     update_partition.address,
                     update_partition.size);

            // Create a buffer for reading/writing
            uint8_t* buffer = (uint8_t*)malloc(FLASH_BUFFER_SIZE);
            if (buffer == NULL)
            {
                fclose(f);
                ESP_LOGE(TAG, "Failed to allocate buffer");
                return FlashStatus::ERROR_UNKNOWN;
            }

            // seek file to offset
            if (fseek(f, offset, SEEK_SET) != 0)
            {
                free(buffer);
                fclose(f);
                ESP_LOGE(TAG, "Failed to seek file to offset: 0x%zx", offset);
                return FlashStatus::ERROR_FILE_READ;
            }
            esp_err_t err;
            // Erase the entire partition
            if (progress_cb)
            {
                progress_cb(-1, "Erasing partition...", arg_cb);
            }
            ESP_LOGI(TAG, "Erasing partition...");
            err = esp_partition_erase_range(&update_partition, 0, update_partition.size);
            if (err != ESP_OK)
            {
                free(buffer);
                fclose(f);
                ESP_LOGE(TAG, "Failed to erase partition: %s", esp_err_to_name(err));
                return FlashStatus::ERROR_FLASH_WRITE;
            }

            ESP_LOGI(TAG, "Flashing partition...");

            // Save first 16 bytes (includes magic byte) to write at the end
            // This ensures that partially written firmware won't boot
            uint8_t first_block[ENCRYPTED_BLOCK_SIZE];
            size_t bytes_read = 0;
            size_t write_offset = 0;

            size_t step = std::min((size_t)FLASH_BUFFER_SIZE, flash_size);
            while (((bytes_read = fread(buffer, 1, step, f)) > 0) && (write_offset < flash_size))
            {
                if (write_offset == 0)
                {
                    // first 16 bytes (already read) for later writing
                    memcpy(first_block, buffer, ENCRYPTED_BLOCK_SIZE);
                    memset(buffer, 0xFF, ENCRYPTED_BLOCK_SIZE);
                }
                // ESP_LOGI(TAG, ">>%zx++%zx...", write_offset, bytes_read);
                // Only write if block is not empty (all 0xFF)
                if (!is_block_empty(buffer, bytes_read))
                {
                    err = esp_partition_write(&update_partition, write_offset, buffer, bytes_read);
                    if (err != ESP_OK)
                    {
                        free(buffer);
                        fclose(f);
                        ESP_LOGE(TAG, "Failed to write to flash: %s", esp_err_to_name(err));
                        return FlashStatus::ERROR_FLASH_WRITE;
                    }
                }
                write_offset += bytes_read;
                step = std::min((size_t)FLASH_BUFFER_SIZE, flash_size - write_offset);
                // Update progress
                if (progress_cb)
                {
                    progress_cb((write_offset * 100) / flash_size, format_size(write_offset, flash_size).c_str(), arg_cb);
                }
            }

            // Now write the first block with magic byte to make the partition bootable
            err = esp_partition_write(&update_partition, 0, first_block, ENCRYPTED_BLOCK_SIZE);
            if (err != ESP_OK)
            {
                free(buffer);
                fclose(f);
                ESP_LOGE(TAG, "Failed to write first block: %s", esp_err_to_name(err));
                return FlashStatus::ERROR_FLASH_WRITE;
            }

            free(buffer);
            fclose(f);

            ESP_LOGD(TAG, "Flash partition %s flashed successfully", (const char*)&update_partition.label);
            return FlashStatus::SUCCESS;
        }

        void reboot_device()
        {
            ESP_LOGW(TAG, "Rebooting device...");
            esp_restart();
        }

        const char* flash_status_to_string(FlashStatus status)
        {
            switch (status)
            {
            case FlashStatus::SUCCESS:
                return "Success";
            case FlashStatus::ERROR_FILE_NOT_FOUND:
                return "File not found";
            case FlashStatus::ERROR_FILE_READ:
                return "File read error";
            case FlashStatus::ERROR_MEMORY_ALLOCATION:
                return "Out of memory";
            case FlashStatus::ERROR_INVALID_FIRMWARE:
                return "Invalid firmware";
            case FlashStatus::ERROR_INVALID_CHIP_ID:
                return "Chip id mismatch";
            case FlashStatus::ERROR_INSUFFICIENT_SPACE:
                return "Not enough space";
            case FlashStatus::ERROR_FLASH_WRITE:
                return "Flash write error";
            case FlashStatus::ERROR_PARTITION_TABLE:
                return "Partition table error";
            case FlashStatus::ERROR_PARTITION_ADD:
                return "Partition add error";
            case FlashStatus::ERROR_PARTITION_NOT_FOUND:
                return "Partition not found";
            case FlashStatus::ERROR_FORMAT_FILESYSTEM:
                return "Format filesystem error";
            case FlashStatus::ERROR_UNKNOWN:
            default:
                return "Something goes wrong";
            }
        }
    } // namespace FLASH_TOOLS
} // namespace UTILS
