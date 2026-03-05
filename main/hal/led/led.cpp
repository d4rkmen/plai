/**
 * @file led.cpp
 * @brief Standalone LED class implementation for WS2812 RGB LED
 * @version 1.0
 * @date 2024
 *
 */

#include "led.h"
#include "esp_log.h"
#include <cstring>
#include <cmath>

static const char* TAG = "LED";

namespace HAL
{
    // WS2812 timing symbols
    static const rmt_symbol_word_t ws2812_zero = {
        .duration0 = (uint16_t)(0.3 * LED::RMT_RESOLUTION_HZ / 1000000), // T0H=0.3us
        .level0 = 1,
        .duration1 = (uint16_t)(0.9 * LED::RMT_RESOLUTION_HZ / 1000000), // T0L=0.9us
        .level1 = 0,
    };

    static const rmt_symbol_word_t ws2812_one = {
        .duration0 = (uint16_t)(0.9 * LED::RMT_RESOLUTION_HZ / 1000000), // T1H=0.9us
        .level0 = 1,
        .duration1 = (uint16_t)(0.3 * LED::RMT_RESOLUTION_HZ / 1000000), // T1L=0.3us
        .level1 = 0,
    };

    static const rmt_symbol_word_t ws2812_reset = {
        .duration0 = (uint16_t)(LED::RMT_RESOLUTION_HZ / 1000000 * 50 / 2),
        .level0 = 0,
        .duration1 = (uint16_t)(LED::RMT_RESOLUTION_HZ / 1000000 * 50 / 2),
        .level1 = 0,
    };

