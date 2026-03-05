/**
 * @file i2c_master.hpp
 * @brief Modern I2C Master Driver Class
 * @details Uses ESP-IDF 5.x i2c_master API
 * @author d4rkmen
 * @license Apache License 2.0
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>

// ============================================================================
// I2C Bus Configuration
// ============================================================================
namespace I2CConfig
{
    // GPIO pins (from old I2C_Driver)
    constexpr int PIN_SDA = 8;
    constexpr int PIN_SCL = 9;

    // Bus configuration
    constexpr int BUS_NUM = I2C_NUM_1;     // I2C_NUM_1
    constexpr uint32_t CLK_SPEED = 400000; // 400kHz
    constexpr uint32_t TIMEOUT_MS = 1000;  // Transaction timeout

    // Pullup configuration
    constexpr bool ENABLE_PULLUP = true;

    // Bus timing (optional, can be left at defaults)
    constexpr uint32_t SCL_WAIT_US = 0; // 0 = use default
}

// ============================================================================
// I2C Master Driver Class
// ============================================================================
class I2CMaster
{
public:
    I2CMaster();
    ~I2CMaster();

    // Initialization
    esp_err_t init();
    esp_err_t deinit();
    bool is_initialized() const { return _initialized; }

    // Device management (for creating device handles)
    esp_err_t add_device(uint8_t device_addr, uint32_t scl_speed_hz, i2c_device_config_t* dev_config, i2c_master_dev_handle_t* dev_handle);
    esp_err_t remove_device(i2c_master_dev_handle_t dev_handle);

    // Get bus handle (for advanced usage)
    i2c_master_bus_handle_t get_bus_handle() { return _bus_handle; }

    // Utility functions
    esp_err_t reset();
    void scan_devices();
    esp_err_t probe(uint8_t device_addr);
    esp_err_t scan(uint8_t* found_devices, size_t* count, size_t max_count);
    esp_err_t wait_all_done(uint32_t timeout_ms);

private:
    i2c_master_bus_handle_t _bus_handle;
    bool _initialized;
};
