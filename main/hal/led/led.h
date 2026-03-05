/**
 * @file led.h
 * @brief Standalone LED class for WS2812 RGB LED control
 * @version 1.0
 * @date 2024
 *
 */

#ifndef LED_H
#define LED_H

#include "driver/rmt_tx.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>

namespace HAL
{
    /**
     * @brief RGB color structure
     */
    struct Color
    {
        uint8_t r; ///< Red component (0-255)
        uint8_t g; ///< Green component (0-255)
        uint8_t b; ///< Blue component (0-255)

        Color() : r(0), g(0), b(0) {}
        Color(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
    };

    /**
     * @brief LED mode enumeration
     */
    enum class LEDMode
    {
        OFF,
        CONSTANT,
        SINGLE_BLINK,
        PERIODIC_BLINK,
        DOUBLE_BLINK,
        FADE
    };

    /**
     * @brief LED class for controlling WS2812 RGB LED
     */
    class LED
    {
    public:
        LED(int gpio_num = -1);
        ~LED();

        /**
         * @brief Initialize the RGB LED
         * @return true on success, false otherwise
         */
        bool init();

        /**
         * @brief Deinitialize the RGB LED and free resources
         * @return true on success, false otherwise
         */
        bool deinit();

        /**
         * @brief Set LED to constant color
         * @param color RGB color to set
         * @return true on success, false otherwise
         */
        bool set_color(const Color& color);

        /**
         * @brief Turn off the LED
         * @return true on success, false otherwise
         */
        bool off();

        /**
         * @brief Single blink with given color
         * @param color RGB color for the blink
         * @param duration_ms Duration of the blink in milliseconds
         * @return true on success, false otherwise
         */
        bool blink_once(const Color& color, uint32_t duration_ms);

        /**
         * @brief Start periodic single blink pattern
         * @param color RGB color for the blink
         * @param on_ms Duration LED is on in milliseconds
         * @param off_ms Duration LED is off in milliseconds
         * @return true on success, false otherwise
         */
        bool blink_periodic(const Color& color, uint32_t on_ms, uint32_t off_ms);

        /**
         * @brief Start periodic double blink pattern
         * @param color RGB color for the blinks
         * @param blink_ms Duration of each blink in milliseconds
         * @param gap_ms Gap between the two blinks in milliseconds
         * @param period_ms Total period (including pause after second blink) in milliseconds
         * @return true on success, false otherwise
         */
        bool blink_periodic_double(const Color& color, uint32_t blink_ms, uint32_t gap_ms, uint32_t period_ms);

        /**
         * @brief Start periodic fade in/fade out pattern
         * @param color RGB color to fade
         * @param fade_in_ms Duration of fade in in milliseconds
         * @param fade_out_ms Duration of fade out in milliseconds
         * @param hold_ms Duration to hold at full brightness in milliseconds
         * @return true on success, false otherwise
         */
        bool fade(const Color& color, uint32_t fade_in_ms, uint32_t fade_out_ms, uint32_t hold_ms);

        /**
         * @brief Stop any ongoing pattern and turn off LED
         * @return true on success, false otherwise
         */
        bool stop();

    private:
        // RMT encoder callback
        static size_t ws2812_encoder_callback(const void* data,
                                              size_t data_size,
                                              size_t symbols_written,
                                              size_t symbols_free,
                                              rmt_symbol_word_t* symbols,
                                              bool* done,
                                              void* arg);

        // Timer callback
        static void timer_callback(void* arg);

        // Internal helper methods
        void update_led();
        void set_pixel_color(const Color& color);
        void set_pixel_off();
        void process_pattern();
        void stop_timer();
        void start_timer(uint64_t period_us);
        bool check_initialized();

        // State variables
        bool _initialized;
        int _gpio_num;
        rmt_channel_handle_t _led_chan;
        rmt_encoder_handle_t _encoder;
        esp_timer_handle_t _timer;
        SemaphoreHandle_t _mutex; // Mutex for serializing access to shared data

        // Pattern state
        LEDMode _mode;
        Color _color;
        Color _cur_color;
        uint32_t _param1, _param2, _param3, _param4;
        uint32_t _pattern_step;
        uint32_t _fade_step;

        // Pixel buffer (GRB format for WS2812)
        uint8_t _led_pixel[3];

    public:
        // WS2812 timing constants
        static constexpr uint32_t RMT_RESOLUTION_HZ = 10000000; // 10MHz
        static constexpr uint32_t FADE_STEPS = 50;
    };

} // namespace HAL

#endif // LED_H
