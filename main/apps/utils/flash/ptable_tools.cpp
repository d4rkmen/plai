#include "ptable_tools.h"
#include <sstream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include <sys/stat.h>
#ifdef CONFIG_PARTITION_TABLE_MD5
#include "esp_rom_md5.h"
#endif
#include "esp_rom_crc.h"

static const char* TAG = "PTABLE_TOOLS";

namespace UTILS
{
    namespace FLASH_TOOLS
    {
        // Static flash usage info
        int s_flash_usage_percent = -1;
        /************************************************************************
         * PartitionTable implementation
         ************************************************************************/

        PartitionTable::PartitionTable() {}

        PartitionTable::~PartitionTable() {}

        bool PartitionTable::load()
        {
            bool result = readFromFlash();
            if (result)
            {
                updateFlashUsageInfo();
            }
            else
            {
                s_flash_usage_percent = -1;
            }
            return result;
        }

        FlashStatus PartitionTable::loadFromFile(const std::string& filename)
        {
            m_partitions.clear();
            // Get file size
            size_t file_size = 0;
            struct stat st;
            if (stat(filename.c_str(), &st) != 0)
            {
                ESP_LOGE(TAG, "Failed to get file size %s", filename.c_str());
                return FlashStatus::ERROR_FILE_NOT_FOUND;
            }
            file_size = st.st_size;

            FILE* f = fopen(filename.c_str(), "rb");
            if (!f)
            {
                ESP_LOGE(TAG, "Failed to open file %s", filename.c_str());
                return FlashStatus::ERROR_FILE_READ;
            }
            // app name is the filename without the path and extension
            std::string app_name = filename.substr(filename.find_last_of("/") + 1);
            app_name = app_name.substr(0, app_name.find_last_of("."));
            if (app_name.length() > 15)
            {
                app_name = app_name.substr(0, 14) + ">";
            }
            // Read image header
            esp_image_header_t header;
            if (fread(&header, sizeof(header), 1, f) != 1)
            {
                ESP_LOGE(TAG, "Failed to read image header %s", filename.c_str());
                fclose(f);
                return FlashStatus::ERROR_FILE_READ;
            }
            ESP_LOGD(TAG,
                     "segment_count: %d, entry_addr: 0x%lX, chip_id: 0x%X, min_chip_rev: 0x%X (0x%X..0x%X)",
                     header.segment_count,
                     header.entry_addr,
                     header.chip_id,
                     header.min_chip_rev,
                     header.min_chip_rev_full,
                     header.max_chip_rev_full);
            ESP_LOGD(TAG,
                     "flash_size: 0x%X, flash_mode: 0x%X, flash_speed: 0x%X",
                     header.spi_size,
                     header.spi_mode,
                     header.spi_speed);

            if (header.chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID)
            {
                fclose(f);
                ESP_LOGE(TAG,
                         "Chip ID mismatch in file %s (expected 0x%X, got 0x%X)",
                         filename.c_str(),
                         CONFIG_IDF_FIRMWARE_CHIP_ID,
                         header.chip_id);
                return FlashStatus::ERROR_INVALID_CHIP_ID;
            }

            // Skip first segment header
            if (fseek(f, sizeof(esp_image_segment_header_t), SEEK_CUR) != 0)
            {
                ESP_LOGE(TAG, "Failed to seek to app desc in %s", filename.c_str());
                fclose(f);
                return FlashStatus::ERROR_FILE_READ;
            }

            // Read app description
            esp_app_desc_t app_desc;
            if (fread(&app_desc, sizeof(app_desc), 1, f) != 1)
            {
                ESP_LOGE(TAG, "Failed to read app description from %s", filename.c_str());
                fclose(f);
                return FlashStatus::ERROR_FILE_READ;
            }

            if (app_desc.magic_word == ESP_APP_DESC_MAGIC_WORD)
            {
                ESP_LOGD(TAG, "This is an application image, no partition table");
                fclose(f);
                // create app partition from app description
                esp_partition_info_t* pi = addPartition(ESP_PARTITION_TYPE_APP, getNextOTA(), app_name.c_str(), 0, file_size);
                if (pi == nullptr)
                {
                    ESP_LOGE(TAG, "Failed to add app partition");
                    return FlashStatus::ERROR_PARTITION_ADD;
                }
                return FlashStatus::SUCCESS;
            }
            else if (app_desc.magic_word == ESP_BOOTLOADER_MAGIC_WORD1 || app_desc.magic_word == ESP_BOOTLOADER_MAGIC_WORD2)
            {
                ESP_LOGD(TAG, "This is a bootloader image, seeking to partition table");

                // Seek to partition table offset
                if (fseek(f, ESP_PARTITION_TABLE_OFFSET, SEEK_SET) != 0)
                {
                    ESP_LOGE(TAG, "Failed to seek to partition table %s", filename.c_str());
                    fclose(f);
                    return FlashStatus::ERROR_FILE_READ;
                }

                // Read partition table
                uint8_t* buffer = new uint8_t[ESP_PARTITION_TABLE_MAX_LEN];
                if (!buffer)
                {
                    ESP_LOGE(TAG, "Failed to allocate memory for partition table buffer %s", filename.c_str());
                    fclose(f);
                    return FlashStatus::ERROR_MEMORY_ALLOCATION;
                }

                if (fread(buffer, ESP_PARTITION_TABLE_MAX_LEN, 1, f) != 1)
                {
                    ESP_LOGE(TAG, "Failed to read partition table %s", filename.c_str());
                    delete[] buffer;
                    fclose(f);
                    return FlashStatus::ERROR_FILE_READ;
                }

                // Parse partition table entries
                const esp_partition_info_t* partitions = reinterpret_cast<const esp_partition_info_t*>(buffer);

                // Check magic number of first entry
                if (partitions[0].magic != ESP_PARTITION_MAGIC)
                {
                    ESP_LOGE(TAG, "Invalid partition table magic: 0x%x %s", partitions[0].magic, filename.c_str());
                    delete[] buffer;
                    fclose(f);
                    return FlashStatus::ERROR_PARTITION_TABLE;
                }

                // Read each partition entry
                for (int i = 0; i < ESP_PARTITION_TABLE_MAX_ENTRIES; i++)
                {
                    const esp_partition_info_t* part = &partitions[i];

                    // Check if this is a valid entry
                    if (part->magic != ESP_PARTITION_MAGIC)
                    {
                        break;
                    }

                    // Add to our partition vector
                    m_partitions.push_back(*part);

                    // Check if this is an end marker
                    if (part->type == PART_TYPE_END)
                    {
                        break;
                    }
                }

                delete[] buffer;
                fclose(f);

                ESP_LOGD(TAG, "Successfully read %lu partitions from file %s", m_partitions.size(), filename.c_str());
                return FlashStatus::SUCCESS;
            }
            ESP_LOGE(TAG, "Unknown image type 0x%lX in file %s", app_desc.magic_word, filename.c_str());
            fclose(f);
            return FlashStatus::ERROR_INVALID_FIRMWARE;
        }

