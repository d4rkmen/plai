/**
 * @file settings_screen.h
 * @brief Settings screens declarations
 */

#pragma once

#include "hal.h"
#include "../../../settings/settings.h"
#include "../anim/hl_text.h"
#include "../anim/scroll_text.h"
#include <functional>
#include <string>

namespace UTILS
{
    namespace UI
    {
        namespace SETTINGS_SCREEN
        {
            /**
             * @brief Render settings group list screen
             *
             * @param hal HAL instance
             * @param groups List of setting groups
             * @param hint_ctx Highlight text context for displaying hints
             * @return true if screen was updated
             */
            bool render_groups(HAL::Hal* hal,
                               const std::vector<SETTINGS::SettingGroup_t>& groups,
                               HL_TEXT::HLTextContext_t* hint_ctx);

            /**
             * @brief Render settings items list screen
             *
             * @param hal HAL instance
             * @param group Selected setting group
             * @return true if screen was updated
             */
            bool render_items(HAL::Hal* hal, SETTINGS::SettingGroup_t& group);

            /**
             * @brief Render scrolling description for the currently selected item
             *
             * @param hal HAL instance
             * @param group Selected setting group
             * @param desc_scroll_ctx Scrolling text context for item descriptions
             * @return true if screen was updated
             */
            bool render_scrolling_desc(HAL::Hal* hal,
                                       const SETTINGS::SettingGroup_t& group,
                                       SCROLL_TEXT::ScrollTextContext_t* desc_scroll_ctx);

            /**
             * @brief Handle user input on the groups selection screen
             *
             * @param hal HAL instance
             * @param groups List of setting groups
             * @param on_enter Callback function when a group is selected
             * @return true if selection changed
             */
            bool
            handle_group_selection(HAL::Hal* hal, std::vector<SETTINGS::SettingGroup_t>& groups, std::function<void()> on_exit);

            /**
             * @brief Handle user input on the items selection screen
             *
             * @param hal HAL instance
             * @param group Selected setting group
             * @param desc_scroll_ctx Scrolling text context for item descriptions
             * @param on_item_selected Callback function when an item is selected
             * @return true if selection changed
             */
            bool handle_item_selection(HAL::Hal* hal,
                                       SETTINGS::SettingGroup_t& group,
                                       SCROLL_TEXT::ScrollTextContext_t* desc_scroll_ctx);

            void reset();
            /**
             * @brief Main update function that handles all settings screen functionality
             *
             * @param hal HAL instance
             * @param groups List of setting groups
             * @param hint_ctx Highlight text context for displaying hints
             * @param desc_ctx Scrolling text context for item descriptions
             * @param on_enter Callback function when a group is selected
             * @return true if screen needs updating
             */
            bool update(HAL::Hal* hal,
                        std::vector<SETTINGS::SettingGroup_t>& groups,
                        HL_TEXT::HLTextContext_t* hint_ctx,
                        SCROLL_TEXT::ScrollTextContext_t* desc_ctx,
                        std::function<void()> on_exit);

            /**
             * @brief Handle changes to a setting item
             *
             * @param hal HAL instance
             * @param group Setting group
             * @param item Setting item to be changed
             */
            void handle_setting_change(HAL::Hal* hal, SETTINGS::SettingGroup_t& group, SETTINGS::SettingItem_t& item);

            /**
             * @brief Save a setting item to persistent storage
             *
             * @param hal HAL instance
             * @param group Setting group
             * @param item Setting item to be saved
             */
            void save_setting(HAL::Hal* hal, const SETTINGS::SettingGroup_t& group, const SETTINGS::SettingItem_t& item);

        } // namespace SETTINGS_SCREEN
    } // namespace UI
} // namespace UTILS