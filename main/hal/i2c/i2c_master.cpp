/**
 * @file i2c_master.cpp
 * @brief Modern I2C Master Driver Class
 * @details Uses ESP-IDF 5.x i2c_master API
 * @author d4rkmen
 * @license Apache License 2.0
 */

#include "i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"
#include <cstring>

static const char* TAG = "I2C_Master";

// ============================================================================
// Constructor / Destructor
// ============================================================================

I2CMaster::I2CMaster() : _bus_handle(nullptr), _initialized(false) { init(); }

I2CMaster::~I2CMaster() { deinit(); }

// ============================================================================
// Initialization
// ============================================================================

esp_err_t I2CMaster::init()
{
    if (_initialized)
    {
        ESP_LOGW(TAG, "I2C master already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing I2C master bus...");

    // Configure I2C bus
    i2c_master_bus_config_t bus_config = {};
    memset(&bus_config, 0, sizeof(bus_config));
    bus_config.i2c_port = I2CConfig::BUS_NUM;
    bus_config.sda_io_num = (gpio_num_t)I2CConfig::PIN_SDA;
    bus_config.scl_io_num = (gpio_num_t)I2CConfig::PIN_SCL;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = I2CConfig::ENABLE_PULLUP;

    // Create I2C master bus
    esp_err_t ret = i2c_new_master_bus(&bus_config, &_bus_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
        return ret;
    }

    _initialized = true;
    ESP_LOGI(TAG, "I2C master initialized successfully (SDA:%d, SCL:%d)", I2CConfig::PIN_SDA, I2CConfig::PIN_SCL);
    // scan all devices
    scan_devices();
    return ESP_OK;
}

void I2CMaster::scan_devices()
{
    uint8_t found_devices[128];
    size_t count = 0;
    esp_err_t ret = scan(found_devices, &count, 128);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to scan I2C bus: %s", esp_err_to_name(ret));
    }
}
esp_err_t I2CMaster::deinit()
{
    if (!_initialized)
    {
        return ESP_OK;
    }

    if (_bus_handle)
    {
        esp_err_t ret = i2c_del_master_bus(_bus_handle);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to delete I2C master bus: %s", esp_err_to_name(ret));
            return ret;
        }
        _bus_handle = nullptr;
    }

    _initialized = false;
    ESP_LOGI(TAG, "I2C master deinitialized");
    return ESP_OK;
}

// ============================================================================
// Device Management
// ============================================================================

esp_err_t I2CMaster::add_device(uint8_t device_addr,
                                uint32_t scl_speed_hz,
                                i2c_device_config_t* dev_config,
                                i2c_master_dev_handle_t* dev_handle)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "I2C not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t config;
    if (dev_config)
    {
        config = *dev_config;
    }
    else
    {
        // Use default configuration
        memset(&config, 0, sizeof(config));
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = device_addr;
        config.scl_speed_hz = scl_speed_hz == 0 ? I2CConfig::CLK_SPEED : scl_speed_hz;
    }

    return i2c_master_bus_add_device(_bus_handle, &config, dev_handle);
}

esp_err_t I2CMaster::remove_device(i2c_master_dev_handle_t dev_handle)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "I2C not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_bus_rm_device(dev_handle);
}

// ============================================================================
// Utility Functions
// ============================================================================
esp_err_t I2CMaster::wait_all_done(uint32_t timeout_ms)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "I2C not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (_bus_handle == nullptr)
    {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_bus_wait_all_done(_bus_handle, timeout_ms);
}

esp_err_t I2CMaster::reset()
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "I2C not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_bus_reset(_bus_handle);
}

esp_err_t I2CMaster::probe(uint8_t device_addr)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "I2C not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_probe(_bus_handle, device_addr, I2CConfig::TIMEOUT_MS);
}

esp_err_t I2CMaster::scan(uint8_t* found_devices, size_t* count, size_t max_count)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "I2C not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!found_devices || !count)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *count = 0;
    ESP_LOGI(TAG, "Scanning I2C bus...");

    for (uint8_t addr = 0x08; addr < 0x78; addr++)
    {
        if (i2c_master_probe(_bus_handle, addr, 10) == ESP_OK)
        {
            ESP_LOGI(TAG, "Found device at 0x%02X", addr);
            if (*count < max_count)
            {
                found_devices[*count] = addr;
                (*count)++;
            }
        }
    }

    ESP_LOGI(TAG, "Scan complete. Found %d device(s)", *count);
    return ESP_OK;
}