    // RMT encoder callback for WS2812
    size_t LED::ws2812_encoder_callback(const void* data,
                                        size_t data_size,
                                        size_t symbols_written,
                                        size_t symbols_free,
                                        rmt_symbol_word_t* symbols,
                                        bool* done,
                                        void* arg)
    {
        if (symbols_free < 8)
        {
            return 0;
        }

        size_t data_pos = symbols_written / 8;
        const uint8_t* data_bytes = static_cast<const uint8_t*>(data);

        if (data_pos < data_size)
        {
            // Encode a byte
            size_t symbol_pos = 0;
            for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1)
            {
                if (data_bytes[data_pos] & bitmask)
                {
                    symbols[symbol_pos++] = ws2812_one;
                }
                else
                {
                    symbols[symbol_pos++] = ws2812_zero;
                }
            }
            return symbol_pos;
        }
        else
        {
            // Encode the reset signal
            symbols[0] = ws2812_reset;
            *done = 1;
            return 1;
        }
    }

    // Timer callback
    void LED::timer_callback(void* arg)
    {
        LED* led = static_cast<LED*>(arg);
        if (led)
        {
            led->process_pattern();
        }
    }

    LED::LED(int gpio_num)
        : _initialized(false), _gpio_num(gpio_num), _led_chan(nullptr), _encoder(nullptr), _timer(nullptr), _mutex(nullptr),
          _mode(LEDMode::OFF), _color(), _param1(0), _param2(0), _param3(0), _param4(0), _pattern_step(0), _fade_step(0)
    {
        memset(_led_pixel, 0, sizeof(_led_pixel));
        init();
    }

    LED::~LED()
    {
        if (_initialized)
        {
            deinit();
        }
    }
    bool LED::check_initialized()
    {
        if (!_initialized)
        {
            ESP_LOGW(TAG, "LED not initialized");
            return false;
        }
        return true;
    }

    bool LED::init()
    {
        if (_initialized)
        {
            ESP_LOGW(TAG, "LED already initialized");
            return true;
        }

        // Create mutex
        _mutex = xSemaphoreCreateMutex();
        if (_mutex == nullptr)
        {
            ESP_LOGE(TAG, "Failed to create mutex");
            return false;
        }

        // Create RMT TX channel
        rmt_tx_channel_config_t tx_chan_config = {
            .gpio_num = (gpio_num_t)_gpio_num,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = RMT_RESOLUTION_HZ,
            .mem_block_symbols = 48,
            .trans_queue_depth = 4,
            .flags = {.invert_out = 0, .with_dma = 0, .io_loop_back = 0, .io_od_mode = 0},
        };

        esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &_led_chan);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(ret));
            vSemaphoreDelete(_mutex);
            _mutex = nullptr;
            return false;
        }

        // Create simple encoder
        rmt_simple_encoder_config_t encoder_config = {
            .callback = ws2812_encoder_callback,
            .arg = nullptr,
            .min_chunk_size = 64,
        };

        ret = rmt_new_simple_encoder(&encoder_config, &_encoder);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to create encoder: %s", esp_err_to_name(ret));
            rmt_del_channel(_led_chan);
            vSemaphoreDelete(_mutex);
            _mutex = nullptr;
            return false;
        }

        // Enable RMT channel
        ret = rmt_enable(_led_chan);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(ret));
            rmt_del_encoder(_encoder);
            rmt_del_channel(_led_chan);
            vSemaphoreDelete(_mutex);
            _mutex = nullptr;
            return false;
        }

        // Create timer
        esp_timer_create_args_t timer_args = {
            .callback = timer_callback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "led_timer",
            .skip_unhandled_events = false,
        };

        ret = esp_timer_create(&timer_args, &_timer);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(ret));
            rmt_disable(_led_chan);
            rmt_del_encoder(_encoder);
            rmt_del_channel(_led_chan);
            vSemaphoreDelete(_mutex);
            _mutex = nullptr;
            return false;
        }

        _initialized = true;
        _mode = LEDMode::OFF;
        set_pixel_off();
        update_led();

        ESP_LOGI(TAG, "LED initialized on GPIO %d", _gpio_num);
        return true;
    }

    bool LED::deinit()
    {
        if (!_initialized)
        {
            return true;
        }

        // Stop timer
        if (_timer)
        {
            esp_timer_stop(_timer);
            esp_timer_delete(_timer);
            _timer = nullptr;
        }

        // Turn off LED
        set_pixel_off();
        update_led();

        // Cleanup RMT resources
        if (_led_chan)
        {
            rmt_disable(_led_chan);
            rmt_del_encoder(_encoder);
            rmt_del_channel(_led_chan);
            _led_chan = nullptr;
            _encoder = nullptr;
        }

        // Delete mutex
        if (_mutex)
        {
            vSemaphoreDelete(_mutex);
            _mutex = nullptr;
        }

        _initialized = false;
        ESP_LOGI(TAG, "LED deinitialized");
        return true;
    }

    void LED::update_led()
    {
        if (!check_initialized())
        {
            return;
        }

        rmt_transmit_config_t tx_config = {.loop_count = 0, .flags = {.eot_level = 0, .queue_nonblocking = 0}};

        rmt_transmit(_led_chan, _encoder, _led_pixel, sizeof(_led_pixel), &tx_config);
        rmt_tx_wait_all_done(_led_chan, 100);
    }

    void LED::set_pixel_color(const Color& color)
    {
        // WS2812 uses GRB format
        _led_pixel[0] = color.g;
        _led_pixel[1] = color.r;
        _led_pixel[2] = color.b;
    }

    void LED::set_pixel_off() { memset(_led_pixel, 0, sizeof(_led_pixel)); }

    void LED::stop_timer()
    {
        if (_timer && _initialized)
        {
            esp_timer_stop(_timer);
        }
    }

    void LED::start_timer(uint64_t period_us)
    {
        if (_timer && _initialized)
        {
            esp_timer_stop(_timer);

            esp_timer_start_once(_timer, period_us);
        }
    }

    void LED::process_pattern()
    {
        // Check if mutex is valid
        if (_mutex == nullptr)
        {
            return;
        }

        // Lock mutex to protect shared state
        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
        {
            return;
        }

        switch (_mode)
        {
        case LEDMode::SINGLE_BLINK:
        {
            if (_pattern_step == 0)
            {
                set_pixel_color(_color);
                update_led();
                start_timer(_param1 * 1000); // on_ms
                _pattern_step = 1;
            }
            else
            {
                set_pixel_off();
                update_led();
                stop_timer();
                _mode = LEDMode::OFF;
                _pattern_step = 0;
            }
            break;
        }

        case LEDMode::PERIODIC_BLINK:
        {
            if (_pattern_step == 0)
            {
                // Turn on
                set_pixel_color(_color);
                update_led();
                start_timer(_param1 * 1000); // on_ms
                _pattern_step = 1;
            }
            else
            {
                // Turn off
                set_pixel_off();
                update_led();
                start_timer(_param2 * 1000); // off_ms
                _pattern_step = 0;
            }
            break;
        }

        case LEDMode::DOUBLE_BLINK:
        {
            switch (_pattern_step)
            {
            case 0: // First blink on
                _cur_color = _color;
                set_pixel_color(_cur_color);
                update_led();
                start_timer(_param1 * 1000); // blink_ms
                _pattern_step = 1;
                break;
            case 1: // First blink off (gap)
                set_pixel_off();
                update_led();
                start_timer(_param2 * 1000); // gap_ms
                _pattern_step = 2;
                break;
            case 2: // Second blink on
                set_pixel_color(_cur_color);
                update_led();
                start_timer(_param1 * 1000); // blink_ms
                _pattern_step = 3;
                break;
            case 3: // Second blink off (pause)
                set_pixel_off();
                update_led();
                start_timer(_param4 * 1000); // pause_ms
                _pattern_step = 0;
                break;
            }
            break;
        }

        case LEDMode::FADE:
        {
            uint32_t total_fade_steps = FADE_STEPS * 2 + 1; // fade in + hold + fade out

            if (_fade_step <= FADE_STEPS)
            {
                // Fade in
                float brightness = static_cast<float>(_fade_step) / FADE_STEPS;
                Color faded_color(static_cast<uint8_t>(_color.r * brightness),
                                  static_cast<uint8_t>(_color.g * brightness),
                                  static_cast<uint8_t>(_color.b * brightness));
                set_pixel_color(faded_color);
                update_led();

                if (_fade_step == FADE_STEPS)
                {
                    // At peak, hold
                    start_timer(_param3 * 1000); // hold_ms
                }
                else
                {
                    start_timer((_param1 * 1000) / FADE_STEPS); // fade_in_ms per step
                }
                _fade_step++;
            }
            else if (_fade_step == FADE_STEPS + 1)
            {
                // Done holding, start fade out
                _fade_step++;
                start_timer((_param2 * 1000) / FADE_STEPS); // fade_out_ms per step
            }
            else if (_fade_step < total_fade_steps + 1)
            {
                // Fade out
                uint32_t fade_out_step = _fade_step - FADE_STEPS - 1;
                float brightness = 1.0f - (static_cast<float>(fade_out_step) / FADE_STEPS);
                Color faded_color(static_cast<uint8_t>(_color.r * brightness),
                                  static_cast<uint8_t>(_color.g * brightness),
                                  static_cast<uint8_t>(_color.b * brightness));
                set_pixel_color(faded_color);
                update_led();
                _fade_step++;
            }
            else
            {
                // Complete cycle, restart
                set_pixel_off();
                update_led();
                _fade_step = 0;
            }
            break;
        }

        case LEDMode::CONSTANT:
        case LEDMode::OFF:
        default:
            // Nothing to do
            stop_timer();
            break;
        }

        // Release mutex
        xSemaphoreGive(_mutex);
    }

    bool LED::set_color(const Color& color)
    {
        if (!check_initialized())
        {
            return false;
        }

        // Lock mutex to protect shared state
        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
        {
            return false;
        }

        stop_timer();
        _mode = LEDMode::CONSTANT;
        _color = color;
        set_pixel_color(color);
        update_led();

        // Release mutex
        xSemaphoreGive(_mutex);
        return true;
    }

    bool LED::off()
    {
        if (!check_initialized())
        {
            return false;
        }

        // Lock mutex to protect shared state
        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
        {
            return false;
        }

        stop_timer();
        _mode = LEDMode::OFF;
        set_pixel_off();
        update_led();

        // Release mutex
        xSemaphoreGive(_mutex);
        return true;
    }

    bool LED::blink_once(const Color& color, uint32_t duration_ms)
    {
        if (!check_initialized())
        {
            return false;
        }

        // Lock mutex to protect shared state
        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
        {
            return false;
        }

        stop_timer();
        _mode = LEDMode::SINGLE_BLINK;
        _color = color;
        _param1 = duration_ms;
        _pattern_step = 0;
        start_timer(1000);

        // Release mutex
        xSemaphoreGive(_mutex);
        return true;
    }

    bool LED::blink_periodic(const Color& color, uint32_t on_ms, uint32_t off_ms)
    {
        if (!check_initialized())
        {
            return false;
        }

        // Lock mutex to protect shared state
        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
        {
            return false;
        }

        if (_mode == LEDMode::PERIODIC_BLINK)
        {
            _color = color;
            _param1 = on_ms;
            _param2 = off_ms;
        }
        else
        {
            stop_timer();
            _mode = LEDMode::PERIODIC_BLINK;
            _color = color;
            _param1 = on_ms;
            _param2 = off_ms;
            _pattern_step = 0;
            start_timer(1000);
        }
        // Release mutex
        xSemaphoreGive(_mutex);
        return true;
    }

    bool LED::blink_periodic_double(const Color& color, uint32_t blink_ms, uint32_t gap_ms, uint32_t period_ms)
    {
        if (!check_initialized())
        {
            return false;
        }

        // Calculate pause after second blink
        uint32_t pause_ms = period_ms - (2 * blink_ms + gap_ms);

        // Lock mutex to protect shared state
        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
        {
            return false;
        }

        if (_mode == LEDMode::DOUBLE_BLINK)
        {
            _color = color;
            _param1 = blink_ms;
            _param2 = gap_ms;
            _param3 = period_ms;
            _param4 = pause_ms;
        }
        else
        {
            stop_timer();
            _mode = LEDMode::DOUBLE_BLINK;
            _color = color;
            _param1 = blink_ms;
            _param2 = gap_ms;
            _param3 = period_ms;
            _param4 = pause_ms;
            _pattern_step = 0;
            start_timer(1000);
        }

        // Release mutex
        xSemaphoreGive(_mutex);
        return true;
    }

    bool LED::fade(const Color& color, uint32_t fade_in_ms, uint32_t fade_out_ms, uint32_t hold_ms)
    {
        if (!check_initialized())
        {
            return false;
        }

        // Lock mutex to protect shared state
        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
        {
            return false;
        }

        stop_timer();
        _mode = LEDMode::FADE;
        _color = color;
        _param1 = fade_in_ms;
        _param2 = fade_out_ms;
        _param3 = hold_ms;
        _fade_step = 0;

        // Start fade in
        start_timer((fade_in_ms * 1000) / FADE_STEPS);

        // Release mutex
        xSemaphoreGive(_mutex);
        return true;
    }

    bool LED::stop()
    {
        // off() already has mutex protection
        return off();
    }

} // namespace HAL
