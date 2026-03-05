/**
 * @file hl_text.h
 * @author 0d4rkmen
 * @brief Utility for text highlighting animation
 * @version 0.1
 * @date 2025-03-30
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include "lgfx/v1/LGFX_Sprite.hpp"

#define HL_TEXT_SPEED 20   // Default animation speed in ms
#define HL_TEXT_DELAY 1500 // Default delay between animations in ms

namespace UTILS
{
    namespace HL_TEXT
    {
        /**
         * @brief Context structure for highlight text animation
         */
        typedef struct
        {
            LGFX_Sprite* sprite = nullptr;            // Sprite for rendering
            lgfx::LovyanGFX* canvas = nullptr;        // Canvas for rendering
            uint32_t last_update_time = 0;            // Last time the animation was updated
            uint32_t animation_speed = HL_TEXT_SPEED; // Update period in ms
            uint32_t animation_delay = HL_TEXT_DELAY; // Delay between animations in ms
            int current_char_index = -1;              // Current highlighted character index
            uint32_t timeout = HL_TEXT_DELAY;         // Current timeout duration
            bool is_rendered = false;                 // Flag to indicate if the animation is rendered
        } HLTextContext_t;

        /**
         * @brief Initialize highlight text context
         *
         * @param ctx Pointer to context to initialize
         * @param canvas Base canvas for rendering
         * @param speed_ms Animation speed in ms
         * @param delay_ms Delay between animations in ms
         * @return true if initialization was successful
         */
        bool hl_text_init(HLTextContext_t* ctx,
                          lgfx::LovyanGFX* canvas,
                          uint32_t speed_ms = HL_TEXT_SPEED,
                          uint32_t delay_ms = HL_TEXT_DELAY);

        /**
         * @brief Render highlight text animation
         *
         * @param ctx Pointer to initialized context
         * @param text Text to highlight
         * @param x X position on canvas
         * @param y Y position on canvas
         * @param normal_color Normal text color
         * @param highlight_color Highlighted character color
         * @param bg_color Background color
         * @return true if animation was updated
         */
        bool hl_text_render(
            HLTextContext_t* ctx, const char* text, int x, int y, int normal_color, int highlight_color, uint32_t bg_color);

        /**
         * @brief Free resources used by highlight text context
         *
         * @param ctx Pointer to context to free
         */
        void hl_text_free(HLTextContext_t* ctx);

        /**
         * @brief Reset highlight text animation
         *
         * @param ctx Pointer to context to reset
         */
        void hl_text_reset(HLTextContext_t* ctx);
    } // namespace HL_TEXT
} // namespace UTILS