/**
 * @file keyboard_reader_tca8418.cpp
 * @brief TCA8418 I2C keyboard reader implementation for CARDPUTER_ADV
 * @version 0.3
 * @date 2025-11-09
 *
 * @copyright Copyright (c) 2025
 */
#include "keyboard_reader_tca8418.h"
#include "esp_log.h"
#include <algorithm>

#define TAG "KB_TCA8418"

namespace KEYBOARD
{
    TCA8418KeyboardReader::TCA8418KeyboardReader(HAL::Hal* hal, int interrupt_pin)
        : _hal(hal), _isr_flag(false), _interrupt_pin(interrupt_pin), _init_success(false)
    {
    }

    TCA8418KeyboardReader::~TCA8418KeyboardReader()
    {
        ESP_LOGI(TAG, "Destructor, init_success: %d", _init_success);
        if (!_init_success)
        {
            return;
        }

        if (_interrupt_pin >= 0)
        {
            gpio_isr_handler_remove((gpio_num_t)_interrupt_pin);
        }

        // Clean up I2C bus handle
#if 0        
        if (_bus_handle != nullptr)
        {
            esp_err_t ret = i2c_del_master_bus(_bus_handle);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to delete I2C master bus: %s", esp_err_to_name(ret));
            }
            _bus_handle = nullptr;
        }
#endif
        if (_tca8418 != nullptr)
        {
            delete _tca8418;
            _tca8418 = nullptr;
        }
    }

    void IRAM_ATTR TCA8418KeyboardReader::gpio_isr_handler(void* arg)
    {
        TCA8418KeyboardReader* reader = static_cast<TCA8418KeyboardReader*>(arg);
        reader->_isr_flag = true;
        if (reader->_notify_task)
        {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xTaskNotifyFromISR(reader->_notify_task, 1, eSetBits, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }

    void TCA8418KeyboardReader::init()
    {
#if 0        
        // Create I2C master bus
        i2c_master_bus_config_t bus_config = {};
        bus_config.i2c_port = KEYBOARD_I2C_PORT;
        bus_config.sda_io_num = (gpio_num_t)KEYBOARD_I2C_SDA_PIN;
        bus_config.scl_io_num = (gpio_num_t)KEYBOARD_I2C_SCL_PIN;
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.flags.enable_internal_pullup = true;

        esp_err_t ret = i2c_new_master_bus(&bus_config, &_bus_handle);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
            _init_success = false;
            return;
        }
#endif
        // Initialize TCA8418 driver
        // _tca8418 = std::make_unique<tca8418_driver>(_hal);
        _tca8418 = new tca8418_driver(_hal);
        if (!_tca8418)
        {
            ESP_LOGI(TAG, "Failed to create TCO8418 driver");
            return;
        }

        if (!_tca8418->begin())
        {
            _init_success = false;
            return;
        }

        // Configure matrix: 7 rows x 8 columns (based on TCA8418 reference)
        _tca8418->set_matrix(7, 8);
        _tca8418->flush();

        // Setup interrupt pin
        if (_interrupt_pin >= 0)
        {
            gpio_reset_pin((gpio_num_t)_interrupt_pin);
            gpio_set_direction((gpio_num_t)_interrupt_pin, GPIO_MODE_INPUT);
            gpio_set_intr_type((gpio_num_t)_interrupt_pin, GPIO_INTR_ANYEDGE);

            // Install GPIO ISR service (if not already installed)
            esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
            {
                ESP_LOGE(TAG, "Failed to install GPIO ISR service: %d", ret);
                return;
            }
            if (ret == ESP_ERR_INVALID_STATE)
            {
                ESP_LOGD(TAG, "GPIO ISR service already installed");
            }

            ret = gpio_isr_handler_add((gpio_num_t)_interrupt_pin, gpio_isr_handler, this);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to add keyboard ISR handler: %d", ret);
                return;
            }
            ESP_LOGI(TAG, "Interrupt pin %d installed", _interrupt_pin);
        }

        // Enable interrupts
        _tca8418->enable_interrupts();

        _init_success = true;
    }

    void TCA8418KeyboardReader::update()
    {
        if (!_isr_flag)
        {
            return;
        }
        uint8_t int_stat = 0;
        if (!_tca8418->read_register(TCA8418_REG_INT_STAT, &int_stat))
            return;
        // Check if key event interrupt is set
        if (int_stat & TCA8418_REG_INT_STAT_K_INT)
        {
            // get number of events
            while (_tca8418->available() > 0)
            {
                // Get the key event
                uint8_t event_raw = _tca8418->get_event();

                // Skip if no event
                if (event_raw == 0)
                    break;

                _key_event_raw_buffer = getKeyEventRaw(event_raw);

                // Remap to match CARDPUTER coordinate system
                remap(_key_event_raw_buffer);

                // Update the key list
                updateKeyList(_key_event_raw_buffer);
            }
            // Clear the IRQ flag
            _tca8418->write_register(TCA8418_REG_INT_STAT, TCA8418_REG_INT_STAT_K_INT);
        }
        _isr_flag = false;
    }

    TCA8418KeyboardReader::KeyEventRaw_t TCA8418KeyboardReader::getKeyEventRaw(const uint8_t& eventRaw)
    {
        KeyEventRaw_t ret;
        ret.state = eventRaw & 0x80; // Bit 7: 1 = pressed, 0 = released

        uint16_t buffer = eventRaw;
        buffer &= 0x7F; // Lower 7 bits = key code
        buffer--;       // Key codes are 1-based

        ret.row = buffer / 10;
        ret.col = buffer % 10;

        return ret;
    }

    void TCA8418KeyboardReader::remap(KeyEventRaw_t& key)
    {
        // Remap to the same coordinate system as CARDPUTER
        // This mapping is based on the TCA8418.cpp implementation

        // Col
        uint8_t col = key.row * 2;
        if (key.col > 3)
            col++;

        // Row
        uint8_t row = (key.col + 4) % 4;

        key.row = row;
        key.col = col;
    }

    void TCA8418KeyboardReader::updateKeyList(const KeyEventRaw_t& eventRaw)
    {
        Point2D_t point;
        point.x = eventRaw.col;
        point.y = eventRaw.row;

        // Add or remove the key from the list based on state
        if (eventRaw.state)
        {
            // Key pressed - add if not already in list
            auto it = std::find(_key_list.begin(), _key_list.end(), point);
            if (it == _key_list.end())
            {
                _key_list.push_back(point);
            }
        }
        else
        {
            // Key released - remove from list
            auto it = std::find(_key_list.begin(), _key_list.end(), point);
            if (it != _key_list.end())
            {
                _key_list.erase(it);
            }
        }
    }

} // namespace KEYBOARD
