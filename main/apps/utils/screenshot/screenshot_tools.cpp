/**
 * @file screenshot_tools.cpp
 * @brief Implementation of screenshot utility functions
 * @version 0.1
 * @date 2024
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "screenshot_tools.h"
#include "hal/hal.h"
#include "hal/keyboard/keyboard.h"
#include "common_define.h"
#include "apps/utils/theme/theme_define.h"
#include "esp_log.h"
#include <format>
#include <sys/stat.h>
#include <errno.h>
#include <cstring>
#include <cstdlib>
#include <ctime>

static const char* TAG = "SCREENSHOT_TOOLS";

namespace UTILS
{
    namespace SCREENSHOT_TOOLS
    {
        bool take_screenshot(HAL::Hal* hal)
        {
            ESP_LOGI(TAG, "Taking screenshot...");
            bool sdcard_mounted = hal->sdcard()->is_mounted();

            // Mount SD card
            if (!sdcard_mounted)
            {
                if (!hal->sdcard()->mount(false))
                {
                    ESP_LOGE(TAG, "Failed to mount SD card for screenshot");
                    return false;
                }
            }

            // Create screenshots directory if it doesn't exist
            const char* parent_dir = "/sdcard/m5apps";
            const char* screenshots_dir = "/sdcard/m5apps/screenshots";
            struct stat st;

            // Create parent directory if it doesn't exist
            if (stat(parent_dir, &st) != 0)
            {
                if (mkdir(parent_dir, 0777) != 0 && errno != EEXIST)
                {
                    ESP_LOGE(TAG, "Failed to create parent directory");
                    return false;
                }
            }

            // Create screenshots directory if it doesn't exist
            if (stat(screenshots_dir, &st) != 0)
            {
                if (mkdir(screenshots_dir, 0777) != 0 && errno != EEXIST)
                {
                    ESP_LOGE(TAG, "Failed to create screenshots directory");
                    return false;
                }
            }

            // Get display dimensions
            int32_t width = hal->display()->width();
            int32_t height = hal->display()->height();

            // Generate filename with timestamp
            uint32_t timestamp = millis();
            std::string filename = std::format("/sdcard/m5apps/screenshots/plai_{:08x}.bmp", timestamp);

            // Calculate row size (must be multiple of 4 bytes)
            uint32_t row_size = ((width * 3 + 3) / 4) * 4;
            uint32_t image_size = row_size * height;
            uint32_t file_size = 54 + image_size;

            // Open file for writing
            FILE* file = fopen(filename.c_str(), "wb");
            if (!file)
            {
                ESP_LOGE(TAG, "Failed to open file for writing: %s", filename.c_str());
                return false;
            }

            // BMP file header (14 bytes)
            uint8_t bmp_header[14] = {
                'B',
                'M', // Signature
                0,
                0,
                0,
                0, // File size (will be filled later)
                0,
                0, // Reserved
                0,
                0, // Reserved
                54,
                0,
                0,
                0 // Offset to pixel data (14 + 40 = 54)
            };

            // BMP DIB header (40 bytes) - BITMAPINFOHEADER
            uint8_t dib_header[40] = {
                40, 0, 0, 0, // DIB header size (40 bytes)
                0,  0, 0, 0, // Width (will be filled)
                0,  0, 0, 0, // Height (will be filled, positive = bottom-up)
                1,  0,       // Color planes (1)
                24, 0,       // Bits per pixel (24 = RGB)
                0,  0, 0, 0, // Compression (0 = none)
                0,  0, 0, 0, // Image size (0 = uncompressed)
                0,  0, 0, 0, // X pixels per meter
                0,  0, 0, 0, // Y pixels per meter
                0,  0, 0, 0, // Colors in palette (0 = default)
                0,  0, 0, 0  // Important colors (0 = all)
            };

            // Fill in width and height (little-endian)
            int32_t width_le = width;
            int32_t height_le = height;
            memcpy(&dib_header[4], &width_le, 4);
            memcpy(&dib_header[8], &height_le, 4);

            // Fill in file size (little-endian)
            memcpy(&bmp_header[2], &file_size, 4);
            memcpy(&dib_header[20], &image_size, 4);

            // Write headers
            if (fwrite(bmp_header, 1, 14, file) != 14 || fwrite(dib_header, 1, 40, file) != 40)
            {
                ESP_LOGE(TAG, "Failed to write BMP headers");
                fclose(file);
                return false;
            }

            // Chunked reading - process 15 rows at a time to minimize memory footprint
            const int32_t CHUNK_ROWS = 15;
            const int32_t chunk_pixel_count = width * CHUNK_ROWS;

            // Allocate buffers for chunk processing
            uint8_t* rgb_chunk_buffer = (uint8_t*)malloc(chunk_pixel_count * 3);
            uint8_t* bmp_row_buffer = (uint8_t*)malloc(row_size);

            if (!rgb_chunk_buffer || !bmp_row_buffer)
            {
                ESP_LOGE(TAG, "Failed to allocate memory for chunked screenshot");
                if (rgb_chunk_buffer)
                    free(rgb_chunk_buffer);
                if (bmp_row_buffer)
                    free(bmp_row_buffer);
                fclose(file);
                return false;
            }

            ESP_LOGI(TAG,
                     "Screenshot using chunked reading (%d rows at a time, buffer size: %d bytes)",
                     CHUNK_ROWS,
                     chunk_pixel_count * 3 + row_size);
            // Process display in chunks from bottom to top (BMP format requirement)
            bool success = true;
            for (int32_t chunk_start_y = height - CHUNK_ROWS; chunk_start_y >= -CHUNK_ROWS + 1; chunk_start_y -= CHUNK_ROWS)
            {
                // Calculate actual chunk boundaries
                int32_t actual_start_y = (chunk_start_y < 0) ? 0 : chunk_start_y;
                int32_t actual_end_y = actual_start_y + CHUNK_ROWS - 1;
                if (actual_end_y >= height)
                    actual_end_y = height - 1;
                int32_t actual_chunk_height = actual_end_y - actual_start_y + 1;

                // Read chunk from display
                hal->display()->readRectRGB(0, actual_start_y, width, actual_chunk_height, rgb_chunk_buffer);

                // Write rows from this chunk in reverse order (bottom-up for BMP)
                for (int32_t chunk_row = actual_chunk_height - 1; chunk_row >= 0; chunk_row--)
                {
                    uint8_t* src_row = rgb_chunk_buffer + (chunk_row * width * 3);

                    // Convert RGB to BGR format and write to row buffer
                    for (int32_t x = 0; x < width; x++)
                    {
                        // BMP uses BGR format
                        bmp_row_buffer[x * 3 + 0] = src_row[x * 3 + 0]; // B
                        bmp_row_buffer[x * 3 + 1] = src_row[x * 3 + 2]; // G
                        bmp_row_buffer[x * 3 + 2] = src_row[x * 3 + 1]; // R
                    }

                    // Pad row to multiple of 4 bytes
                    memset(bmp_row_buffer + width * 3, 0, row_size - width * 3);

                    // Write row to file
                    if (fwrite(bmp_row_buffer, 1, row_size, file) != row_size)
                    {
                        ESP_LOGE(TAG, "Failed to write pixel data at chunk y=%d", chunk_start_y);
                        success = false;
                        break;
                    }
                }

                if (!success)
                    break;
            }

            // Cleanup
            free(bmp_row_buffer);
            free(rgb_chunk_buffer);
            fclose(file);

            if (success)
            {
                ESP_LOGI(TAG, "Screenshot saved: %s", filename.c_str());
            }
            else
            {
                ESP_LOGE(TAG, "Screenshot failed, removing incomplete file");
                remove(filename.c_str());
            }
            // Unmount SD card
            if (!sdcard_mounted)
            {
                hal->sdcard()->eject();
            }

            return success;
        }

        bool check_and_handle_screenshot(HAL::Hal* hal, bool* system_bar_force_update_flag)
        {
            // Check for screenshot key combination: CTRL + SPACE
            bool ctrl_pressed = hal->keyboard()->keysState().ctrl;
            bool space_pressed = hal->keyboard()->keysState().space;

            bool screenshot_combo = ctrl_pressed && space_pressed;

            if (screenshot_combo)
            {
                hal->playKeyboardSound();
                hal->keyboard()->waitForRelease(KEY_NUM_SPACE);
                bool success = take_screenshot(hal);
                // show status
                auto c = hal->canvas_system_bar();
                c->fillScreen(THEME_COLOR_BG);
                int margin_x = 5;
                int margin_y = 4;

                hal->canvas_system_bar()->fillScreen(THEME_COLOR_BG);
                hal->canvas_system_bar()->fillSmoothRoundRect(margin_x,
                                                              margin_y,
                                                              c->width() - margin_x * 2,
                                                              c->height() - margin_y * 2,
                                                              (c->height() - margin_y * 2) / 2,
                                                              success ? TFT_GREENYELLOW : TFT_RED);
                c->setTextColor(success ? TFT_BLACK : TFT_WHITE);
                c->setFont(FONT_16);
                c->drawCenterString(success ? "Screenshot saved" : "Screenshot failed",
                                    c->width() / 2,
                                    (c->height() - 16) / 2 - 1);
                hal->canvas_system_bar_update();
                if (!success)
                {
                    hal->playErrorSound();
                }
                delay(1000);
                if (system_bar_force_update_flag)
                {
                    *system_bar_force_update_flag = true;
                }
                return true;
            }
            return false;
        }

    } // namespace SCREENSHOT_TOOLS
} // namespace UTILS
