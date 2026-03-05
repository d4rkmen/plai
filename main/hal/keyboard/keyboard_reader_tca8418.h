/**
 * @file keyboard_reader_tca8418.h
 * @brief TCA8418 I2C keyboard reader for CARDPUTER_ADV
 * @version 0.2
 * @date 2025-11-09
 *
 * @copyright Copyright (c) 2025
 */
#pragma once
#include "keyboard_reader.h"
#include "tca8418_driver.h"
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include "esp_attr.h"
#include <memory>

// I2C configuration for TCA8418
#define KEYBOARD_I2C_PORT I2C_NUM_1
#define KEYBOARD_I2C_SDA_PIN GPIO_NUM_8
#define KEYBOARD_I2C_SCL_PIN GPIO_NUM_9

// Interrupt pin for TCA8418
#define KEYBOARD_TCA8418_INT_PIN 11

namespace HAL
{
    class Hal;
}

namespace KEYBOARD
{
    /**
     * @brief TCA8418 I2C keyboard reader implementation for CARDPUTER_ADV
     *
     * This implementation uses the TCA8418 I2C keyboard controller to read
     * keyboard state via interrupt-driven events.
     */
    class TCA8418KeyboardReader : public KeyboardReader
    {
    public:
        TCA8418KeyboardReader(HAL::Hal* hal, int interrupt_pin = KEYBOARD_TCA8418_INT_PIN);
        virtual ~TCA8418KeyboardReader();

        void init() override;
        void update() override;
        bool isInitialized() const { return _init_success; }
        inline HAL::Hal* hal() { return _hal; }

    private:
        HAL::Hal* _hal;
        struct KeyEventRaw_t
        {
            bool state; // true = pressed, false = released
            uint8_t row;
            uint8_t col;
        };

        // std::unique_ptr<tca8418_driver> _tca8418;
        tca8418_driver* _tca8418;
        volatile bool _isr_flag;
        int _interrupt_pin;
        // i2c_master_bus_handle_t _bus_handle;
        KeyEventRaw_t _key_event_raw_buffer;
        bool _init_success;

        static void IRAM_ATTR gpio_isr_handler(void* arg);
        KeyEventRaw_t getKeyEventRaw(const uint8_t& eventRaw);
        void remap(KeyEventRaw_t& eventRaw);
        void updateKeyList(const KeyEventRaw_t& eventRaw);
    };

} // namespace KEYBOARD
