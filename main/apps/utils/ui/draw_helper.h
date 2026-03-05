/**
 * @file draw_helper.h
 * @brief Shared UI drawing helpers: scrollbar, node colors, message status
 */
#pragma once

#include <cstdint>
#include "lgfx/v1/LGFX_Sprite.hpp"

namespace UTILS
{
    namespace UI
    {
        uint32_t node_color(uint32_t node_id);
        uint32_t node_text_color(uint32_t node_id);

        void draw_scrollbar(LGFX_Sprite* canvas,
                            int x,
                            int y,
                            int width,
                            int height,
                            int total,
                            int visible,
                            int offset,
                            int min_thumb = 10,
                            int track_color = TFT_DARKGREY,
                            int thumb_color = TFT_ORANGE);

        struct StatusInfo
        {
            int color;
            const char* icon;
        };

        StatusInfo message_status_info(int status);

    } // namespace UI
} // namespace UTILS
