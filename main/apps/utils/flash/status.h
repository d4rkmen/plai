#pragma once

namespace UTILS
{
    namespace FLASH_TOOLS
    {
        /**
         * @brief Progress callback function type
         */
        typedef void (*progress_callback_t)(int progress, const char* message, void* arg);
        /**
         * @brief Status of the flashing operation
         */
        enum class FlashStatus
        {
            SUCCESS = 0,
            ERROR_FILE_NOT_FOUND,
            ERROR_FILE_READ,
            ERROR_MEMORY_ALLOCATION,
            ERROR_INVALID_FIRMWARE,
            ERROR_INVALID_CHIP_ID,
            ERROR_INSUFFICIENT_SPACE,
            ERROR_FLASH_WRITE,
            ERROR_PARTITION_TABLE,
            ERROR_PARTITION_ADD,
            ERROR_PARTITION_NOT_FOUND,
            ERROR_FORMAT_FILESYSTEM,
            ERROR_UNKNOWN
        };
    } // namespace FLASH_TOOLS
} // namespace UTILS