        bool PartitionTable::save()
        {
            bool result = writeToFlash();
            if (result)
            {
                updateFlashUsageInfo();
            }
            return result;
        }

        std::vector<esp_partition_info_t> PartitionTable::listPartitions() const { return m_partitions; }

        esp_partition_info_t* PartitionTable::findPartitionByName(const std::string& name)
        {
            for (auto& partition : m_partitions)
            {
                // Convert uint8_t array to std::string for comparison
                std::string partName((const char*)partition.label);
                if (name == partName)
                {
                    return &partition;
                }
            }
            return nullptr;
        }

        esp_partition_info_t* PartitionTable::addPartition(
            uint8_t type, uint8_t subtype, const std::string& name, uint32_t offset, uint32_t size, uint32_t flags)
        {
            // log name and type
            ESP_LOGD(TAG, "Adding partition: %s, type: %d", name.c_str(), type);

            // Check if name is valid (not too long)
            if (name.length() > 15)
            {
                ESP_LOGE(TAG, "Partition name '%s' is too long (max 15 chars)", name.c_str());
                return nullptr;
            }

            // Determine alignment based on partition type
            uint32_t alignment = (type == PART_TYPE_APP) ? 0x10000 : 0x1000;

            // If offset is 0, calculate the next available offset
            if (offset == 0)
            {
                offset = findNextAvailableOffset();
                // Ensure alignment based on partition type
                if (offset % alignment != 0)
                {
                    offset = ((offset + alignment - 1) / alignment) * alignment;
                }
            }
            else
            {
                // Check if the offset is already used
                if (findPartitionByOffset(offset) != nullptr)
                {
                    ESP_LOGE(TAG, "Offset 0x%lx is already used by another partition", offset);
                    return nullptr;
                }

                // Check if offset is properly aligned
                if (offset % alignment != 0)
                {
                    ESP_LOGE(TAG,
                             "Partition '%s' offset 0x%lx is not %s aligned",
                             name.c_str(),
                             offset,
                             (type == PART_TYPE_APP) ? "64KB (0x10000)" : "4KB (0x1000)");
                    return nullptr;
                }
            }
            // align size too
            if (size % alignment != 0)
            {
                size = ((size + alignment - 1) / alignment) * alignment;
            }
            // Check if partition would exceed flash size
            if (offset + size > ESP_FLASH_SIZE)
            {
                ESP_LOGE(TAG,
                         "Partition '%s' would exceed flash size (offset: 0x%lx, size: 0x%lx, flash size: 0x%x)",
                         name.c_str(),
                         offset,
                         size,
                         ESP_FLASH_SIZE);
                return nullptr;
            }

            // Create new partition
            esp_partition_info_t newPartition = {};
            newPartition.magic = ESP_PARTITION_MAGIC;
            newPartition.type = type;
            newPartition.subtype = subtype;
            newPartition.pos.offset = offset;
            newPartition.pos.size = size;
            // Properly copy string to uint8_t array
            memset(newPartition.label, 0, sizeof(newPartition.label));
            strncpy((char*)newPartition.label, name.c_str(), sizeof(newPartition.label) - 1);
            newPartition.flags = flags;

            // Add to the list
            m_partitions.push_back(newPartition);

            // Sort partitions by offset
            std::sort(m_partitions.begin(),
                      m_partitions.end(),
                      [](const esp_partition_info_t& a, const esp_partition_info_t& b) { return a.pos.offset < b.pos.offset; });
            return getPartition(m_partitions.size() - 1);
        }

