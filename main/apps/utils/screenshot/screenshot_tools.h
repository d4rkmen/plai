/**
 * @file screenshot_tools.h
 * @brief Screenshot utility functions
 * @version 0.1
 * @date 2024
 *
 * @copyright Copyright (c) 2024
 *
 */
#pragma once

#include "hal/hal.h"

namespace UTILS
{
    namespace SCREENSHOT_TOOLS
    {
        /**
         * @brief Take a screenshot and save it to SD card
         *
         * @param hal Pointer to HAL instance
         * @return true if screenshot was saved successfully, false otherwise
         */
        bool take_screenshot(HAL::Hal* hal);

        /**
         * @brief Check for screenshot key combination (CTRL + SPACE) and handle it
         *
         * @param hal Pointer to HAL instance
         * @param system_bar_force_update_flag Pointer to flag that forces system bar update
         * @return true if screenshot was taken, false otherwise
         */
        bool check_and_handle_screenshot(HAL::Hal* hal, bool* system_bar_force_update_flag);

    } // namespace SCREENSHOT_TOOLS
} // namespace UTILS
