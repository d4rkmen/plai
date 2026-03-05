/**
 * @file scroll_text.h
 * @brief Utility for smooth text scrolling
 * @version 0.1
 * @date 2024-06-18
 *
 * @copyright Copyright (c) 2024
 *
 */
#pragma once

#include "lgfx/v1/LGFX_Sprite.hpp"
#include "lgfx/v1/lgfx_fonts.hpp"

#define SCROLL_TEXT_PAUSE 1000 // Default pause duration at scroll ends in ms

namespace UTILS
{
    namespace SCROLL_TEXT
    {
        /**
         * @brief Context structure for scroll text
         */
        typedef struct
        {
            LGFX_Sprite* sprite = nullptr;               // Sprite for rendering
            lgfx::LovyanGFX* canvas = nullptr;           // Canvas for rendering
            uint32_t scroll_time_count = 0;              // Time tracking for scroll animation
            uint32_t scroll_period = 50;                 // Update period in ms (scroll speed)
            uint32_t pause_duration = SCROLL_TEXT_PAUSE; // Pause duration at scroll ends
            int scroll_pos = 0;                          // Current scroll position
            bool scroll_direction = true;                // true = right to left, false = left to right
            int width = 0;                               // Width of the scrolling area
            int height = 0;                              // Height of the scrolling area
            bool is_rendered = false;                    //
        } ScrollTextContext_t;

        /**
         * @brief Initialize scroll text context
         *
         * @param ctx Pointer to context to initialize
         * @param canvas Base canvas to create sprite on
         * @param width Width of the scrolling area
         * @param height Height of the scrolling area
         * @param speed_ms Update period in ms (lower = faster)
         * @param pause_ms Pause duration at ends in ms
         * @return true if initialization was successful
         */
        bool scroll_text_init(ScrollTextContext_t* ctx,
                              lgfx::LovyanGFX* canvas,
                              int width,
                              int height,
                              uint32_t speed_ms = 50,
                              uint32_t pause_ms = SCROLL_TEXT_PAUSE);
        /**
         * @brief Initialize scroll text context
         *
         * @param ctx Pointer to context to initialize
         * @param canvas Base canvas to create sprite on
         * @param width Width of the scrolling area
         * @param height Height of the scrolling area
         * @param speed_ms Update period in ms (lower = faster)
         * @param pause_ms Pause duration at ends in ms
         * @param font Font to use
         * @return true if initialization was successful
         */
        bool scroll_text_init_ex(ScrollTextContext_t* ctx,
                                 lgfx::LovyanGFX* canvas,
                                 int width,
                                 int height,
                                 uint32_t speed_ms,
                                 uint32_t pause_ms,
                                 const lgfx::IFont* font);

        /**
         * @brief Render scrolling text
         *
         * @param ctx Pointer to initialized context
         * @param text Text to scroll
         * @param x X position on canvas
         * @param y Y position on canvas
         * @param fg_color Text color
         * @param bg_color Background color
         * @return true if text was updated (scrolled)
         */
        bool scroll_text_render(
            ScrollTextContext_t* ctx, const char* text, int x, int y, uint32_t fg_color, uint32_t bg_color, bool force = false);

        /**
         * @brief Free resources used by scroll text context
         *
         * @param ctx Pointer to context to free
         */
        void scroll_text_free(ScrollTextContext_t* ctx);

        /**
         * @brief Reset scroll text context
         *
         * @param ctx Pointer to context to reset
         */
        void scroll_text_reset(ScrollTextContext_t* ctx);
    } // namespace SCROLL_TEXT
} // namespace UTILS