        bool PartitionTable::deletePartition(uint8_t index, progress_callback_t progress_cb, void* arg_cb)
        {
            if (index >= m_partitions.size())
            {
                ESP_LOGE(TAG, "Partition index %d out of range", index);
                return false;
            }

            // Store partition info before removing
            esp_partition_info_t& part = m_partitions[index];
            uint32_t deleted_offset = index == 0 ? CONFIG_PARTITION_TABLE_OFFSET + 0x1000
                                                 : m_partitions[index - 1].pos.offset + m_partitions[index - 1].pos.size;
            uint8_t deleted_type = part.type;
            uint8_t deleted_subtype = part.subtype;

            // Remove the partition from the list
            m_partitions.erase(m_partitions.begin() + index);

            // If this is not the last partition, move subsequent partitions up
            if (index < m_partitions.size())
            {
                for (size_t i = index; i < m_partitions.size(); i++)
                {
                    esp_partition_info_t& part = m_partitions[i];
                    uint32_t old_offset = part.pos.offset;

                    // Calculate new aligned offset
                    uint32_t alignment = (part.type == PART_TYPE_APP) ? 0x10000 : 0x1000;
                    uint32_t new_offset = ((deleted_offset + alignment - 1) & ~(alignment - 1));

                    // Move partition data
                    if (!movePartitionData(old_offset, part.pos.size, new_offset, progress_cb, arg_cb))
                    {
                        ESP_LOGE(TAG, "Failed to move partition data for '%s'", (char*)part.label);
                        return false;
                    }

                    // Update partition offset
                    part.pos.offset = new_offset;
                    // for ota partitions, increment subtype to keep it in order
                    if (deleted_type == PART_TYPE_APP && deleted_subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
                        deleted_subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX)
                    {
                        part.subtype = deleted_subtype;
                        deleted_subtype++;
                    }
                    deleted_offset = new_offset + part.pos.size;
                }
            }

            updateFlashUsageInfo();
            return true;
        }

        bool PartitionTable::makeDefaultPartitions()
        {
            // Clear current partitions
            m_partitions.clear();

            // Define default partitions in a const table
            const struct
            {
                uint8_t type;
                uint8_t subtype;
                const char* name;
                uint32_t offset;
                uint32_t size;
            } default_partitions[] = {
                {ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_PHY, "phy_init", 0x9000, 0x1000},
                {ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "apps_nvs", 0xA000, 0x4000},
                {ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, "apps_ota", 0xE000, 0x2000},
                {ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "apps_app", 0x10000, 0x190000}};

            // Add all partitions from the table
            for (const auto& part : default_partitions)
            {
                if (!addPartition(part.type, part.subtype, part.name, part.offset, part.size))
                {
                    ESP_LOGE(TAG, "Failed to add %s partition", part.name);
                    return false;
                }
            }
            if (!recalculateOffsets())
            {
                ESP_LOGE(TAG, "Failed to recalculate offsets");
                return false;
            }
            ESP_LOGD(TAG, "Created default partition table with %lu partitions", m_partitions.size());
            return true;
        }

        size_t PartitionTable::getCount() const { return m_partitions.size(); }

        std::string PartitionTable::toString(bool verbose) const
        {
#if 0
            // debug omly
            std::stringstream ss;

            ss << "Partition Table (8MB Flash):" << std::endl;
            ss << "# Name        Type    Subtype   Offset     Size       Flags" << std::endl;
            ss << "-------------------------------------------------------------" << std::endl;

            int i = 0;
            for (const auto& p : m_partitions)
            {
                ss << std::setw(2) << i++ << " ";
                // Convert uint8_t array to std::string for output
                std::string partName((const char*)p.label);
                ss << std::left << std::setw(12) << partName << " ";
                ss << std::left << std::setw(8) << getTypeString(p.type) << " ";
                ss << std::left << std::setw(10) << getSubtypeString(p.type, p.subtype) << " ";
                ss << "0x" << std::hex << std::setw(8) << p.pos.offset << " ";
                ss << std::dec << std::setfill(' ') << std::setw(10) << formatSize(p.pos.size) << " ";
                ss << "0x" << std::hex << p.flags << std::dec << std::endl;

                if (verbose)
                {
                    ss << "    Magic: 0x" << std::hex << p.magic << std::dec << std::endl;

                    // Check and show alignment info
                    uint32_t requiredAlignment = (p.type == PART_TYPE_APP) ? 0x10000 : 0x1000;
                    if (p.pos.offset % requiredAlignment != 0)
                    {
                        ss << "    WARNING: Offset not " << ((p.type == PART_TYPE_APP) ? "64KB (0x10000)" : "4KB (0x1000)")
                           << " aligned!" << std::endl;
                    }
                }
            }

            // Display total usage
            uint32_t totalSize = 0;
            for (const auto& p : m_partitions)
            {
                totalSize += p.pos.size;
            }

            double usagePercent = (static_cast<double>(totalSize) / ESP_FLASH_SIZE) * 100.0;
            ss << "\nTotal flash usage: " << formatSize(totalSize) << " of " << formatSize(ESP_FLASH_SIZE) << " (" << std::fixed
               << std::setprecision(1) << usagePercent << "%)" << std::endl;

            return ss.str();
#else
            return "";
#endif
        }

