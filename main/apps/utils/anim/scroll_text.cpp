/**
 * @file scroll_text.cpp
 * @brief Implementation of scroll text utility
 * @version 0.1
 * @date 2024-06-18
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "scroll_text.h"
#include "common_define.h"
#include "../theme/theme_define.h"
#include <string.h>
#include "apps/utils/text/text_utils.h"

namespace UTILS
{
    namespace SCROLL_TEXT
    {
        using UTILS::TEXT::utf8_char_len;

        // UTF-8 helper: Find byte offset of first visible character
        // Walks through text measuring character widths until we reach start_px
        // Returns byte offset and sets out_pixel_offset to the pixel position of that character
        static int utf8_find_start_offset(lgfx::LovyanGFX* canvas, const char* text, int start_px, int& out_pixel_offset)
        {
            const char* ptr = text;
            int current_px = 0;
            int byte_offset = 0;

            while (*ptr)
            {
                int char_len = utf8_char_len((unsigned char)*ptr);
                if (char_len == 0)
                    break;

                // Measure this character's width
                char temp[5] = {0};
                memcpy(temp, ptr, char_len);
                int char_width = canvas->textWidth(temp);

                // If this character extends past start_px, it's our first visible char
                if (current_px + char_width > start_px)
                {
                    out_pixel_offset = current_px;
                    return byte_offset;
                }

                current_px += char_width;
                ptr += char_len;
                byte_offset += char_len;
            }

            // Reached end of string
            out_pixel_offset = current_px;
            return byte_offset;
        }

        bool scroll_text_init(
            ScrollTextContext_t* ctx, lgfx::LovyanGFX* canvas, int width, int height, uint32_t speed_ms, uint32_t pause_ms)
        {
            return scroll_text_init_ex(ctx, canvas, width, height, speed_ms, pause_ms, FONT_16);
        };
        bool scroll_text_init_ex(ScrollTextContext_t* ctx,
                                 lgfx::LovyanGFX* canvas,
                                 int width,
                                 int height,
                                 uint32_t speed_ms,
                                 uint32_t pause_ms,
                                 const lgfx::IFont* font)
        {
            if (!ctx || !canvas || width <= 0 || height <= 0)
                return false;

            // Free any existing sprite
            if (ctx->sprite)
            {
                ctx->sprite->deleteSprite();
                delete ctx->sprite;
            }

            // Initialize context
            ctx->sprite = new LGFX_Sprite(canvas);
            if (!ctx->sprite)
                return false;

            ctx->canvas = canvas;
            ctx->sprite->createSprite(width, height);
            ctx->sprite->setFont(font);
            ctx->sprite->setTextSize(1);

            ctx->width = width;
            ctx->height = height;
            ctx->scroll_time_count = millis();
            ctx->scroll_period = speed_ms;
            ctx->pause_duration = pause_ms;
            ctx->scroll_pos = 0;
            ctx->scroll_direction = true; // Start scrolling from right to left
            ctx->is_rendered = false;

            return true;
        }

        bool scroll_text_render(
            ScrollTextContext_t* ctx, const char* text, int x, int y, uint32_t fg_color, uint32_t bg_color, bool force)
        {
            if (!ctx || !ctx->sprite || !text)
                return false;

            // Calculate text width (approximate - 8 pixels per character)
            const int text_width = ctx->canvas->textWidth(text);

            // If text fits in the area and we're not forcing scroll, just render it statically
            if (text_width <= ctx->width)
            {
                if (ctx->scroll_direction)
                {
                    ctx->sprite->fillScreen(bg_color);
                    // Static display - text fits in area
                    ctx->sprite->setTextColor(fg_color, bg_color);
                    ctx->sprite->drawString(text, 0, 0);
                    // Push to canvas
                    ctx->sprite->pushSprite(ctx->canvas, x, y);
                    ctx->scroll_direction = false; // using direction flasg as update flag
                    return true;                   // update required
                }
                else
                {
                    return false; // no update required
                }
            }

            // Scrolling is needed
            uint32_t now = millis();
            bool updated = false;
            if (now > (ctx->scroll_time_count + ctx->scroll_period))
            {
                ctx->scroll_time_count = now;
                updated = true;
                if (ctx->is_rendered)
                {
                    // Calculate positions
                    if (ctx->scroll_direction)
                    {
                        // Scrolling right to left
                        ctx->scroll_pos--;
                        if (ctx->scroll_pos <= ctx->width - text_width)
                        {
                            ctx->scroll_pos = ctx->width - text_width;
                            ctx->scroll_time_count = now + ctx->pause_duration;
                            ctx->scroll_direction = false;
                        }
                    }
                    else
                    {
                        // Scrolling left to right
                        ctx->scroll_pos++;
                        if (ctx->scroll_pos >= 0)
                        {
                            ctx->scroll_pos = 0;
                            ctx->scroll_time_count = now + ctx->pause_duration;
                            ctx->scroll_direction = true;
                        }
                    }
                }
                else
                {
                    ctx->is_rendered = true;
                    // wait for pause duration to start scrolling
                    ctx->scroll_time_count += ctx->pause_duration;
                }
            }
            if (force || updated)
            {
                // Update the sprite contents
                ctx->sprite->fillScreen(bg_color);
                ctx->sprite->setTextColor(fg_color, bg_color);

                // Find first visible UTF-8 character (no memory allocation)
                int pixel_offset = 0;
                int byte_offset = 0;
                if (ctx->scroll_pos < 0)
                {
                    // Text is scrolled left, find first visible character
                    byte_offset = utf8_find_start_offset(ctx->canvas, text, -ctx->scroll_pos, pixel_offset);
                }

                // Draw from the first visible character - sprite clips the rest
                int draw_x = ctx->scroll_pos + pixel_offset;
                ctx->sprite->drawString(text + byte_offset, draw_x, 0);

                // Push to canvas
                ctx->sprite->pushSprite(ctx->canvas, x, y);
            }

            return updated;
        }
        void scroll_text_reset(ScrollTextContext_t* ctx)
        {
            if (!ctx)
                return;

            ctx->scroll_pos = 0;
            ctx->scroll_direction = true;
            ctx->scroll_time_count = millis();
            ctx->is_rendered = false;
        }

        void scroll_text_free(ScrollTextContext_t* ctx)
        {
            if (!ctx)
                return;

            if (ctx->sprite)
            {
                ctx->sprite->deleteSprite();
                delete ctx->sprite;
                ctx->sprite = nullptr;
            }
        }
    } // namespace SCROLL_TEXT
} // namespace UTILS
