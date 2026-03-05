#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>
#include "esp_partition.h"
#include "esp_flash_partitions.h"
#include "spi_flash_mmap.h"
#include "status.h"

// Flash size definition (8MB)
#define ESP_FLASH_SIZE (8 * 1024 * 1024) // 8MB flash size
// // hardcoded offset for ota data partition in partition table
#define FLASH_SECTOR_SIZE 0x1000
#define FLASH_BLOCK_SIZE 0x10000
// #define OTA_DATA_OFFSET (0xE000)

// Partition table entry magic number
#define ESP_BOOTLOADER_MAGIC_WORD1 0xFFFFFFFF
#define ESP_BOOTLOADER_MAGIC_WORD2 0x00000050

namespace UTILS
{
    namespace FLASH_TOOLS
    {
        esp_err_t bootloader_flash_read(size_t src, void* dest, size_t size, bool allow_decrypt);
        esp_err_t bootloader_flash_write(size_t dest_addr, void* src, size_t size, bool write_encrypted);
        esp_err_t bootloader_flash_erase_sector(size_t sector);

        extern int s_flash_usage_percent;
        /**
         * @brief Set the boot partition
         *
         * @param pi Partition info
         * @return FlashStatus
         */
        FlashStatus set_boot_partition(const esp_partition_info_t* pi);

        class PartitionTable
        {
        public:
            PartitionTable();
            ~PartitionTable();

            // Load the partition table from flash
            bool load();

            // Save the partition table to flash
            bool save();

            // Load the partition table from a firmware file
            FlashStatus loadFromFile(const std::string& filename);

            // List all partitions in the table
            std::vector<esp_partition_info_t> listPartitions() const;

            // Find a partition by name
            esp_partition_info_t* findPartitionByName(const std::string& name);

            // Add a new partition, automatically calculating offset if needed
            esp_partition_info_t* addPartition(
                uint8_t type, uint8_t subtype, const std::string& name, uint32_t offset, uint32_t size, uint32_t flags = 0);

            // Delete a partition by name and recalculate offsets for subsequent partitions
            bool deletePartition(uint8_t index, progress_callback_t progress_cb, void* arg_cb);

            // Move partition data
            bool movePartitionData(
                uint32_t src_offset, uint32_t size, uint32_t dst_offset, progress_callback_t progress_cb, void* arg_cb);

            // Get count of partitions
            size_t getCount() const;

            // Get the next available OTA subtype
            uint8_t getNextOTA();

            // Get the free space in bytes for a given partition type
            size_t getFreeSpace(uint8_t type);

            // Create and set default partition table
            bool makeDefaultPartitions();

            // Check if the current partition table is valid
            bool isValid() const;

            // Get a human-readable string representation of the partition table
            std::string toString(bool verbose = false) const;

            // Format a size in bytes to a human-readable string
            static std::string formatSize(uint64_t size);

            // Helper functions for partition type/subtype string conversion
            static std::string getTypeString(uint8_t type);
            static std::string getSubtypeString(uint8_t type, uint8_t subtype);

            // Get a partition by index
            esp_partition_info_t* getPartition(size_t index) const;

            // Get current flash usage info
            static int getFlashUsagePercent() { return s_flash_usage_percent; }

            // Initialize flash usage info
            static void initFlashUsagePercent();

        private:
            std::vector<esp_partition_info_t> m_partitions;

            // Read the partition table from flash
            bool readFromFlash();

            // Write the partition table to flash
            bool writeToFlash();

            // Recalculate partition offsets ensuring no overlaps
            bool recalculateOffsets(size_t startIndex = 0);

            // Find a partition by offset
            esp_partition_info_t* findPartitionByOffset(uint32_t offset);

            // Find the next available offset after all existing partitions
            uint32_t findNextAvailableOffset() const;

            // Update flash usage info
            void updateFlashUsageInfo();
        };

    } // namespace FLASH_TOOLS
} // namespace UTILS