        std::string PartitionTable::formatSize(uint64_t size)
        {
            const char* units[] = {"B", "KB", "MB", "GB"};
            int unit = 0;
            float size_d = size;

            while (size_d >= 1024.0 && unit < 3)
            {
                size_d /= 1024.0;
                unit++;
            }

            return std::format("{:.1f}{}", size_d, units[unit]);
            // return std::format("{}{}", (uint32_t)size_d, units[unit]);
        }

        std::string PartitionTable::getTypeString(uint8_t type)
        {
            switch (type)
            {
            case PART_TYPE_APP:
                return "app";
            case PART_TYPE_DATA:
                return "data";
            case PART_TYPE_END:
                return "end";
            default:
                return "unknown";
            }
        }

        std::string PartitionTable::getSubtypeString(uint8_t type, uint8_t subtype)
        {
            if (type == PART_TYPE_APP)
            {
                switch (subtype)
                {
                case ESP_PARTITION_SUBTYPE_APP_FACTORY:
                    return "factory";
                case ESP_PARTITION_SUBTYPE_APP_OTA_0:
                case ESP_PARTITION_SUBTYPE_APP_OTA_1:
                case ESP_PARTITION_SUBTYPE_APP_OTA_2:
                case ESP_PARTITION_SUBTYPE_APP_OTA_3:
                case ESP_PARTITION_SUBTYPE_APP_OTA_4:
                case ESP_PARTITION_SUBTYPE_APP_OTA_5:
                case ESP_PARTITION_SUBTYPE_APP_OTA_6:
                case ESP_PARTITION_SUBTYPE_APP_OTA_7:
                case ESP_PARTITION_SUBTYPE_APP_OTA_8:
                case ESP_PARTITION_SUBTYPE_APP_OTA_9:
                case ESP_PARTITION_SUBTYPE_APP_OTA_10:
                case ESP_PARTITION_SUBTYPE_APP_OTA_11:
                case ESP_PARTITION_SUBTYPE_APP_OTA_12:
                case ESP_PARTITION_SUBTYPE_APP_OTA_13:
                case ESP_PARTITION_SUBTYPE_APP_OTA_14:
                case ESP_PARTITION_SUBTYPE_APP_OTA_15:
                    return std::format("ota_{}", subtype & 0x0F);
                case ESP_PARTITION_SUBTYPE_APP_TEST:
                    return "test";
                default:
                    return "unknown";
                }
            }
            else if (type == ESP_PARTITION_TYPE_DATA)
            {
                switch (subtype)
                {
                case ESP_PARTITION_SUBTYPE_DATA_OTA:
                    return "ota";
                case ESP_PARTITION_SUBTYPE_DATA_PHY:
                    return "phy";
                case ESP_PARTITION_SUBTYPE_DATA_NVS:
                    return "nvs";
                case ESP_PARTITION_SUBTYPE_DATA_COREDUMP:
                    return "coredump";
                case ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS:
                    return "nvs_keys";
                case ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM:
                    return "efuse";
                case ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD:
                    return "esphttpd";
                case ESP_PARTITION_SUBTYPE_DATA_FAT:
                    return "fat";
                case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
                    return "spiffs";
                default:
                    return "unknown";
                }
            }
            else if (type == PART_TYPE_END)
            {
                return "end";
            }
            return "unknown";
        }

        esp_err_t bootloader_flash_read(size_t src, void* dest, size_t size, bool allow_decrypt)
        {
            return esp_flash_read(NULL, dest, src, size);
        }

        esp_err_t bootloader_flash_write(size_t dest_addr, void* src, size_t size, bool write_encrypted)
        {
            return esp_flash_write(NULL, src, dest_addr, size);
        }

        esp_err_t bootloader_flash_erase_sector(size_t sector)
        {
            // Will de-dependency IDF-5025
            return esp_flash_erase_region(NULL, sector * SPI_FLASH_SEC_SIZE, SPI_FLASH_SEC_SIZE);
        }

