/**
 * @file hl_text.cpp
 * @brief Implementation of text highlighting animation utility
 * @version 0.1
 * @date 2024-06-18
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "hl_text.h"
#include "common_define.h"
#include "../theme/theme_define.h"
#include <string.h>
#include <algorithm>
#include "apps/utils/text/text_utils.h"

namespace UTILS
{
    namespace HL_TEXT
    {
        using UTILS::TEXT::utf8_char_len;

        // Count UTF-8 characters in a string
        static int utf8_strlen(const char* s)
        {
            int count = 0;
            while (*s)
            {
                s += utf8_char_len((uint8_t)*s);
                count++;
            }
            return count;
        }

        // Return byte offset of the Nth UTF-8 character (0-based).
        // Returns byte length of the string if n >= char count.
        static int utf8_byte_offset(const char* s, int n)
        {
            const char* p = s;
            for (int i = 0; i < n && *p; i++)
                p += utf8_char_len((uint8_t)*p);
            return (int)(p - s);
        }

        bool hl_text_init(HLTextContext_t* ctx, lgfx::LovyanGFX* canvas, uint32_t speed_ms, uint32_t delay_ms)
        {
            if (!ctx || !canvas)
                return false;

            ctx->sprite = new LGFX_Sprite(canvas);
            if (!ctx->sprite)
                return false;
            ctx->sprite->createSprite(canvas->width(), ctx->sprite->fontHeight(FONT_10));
            ctx->sprite->setFont(FONT_10);
            ctx->sprite->setTextSize(1);
            ctx->canvas = canvas;
            ctx->animation_speed = speed_ms;
            ctx->animation_delay = delay_ms;
            ctx->current_char_index = -1;
            ctx->last_update_time = millis();
            ctx->timeout = delay_ms;
            ctx->is_rendered = false;

            return true;
        }

        bool hl_text_render(
            HLTextContext_t* ctx, const char* text, int x, int y, int normal_color, int highlight_color, uint32_t bg_color)
        {
            if (!ctx)
                return false;

            uint32_t now = millis();
            bool updated = false;
            int char_count = utf8_strlen(text);

            if (!ctx->is_rendered)
            {
                ctx->sprite->fillScreen(bg_color);
                ctx->sprite->setTextColor(normal_color, bg_color);
                ctx->sprite->drawCenterString(text, ctx->sprite->width() / 2, 0);

                if (ctx->current_char_index >= char_count)
                    ctx->current_char_index = -1;

                if (ctx->current_char_index >= 0)
                {
                    int byte_off = utf8_byte_offset(text, ctx->current_char_index);
                    int clen = utf8_char_len((uint8_t)text[byte_off]);

                    // Extract the single UTF-8 character
                    char highlighted_char[5] = {};
                    memcpy(highlighted_char, text + byte_off, std::min(clen, 4));

                    ctx->sprite->setTextColor(highlight_color, bg_color);
                    int total_width = ctx->sprite->textWidth(text);
                    int start_x = ctx->sprite->width() / 2 - total_width / 2;

                    // Measure prefix up to this character
                    char prefix[256] = {};
                    int prefix_len = std::min(byte_off, (int)sizeof(prefix) - 1);
                    memcpy(prefix, text, prefix_len);
                    prefix[prefix_len] = '\0';
                    int char_pos = ctx->sprite->textWidth(prefix);

                    ctx->sprite->drawString(highlighted_char, start_x + char_pos, 0);
                }
                ctx->sprite->pushSprite(x, y, bg_color);
                updated = true;
            }

            if (now - ctx->last_update_time > ctx->timeout)
            {
                ctx->current_char_index++;

                if (ctx->current_char_index >= char_count)
                {
                    ctx->current_char_index = -1;
                    ctx->timeout = ctx->animation_delay;
                }
                else
                {
                    ctx->timeout = ctx->animation_speed;
                }

                ctx->last_update_time = now;
                ctx->is_rendered = false;
            }

            return updated;
        }

        void hl_text_reset(HLTextContext_t* ctx)
        {
            if (!ctx)
                return;

            ctx->current_char_index = -1;
            ctx->last_update_time = millis();
            ctx->timeout = ctx->animation_delay;
            ctx->is_rendered = false;
        }

        void hl_text_free(HLTextContext_t* ctx)
        {
            if (!ctx)
                return;
            if (ctx->sprite)
            {
                ctx->sprite->deleteSprite();
                delete ctx->sprite;
            }
        }
    } // namespace HL_TEXT
} // namespace UTILS