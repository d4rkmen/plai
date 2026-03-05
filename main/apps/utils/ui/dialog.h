#pragma once

#include "hal.h"
#include "../theme/theme_define.h"
#include "common_define.h"
#include "../anim/scroll_text.h"
#include <string>
#include <vector>

namespace UTILS
{
    namespace UI
    {
        struct DialogButton_t
        {
            std::string text;
            uint32_t bg_color;
            uint16_t text_color;
            DialogButton_t(const std::string& text, uint32_t bg = THEME_COLOR_BG, uint16_t fg = TFT_WHITE)
                : text(text), bg_color(bg), text_color(fg)
            {
            }
        };

        /**
         * @brief Show a dialog box with customizable appearance and buttons
         *
         * @param hal HAL instance for drawing and input
         * @param title Dialog title
         * @param title_color Title text color
         * @param message Dialog message
         * @param message_color Message text color
         * @param buttons Vector of buttons to show
         * @param close_timeout_ms Timeout in ms after which dialog closes (0 = no timeout)
         * @param scroll_speed Speed of text scrolling (pixels per frame)
         * @param scroll_pause_ms Pause time at text ends in ms
         * @return int Index of pressed button, -1 if cancelled with BACKSPACE
         */
        int show_dialog(HAL::Hal* hal,
                        const std::string& title,
                        uint32_t title_color,
                        const std::string& message,
                        uint32_t message_color,
                        const std::vector<DialogButton_t>& buttons,
                        uint32_t close_timeout_ms = 0,
                        uint8_t scroll_speed = 20,
                        uint32_t scroll_pause_ms = 500);

        // Convenience functions for common dialog types
        bool show_confirmation_dialog(HAL::Hal* hal,
                                      const std::string& title,
                                      const std::string& message,
                                      const std::string& ok_text = "OK",
                                      const std::string& cancel_text = "Cancel");

        void show_error_dialog(HAL::Hal* hal,
                               const std::string& title,
                               const std::string& message,
                               const std::string& button_text = "OK");

        int show_message_dialog(HAL::Hal* hal,
                                const std::string& title,
                                const std::string& message,
                                uint32_t close_timeout_ms = 2000);

        void show_progress(HAL::Hal* hal, const std::string& title, int progress, const std::string& message);

        // New dialog functions for settings
        bool show_edit_bool_dialog(HAL::Hal* hal, const std::string& title, bool& value);

        bool
        show_edit_number_dialog(HAL::Hal* hal, const std::string& title, int& value, int min_value = 0, int max_value = 999);

        bool show_edit_string_dialog(
            HAL::Hal* hal, const std::string& title, std::string& value, bool is_password = false, int max_length = 32);

        /**
         * @brief Show a selection dialog with a scrollable list of items
         *
         * @param hal HAL instance for drawing and input
         * @param title Dialog title
         * @param items Vector of strings to display in the list
         * @param default_index Default selected index (or -1 for first item)
         * @return int Index of selected item, or -1 if canceled
         */
        int show_select_dialog(HAL::Hal* hal,
                               const std::string& title,
                               const std::vector<std::string>& items,
                               int default_index = 0);
    } // namespace UI
} // namespace UTILS