        bool PartitionTable::readFromFlash()
        {
            m_partitions.clear();

            // Allocate buffer for the partition table
            uint8_t* buffer = new uint8_t[ESP_PARTITION_TABLE_MAX_LEN];
            if (!buffer)
            {
                ESP_LOGE(TAG, "Failed to allocate memory for partition table buffer");
                return false;
            }

            // Read partition table from flash
            esp_err_t err = bootloader_flash_read(ESP_PARTITION_TABLE_OFFSET, buffer, ESP_PARTITION_TABLE_MAX_LEN, false);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to read partition table from flash: %s", esp_err_to_name(err));
                delete[] buffer;
                return false;
            }

            // Parse partition table entries
            const esp_partition_info_t* partitions = reinterpret_cast<const esp_partition_info_t*>(buffer);

            // Check magic number of first entry
            if (partitions[0].magic != ESP_PARTITION_MAGIC)
            {
                ESP_LOGE(TAG, "Invalid partition table magic: 0x%x", partitions[0].magic);
                delete[] buffer;
                return false;
            }

            // Read each partition entry
            for (int i = 0; i < ESP_PARTITION_TABLE_MAX_ENTRIES; i++)
            {
                const esp_partition_info_t* part = &partitions[i];

                // Check if this is a valid entry
                if (part->magic != ESP_PARTITION_MAGIC)
                {
                    break;
                }

                // Verify that the partition fits within flash size
                if (part->pos.offset + part->pos.size > ESP_FLASH_SIZE)
                {
                    ESP_LOGW(TAG,
                             "Partition exceeds flash size - may be corrupted: offset=0x%lx, size=0x%lx",
                             part->pos.offset,
                             part->pos.size);
                }

                // Check alignment based on partition type
                uint32_t requiredAlignment = (part->type == PART_TYPE_APP) ? 0x10000 : 0x1000;
                if (part->pos.offset % requiredAlignment != 0)
                {
                    ESP_LOGW(TAG,
                             "Partition at offset 0x%lx is not properly aligned for type %s (required: 0x%lx)",
                             part->pos.offset,
                             getTypeString(part->type).c_str(),
                             requiredAlignment);
                }

                // Add to our partition vector
                m_partitions.push_back(*part);

                // Check if this is an end marker
                if (part->type == PART_TYPE_END)
                {
                    break;
                }
            }

            delete[] buffer;

            ESP_LOGD(TAG, "Read %lu partitions from flash (8MB flash size)", m_partitions.size());
            return true;
        }

        bool PartitionTable::writeToFlash()
        {
            if (m_partitions.empty())
            {
                ESP_LOGE(TAG, "No partitions to write");
                return false;
            }

            // Sort partitions by offset
            std::sort(m_partitions.begin(),
                      m_partitions.end(),
                      [](const esp_partition_info_t& a, const esp_partition_info_t& b) { return a.pos.offset < b.pos.offset; });

            // Allocate buffer for the partition table
            const size_t entry_size = sizeof(esp_partition_info_t);
            const size_t table_size = m_partitions.size() * entry_size;

            if (table_size > ESP_PARTITION_TABLE_MAX_LEN)
            {
                ESP_LOGE(TAG, "Partition table too large: %lu bytes", table_size);
                return false;
            }

            // Verify all partitions fit within flash size and have proper alignment
            for (const auto& part : m_partitions)
            {
                if (part.pos.offset + part.pos.size > ESP_FLASH_SIZE)
                {
                    ESP_LOGE(TAG,
                             "Partition exceeds flash size - cannot write: offset=0x%lx, size=0x%lx",
                             part.pos.offset,
                             part.pos.size);
                    return false;
                }

                // Check alignment based on partition type
                uint32_t requiredAlignment = (part.type == PART_TYPE_APP) ? 0x10000 : 0x1000;
                if (part.pos.offset % requiredAlignment != 0)
                {
                    ESP_LOGE(TAG,
                             "Partition at offset 0x%lx is not properly aligned for type %s (required: 0x%lx)",
                             part.pos.offset,
                             getTypeString(part.type).c_str(),
                             requiredAlignment);
                    return false;
                }
            }

            uint8_t* buffer = new uint8_t[ESP_PARTITION_TABLE_MAX_LEN];
            if (!buffer)
            {
                ESP_LOGE(TAG, "Failed to allocate memory for partition table buffer");
                return false;
            }

            // Fill buffer with 0xFF (erased flash state)
            memset(buffer, 0xFF, ESP_PARTITION_TABLE_MAX_LEN);

            // Copy partitions to buffer
            esp_partition_info_t* table = reinterpret_cast<esp_partition_info_t*>(buffer);
            for (size_t i = 0; i < m_partitions.size(); i++)
            {
                table[i] = m_partitions[i];
            }
#ifdef CONFIG_PARTITION_TABLE_MD5
            struct MD5Context context;
            table[m_partitions.size()].magic = ESP_PARTITION_MAGIC_MD5;
            esp_rom_md5_init(&context);
            esp_rom_md5_update(&context, (unsigned char*)buffer, table_size);
            esp_rom_md5_final((unsigned char*)buffer + table_size + ESP_PARTITION_MD5_OFFSET, &context);
#endif
            // Erase the partition table sector
            esp_err_t err = bootloader_flash_erase_sector(ESP_PARTITION_TABLE_OFFSET / 0x1000);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to erase partition table sector: %s", esp_err_to_name(err));
                delete[] buffer;
                return false;
            }

            // Write the partition table to flash
            err = bootloader_flash_write(ESP_PARTITION_TABLE_OFFSET, buffer, ESP_PARTITION_TABLE_MAX_LEN, false);

            delete[] buffer;

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to write partition table to flash: %s", esp_err_to_name(err));
                return false;
            }

            ESP_LOGD(TAG, "Successfully wrote %lu partitions to flash", m_partitions.size());
            return true;
        }

        bool PartitionTable::recalculateOffsets(size_t startIndex)
        {
            if (m_partitions.empty() || startIndex >= m_partitions.size())
            {
                return true;
            }

            // Sort partitions by offset
            std::sort(m_partitions.begin(),
                      m_partitions.end(),
                      [](const esp_partition_info_t& a, const esp_partition_info_t& b) { return a.pos.offset < b.pos.offset; });

            // Start with the first partition or the specified index
            size_t i = startIndex;

            // If this is not the first partition, make sure it doesn't overlap with the previous one
            if (i > 0)
            {
                esp_partition_info_t& prev = m_partitions[i - 1];
                esp_partition_info_t& current = m_partitions[i];

                // Calculate the end of the previous partition
                uint32_t prev_end = prev.pos.offset + prev.pos.size;

                // Determine alignment based on partition type
                uint32_t alignment = (current.type == PART_TYPE_APP) ? 0x10000 : 0x1000;

                // Align to proper boundary based on type
                uint32_t aligned_end = (prev_end + alignment - 1) & ~(alignment - 1);

                // If current offset is smaller than aligned_end, adjust it
                if (current.pos.offset < aligned_end)
                {
                    current.pos.offset = aligned_end;
                }
            }

            // Now recalculate offsets for all subsequent partitions
            for (i++; i < m_partitions.size(); i++)
            {
                esp_partition_info_t& prev = m_partitions[i - 1];
                esp_partition_info_t& current = m_partitions[i];

                // Calculate the end of the previous partition
                uint32_t prev_end = prev.pos.offset + prev.pos.size;

                // Determine alignment based on partition type
                uint32_t alignment = (current.type == PART_TYPE_APP) ? 0x10000 : 0x1000;

                // Align to proper boundary based on type
                uint32_t aligned_end = (prev_end + alignment - 1) & ~(alignment - 1);

                // Update current offset
                current.pos.offset = aligned_end;

                // Verify we haven't exceeded flash size
                if (current.pos.offset + current.pos.size > ESP_FLASH_SIZE)
                {
                    ESP_LOGE(TAG, "Partition '%s' would exceed flash size after recalculation", (char*)current.label);
                    // Adjust the size to fit within flash limits
                    if (current.pos.offset < ESP_FLASH_SIZE)
                    {
                        uint32_t newSize = ESP_FLASH_SIZE - current.pos.offset;
                        ESP_LOGW(TAG, "Adjusting partition size from %lu to %lu bytes", current.pos.size, newSize);
                        current.pos.size = newSize;
                    }
                    else
                    {
                        // Can't fit the partition
                        return false;
                    }
                }
            }

            return true;
        }

        esp_partition_info_t* PartitionTable::findPartitionByOffset(uint32_t offset)
        {
            for (auto& partition : m_partitions)
            {
                if (offset >= partition.pos.offset && offset < partition.pos.offset + partition.pos.size)
                {
                    return &partition;
                }
            }
            return nullptr;
        }

        uint32_t PartitionTable::findNextAvailableOffset() const
        {
            if (m_partitions.empty())
            {
                // to make single partition app offset to match file offset
                return 0x0;
            }

            // Find the last partition by offset + size
            uint32_t max_end = 0;
            for (const auto& partition : m_partitions)
            {
                uint32_t end = partition.pos.offset + partition.pos.size;
                if (end > max_end)
                {
                    max_end = end;
                }
            }

            // We don't know what type of partition will use this offset yet,
            // so align to the most restrictive alignment (0x10000 for app partitions).
            // This ensures the returned offset will work for any partition type.
            uint32_t aligned_end = (max_end + 0xFFFF) & ~0xFFFF;

            // Ensure we don't exceed flash size
            if (aligned_end >= ESP_FLASH_SIZE)
            {
                // Try with 4K alignment for data partitions
                aligned_end = (max_end + 0xFFF) & ~0xFFF;

                if (aligned_end >= ESP_FLASH_SIZE)
                {
                    ESP_LOGW(TAG, "No more space available in flash for new partitions!");
                    return 0; // Indicate no space left
                }
                else
                {
                    ESP_LOGW(TAG, "Only space for data partitions (4K aligned) remaining");
                }
            }

            return aligned_end;
        }

        esp_partition_info_t* PartitionTable::getPartition(size_t index) const
        {
            if (index >= m_partitions.size())
            {
                ESP_LOGE(TAG, "Partition index out of bounds: %lu", index);
                return nullptr;
            }
            return const_cast<esp_partition_info_t*>(&m_partitions[index]);
        }

        uint8_t PartitionTable::getNextOTA()
        {
            uint8_t ota_count = ESP_PARTITION_SUBTYPE_APP_OTA_MAX - ESP_PARTITION_SUBTYPE_APP_OTA_MIN;
            std::vector<bool> used_subtypes(ota_count, false); // Track used OTA subtypes (0-15)

            // Mark used OTA subtypes
            for (const auto& partition : m_partitions)
            {
                if (partition.type == ESP_PARTITION_TYPE_APP)
                {
                    if (partition.subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
                        partition.subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX)
                    {
                        used_subtypes[partition.subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN] = true;
                    }
                }
            }

            // Find first unused OTA subtype
            for (uint8_t i = 0; i < ota_count; i++)
            {
                if (!used_subtypes[i])
                {
                    uint8_t subtype = ESP_PARTITION_SUBTYPE_APP_OTA_MIN + i;
                    ESP_LOGD(TAG, "Found next available OTA subtype: 0x%x", subtype);
                    return subtype;
                }
            }

            // All OTA slots are used
            ESP_LOGW(TAG, "No available OTA subtypes found");
            return ESP_PARTITION_SUBTYPE_ANY;
        }

        size_t PartitionTable::getFreeSpace(uint8_t type)
        {
            // Get the alignment requirement based on partition type
            size_t alignment = (type == PART_TYPE_APP) ? 0x10000 : 0x1000;

            // Find the end of the last partition
            size_t last_end = 0;
            for (const auto& partition : m_partitions)
            {
                size_t end = partition.pos.offset + partition.pos.size;
                if (end > last_end)
                {
                    last_end = end;
                }
            }

            // Align the start address for the new partition
            size_t aligned_start = (last_end + alignment - 1) & ~(alignment - 1);

            // If aligned start is beyond flash size, no space available
            if (aligned_start >= ESP_FLASH_SIZE)
            {
                ESP_LOGW(TAG, "No space available for new partition (aligned start: 0x%zx)", aligned_start);
                return 0;
            }

            // Calculate maximum available size
            size_t max_size = ESP_FLASH_SIZE - aligned_start;

            // Align the size to the partition type requirement
            max_size = max_size & ~(alignment - 1);

            ESP_LOGD(TAG,
                     "Free space for %s partition: 0x%zx bytes at offset 0x%zx",
                     (type == PART_TYPE_APP) ? "app" : "data",
                     max_size,
                     aligned_start);

            return max_size;
        }

        static esp_err_t write_otadata(esp_ota_select_entry_t* otadata, uint32_t offset, bool write_encrypted)
        {
            esp_err_t err = bootloader_flash_erase_sector(offset / FLASH_SECTOR_SIZE);
            if (err == ESP_OK)
            {
                err = bootloader_flash_write(offset, otadata, sizeof(esp_ota_select_entry_t), write_encrypted);
            }
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Error in write_otadata operation. err = 0x%x", err);
            }
            return err;
        }

        static uint32_t bootloader_common_ota_select_crc(const esp_ota_select_entry_t* s)
        {
            return esp_rom_crc32_le(UINT32_MAX, (uint8_t*)&s->ota_seq, 4);
        }

        static void set_actual_ota_seq(int index)
        {
            esp_ota_select_entry_t otadata;
            memset(&otadata, 0xFF, sizeof(otadata));
            otadata.ota_seq = index + 1;
            otadata.ota_state = ESP_OTA_IMG_VALID;
            otadata.crc = bootloader_common_ota_select_crc(&otadata);

            bool write_encrypted = false;
            //
            const esp_partition_t* otadata_partition =
                esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
            if (!otadata_partition)
            {
                ESP_LOGE(TAG, "Failed to find ota data partition");
                return;
            }
            write_otadata(&otadata, otadata_partition->address, write_encrypted);
            ESP_LOGD(TAG, "Set actual ota_seq=%" PRIu32 " in otadata[0]", otadata.ota_seq);
        }

        // we cant use esp_partition_set_boot_partition() if PT has been changed
        FlashStatus set_boot_partition(const esp_partition_info_t* pi)
        {
            if (!pi)
            {
                ESP_LOGE(TAG, "Partition info is NULL");
                return FlashStatus::ERROR_PARTITION_NOT_FOUND;
            }
            if (pi->type != ESP_PARTITION_TYPE_APP)
            {
                ESP_LOGE(TAG, "Invalid partition type: %d", pi->type);
                return FlashStatus::ERROR_PARTITION_TABLE;
            }
            if (pi->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MIN || pi->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MAX)
            {
                ESP_LOGE(TAG, "Invalid partition subtype: %d", pi->subtype);
                return FlashStatus::ERROR_PARTITION_TABLE;
            }
            set_actual_ota_seq(pi->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN);
            return FlashStatus::SUCCESS;
        }

        bool PartitionTable::movePartitionData(
            uint32_t src_offset, uint32_t size, uint32_t dst_offset, progress_callback_t progress_cb, void* arg_cb)
        {
            if (src_offset == dst_offset)
            {
                // No need to move if source and destination are the same
                return true;
            }

            ESP_LOGD(TAG, "Moving partition data: 0x%lx -> 0x%lx (size: 0x%lx)", src_offset, dst_offset, size);

            // Allocate buffer for moving data in chunks
            const uint32_t BUFFER_SIZE = 4096; // 4KB chunks
            uint8_t* buffer = new uint8_t[BUFFER_SIZE];
            if (!buffer)
            {
                ESP_LOGE(TAG, "Failed to allocate buffer for moving partition data");
                return false;
            }

            bool success = true;
            uint32_t bytes_moved = 0;

            // If moving up (dst < src), move from start to end
            // If moving down (dst > src), move from end to start to avoid overwriting
            if (dst_offset < src_offset)
            {
                while (bytes_moved < size)
                {
                    uint32_t chunk_size = std::min(BUFFER_SIZE, size - bytes_moved);

                    // Read chunk from source
                    esp_err_t err = bootloader_flash_read(src_offset + bytes_moved, buffer, chunk_size, false);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG,
                                 "Failed to read flash at offset 0x%lx: %s",
                                 src_offset + bytes_moved,
                                 esp_err_to_name(err));
                        success = false;
                        break;
                    }

                    // Erase destination sector if needed
                    uint32_t dst_sector = (dst_offset + bytes_moved) / SPI_FLASH_SEC_SIZE;
                    err = bootloader_flash_erase_sector(dst_sector);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Failed to erase sector %lu: %s", dst_sector, esp_err_to_name(err));
                        success = false;
                        break;
                    }

                    // Write chunk to destination
                    err = bootloader_flash_write(dst_offset + bytes_moved, buffer, chunk_size, false);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG,
                                 "Failed to write flash at offset 0x%lx: %s",
                                 dst_offset + bytes_moved,
                                 esp_err_to_name(err));
                        success = false;
                        break;
                    }

                    bytes_moved += chunk_size;
                    if (progress_cb)
                    {
                        progress_cb(
                            (bytes_moved * 100) / size,
                            std::format("Moved {} / {}KB", (uint32_t)(bytes_moved / 1024), (uint32_t)(size / 1024)).c_str(),
                            arg_cb);
                    }
                }
            }
            else
            {
                // Moving down - start from the end
                while (bytes_moved < size)
                {
                    uint32_t remaining = size - bytes_moved;
                    uint32_t chunk_size = std::min(BUFFER_SIZE, remaining);
                    uint32_t offset = size - bytes_moved - chunk_size;

                    // Read chunk from source
                    esp_err_t err = bootloader_flash_read(src_offset + offset, buffer, chunk_size, false);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Failed to read flash at offset 0x%lx: %s", src_offset + offset, esp_err_to_name(err));
                        success = false;
                        break;
                    }

                    // Erase destination sector if needed
                    uint32_t dst_sector = (dst_offset + offset) / SPI_FLASH_SEC_SIZE;
                    err = bootloader_flash_erase_sector(dst_sector);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Failed to erase sector %lu: %s", dst_sector, esp_err_to_name(err));
                        success = false;
                        break;
                    }

                    // Write chunk to destination
                    err = bootloader_flash_write(dst_offset + offset, buffer, chunk_size, false);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Failed to write flash at offset 0x%lx: %s", dst_offset + offset, esp_err_to_name(err));
                        success = false;
                        break;
                    }

                    bytes_moved += chunk_size;
                    if (progress_cb)
                    {
                        progress_cb((bytes_moved * 100) / size, "Moving partition data", arg_cb);
                    }
                }
            }

            delete[] buffer;

            if (!success)
            {
                ESP_LOGE(TAG, "Failed to move partition data");
            }

            return success;
        }

        void PartitionTable::updateFlashUsageInfo()
        {
            uint32_t total_size = getFreeSpace(PART_TYPE_APP);
            s_flash_usage_percent = ((ESP_FLASH_SIZE - total_size) * 100) / ESP_FLASH_SIZE;
        }

        void PartitionTable::initFlashUsagePercent()
        {
            PartitionTable flash_table;
            flash_table.load();
        }

    } // namespace FLASH_TOOLS
} // namespace UTILS
