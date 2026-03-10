/**
 * @file sx1262.cpp
 * @author d4rkmen
 * @brief SX1262 LoRa radio driver implementation
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "sx1262.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "SX1262";

// SX1262 Commands
#define SX1262_CMD_SET_SLEEP 0x84
#define SX1262_CMD_SET_STANDBY 0x80
#define SX1262_CMD_SET_FS 0xC1
#define SX1262_CMD_SET_TX 0x83
#define SX1262_CMD_SET_RX 0x82
#define SX1262_CMD_STOP_TIMER_ON_PREAMBLE 0x9F
#define SX1262_CMD_SET_CAD 0xC5
#define SX1262_CMD_SET_TX_CONTINUOUS 0xD1
#define SX1262_CMD_SET_TX_INFINITE_PREAMBLE 0xD2
#define SX1262_CMD_SET_REGULATOR_MODE 0x96
#define SX1262_CMD_CALIBRATE 0x89
#define SX1262_CMD_CALIBRATE_IMAGE 0x98
#define SX1262_CMD_SET_PA_CONFIG 0x95
#define SX1262_CMD_SET_RX_TX_FALLBACK 0x93

// Register and buffer access
#define SX1262_CMD_WRITE_REGISTER 0x0D
#define SX1262_CMD_READ_REGISTER 0x1D
#define SX1262_CMD_WRITE_BUFFER 0x0E
#define SX1262_CMD_READ_BUFFER 0x1E

// DIO and IRQ control
#define SX1262_CMD_SET_DIO_IRQ_PARAMS 0x08
#define SX1262_CMD_GET_IRQ_STATUS 0x12
#define SX1262_CMD_CLR_IRQ_STATUS 0x02
#define SX1262_CMD_SET_DIO2_AS_RF_SW 0x9D
#define SX1262_CMD_SET_DIO3_AS_TCXO 0x97

// RF and packet config
#define SX1262_CMD_SET_RF_FREQUENCY 0x86
#define SX1262_CMD_SET_PACKET_TYPE 0x8A
#define SX1262_CMD_GET_PACKET_TYPE 0x11
#define SX1262_CMD_SET_TX_PARAMS 0x8E
#define SX1262_CMD_SET_MODULATION_PARAMS 0x8B
#define SX1262_CMD_SET_PACKET_PARAMS 0x8C
#define SX1262_CMD_SET_CAD_PARAMS 0x88
#define SX1262_CMD_SET_BUFFER_BASE_ADDR 0x8F
#define SX1262_CMD_SET_LORA_SYMB_TIMEOUT 0xA0

// Status
#define SX1262_CMD_GET_STATUS 0xC0
#define SX1262_CMD_GET_RX_BUFFER_STATUS 0x13
#define SX1262_CMD_GET_PACKET_STATUS 0x14
#define SX1262_CMD_GET_RSSI_INST 0x15
#define SX1262_CMD_GET_STATS 0x10
#define SX1262_CMD_RESET_STATS 0x00
#define SX1262_CMD_GET_DEVICE_ERRORS 0x17
#define SX1262_CMD_CLR_DEVICE_ERRORS 0x07

// IRQ masks
#define SX1262_IRQ_TX_DONE (1 << 0)
#define SX1262_IRQ_RX_DONE (1 << 1)
#define SX1262_IRQ_PREAMBLE_DETECTED (1 << 2)
#define SX1262_IRQ_SYNC_WORD_VALID (1 << 3)
#define SX1262_IRQ_HEADER_VALID (1 << 4)
#define SX1262_IRQ_HEADER_ERR (1 << 5)
#define SX1262_IRQ_CRC_ERR (1 << 6)
#define SX1262_IRQ_CAD_DONE (1 << 7)
#define SX1262_IRQ_CAD_DETECTED (1 << 8)
#define SX1262_IRQ_TIMEOUT (1 << 9)
#define SX1262_IRQ_ALL 0x03FF

// Standby modes
#define SX1262_STANDBY_RC 0x00
#define SX1262_STANDBY_XOSC 0x01

// Packet types
#define SX1262_PACKET_TYPE_GFSK 0x00
#define SX1262_PACKET_TYPE_LORA 0x01

// Bandwidth values
#define SX1262_LORA_BW_7_8 0x00
#define SX1262_LORA_BW_10_4 0x08
#define SX1262_LORA_BW_15_6 0x01
#define SX1262_LORA_BW_20_8 0x09
#define SX1262_LORA_BW_31_25 0x02
#define SX1262_LORA_BW_41_7 0x0A
#define SX1262_LORA_BW_62_5 0x03
#define SX1262_LORA_BW_125 0x04
#define SX1262_LORA_BW_250 0x05
#define SX1262_LORA_BW_500 0x06

// TX ramp times
#define SX1262_RAMP_10_US 0x00
#define SX1262_RAMP_20_US 0x01
#define SX1262_RAMP_40_US 0x02
#define SX1262_RAMP_80_US 0x03
#define SX1262_RAMP_200_US 0x04
#define SX1262_RAMP_800_US 0x05
#define SX1262_RAMP_1700_US 0x06
#define SX1262_RAMP_3400_US 0x07

// Regulator modes
#define SX1262_REG_MODE_LDO 0x00
#define SX1262_REG_MODE_DCDC 0x01

// RX/TX fallback modes
#define SX1262_RX_TX_FALLBACK_STDBY_RC 0x20

// Calibration masks
#define SX1262_CALIBRATE_ALL 0x7F

// TCXO voltage
#define SX1262_TCXO_1_6V 0x00
#define SX1262_TCXO_1_7V 0x01
#define SX1262_TCXO_1_8V 0x02
#define SX1262_TCXO_2_2V 0x03
#define SX1262_TCXO_2_4V 0x04
#define SX1262_TCXO_2_7V 0x05
#define SX1262_TCXO_3_0V 0x06
#define SX1262_TCXO_3_3V 0x07

namespace HAL
{

    SX1262::SX1262(const SX1262Pins& pins)
        : _pins(pins), _spi_handle(nullptr), _spi_mutex(nullptr), _mode(RadioMode::SLEEP), _state(SX1262State::UNINITIALIZED),
          _event_callback(nullptr), _irq_pending(false), _last_rssi(0), _last_snr(0.0f), _last_rx_len(0), _rx_buffer_ptr(0),
          _initialized(false)
    {
        // Initialize default config
        _config.frequency_hz = 915000000; // 915 MHz
        _config.bandwidth_hz = 250000;
        _config.spreading_factor = 9;
        _config.coding_rate = 5;
        _config.tx_power_dbm = 22;
        _config.preamble_length = 8; // 16;
        _config.sync_word = 0x2B;
        _config.crc_enabled = true;
        _config.implicit_header = false;
        _config.iq_inverted = false;
    }

    SX1262::~SX1262()
    {
        if (_initialized)
        {
            deinit();
        }
    }

    bool SX1262::init()
    {
        ESP_LOGI(TAG, "Initializing SX1262");

        // Create SPI mutex
        _spi_mutex = xSemaphoreCreateMutex();
        if (_spi_mutex == nullptr)
        {
            ESP_LOGE(TAG, "Failed to create SPI mutex");
            return false;
        }

        // Initialize SPI
        if (!spiInit())
        {
            ESP_LOGE(TAG, "Failed to initialize SPI");
            return false;
        }

        // Configure GPIO pins
        gpio_config_t io_conf = {};

        // RST pin (output)
        io_conf.pin_bit_mask = (1ULL << _pins.rst);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);
        gpio_set_level((gpio_num_t)_pins.rst, 1);

        // BUSY pin (input)
        io_conf.pin_bit_mask = (1ULL << _pins.busy);
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);

        // DIO1 pin (input with interrupt)
        io_conf.pin_bit_mask = (1ULL << _pins.dio1);
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.intr_type = GPIO_INTR_POSEDGE;
        gpio_config(&io_conf);

        // Install GPIO ISR service (if not already installed)
        esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGE(TAG, "Failed to install GPIO ISR service: %d", ret);
            return false;
        }
        if (ret == ESP_ERR_INVALID_STATE)
        {
            ESP_LOGD(TAG, "GPIO ISR service already installed");
        }

        // Add ISR handler for DIO1 pin
        ret = gpio_isr_handler_add((gpio_num_t)_pins.dio1, dio1_isr_handler, this);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to add DIO1 ISR handler: %d", ret);
            return false;
        }

        // Configure antenna switch pins if used
        if (_pins.rxen >= 0)
        {
            io_conf.pin_bit_mask = (1ULL << _pins.rxen);
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.intr_type = GPIO_INTR_DISABLE;
            gpio_config(&io_conf);
            gpio_set_level((gpio_num_t)_pins.rxen, 0);
        }
        if (_pins.txen >= 0)
        {
            io_conf.pin_bit_mask = (1ULL << _pins.txen);
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.intr_type = GPIO_INTR_DISABLE;
            gpio_config(&io_conf);
            gpio_set_level((gpio_num_t)_pins.txen, 0);
        }

        // Reset the chip
        reset();

        // Read chip version to verify SPI communication works
        uint8_t chip_version = getChipVersion();
        if (chip_version == 0xFF)
        {
            ESP_LOGE(TAG, "Failed to read chip version - SPI communication failed");
            return false;
        }
        ESP_LOGI(TAG, "SX1262 chip version: 0x%02X", chip_version);

        // Verify it's a valid SX1262 (version should be 0x00 or 0x01)
        if (chip_version > 0x01)
        {
            ESP_LOGW(TAG, "Unexpected chip version 0x%02X (expected 0x00 or 0x01)", chip_version);
        }

        // Check if chip is present
        if (!isChipPresent())
        {
            ESP_LOGE(TAG, "SX1262 not detected");
            return false;
        }

        ESP_LOGI(TAG, "SX1262 detected, status: 0x%02X", getStatus());

        // Basic initialization sequence
        setStandby(true); // RC oscillator standby
        waitBusy();

        // Use DC-DC regulator for better efficiency
        setRegulatorMode(true);
        waitBusy();

        // Enable TCXO on DIO3 at 1.8V (timeout in 15.625 us units)
        setDio3AsTcxoCtrl(SX1262_TCXO_1_8V, 640); // 10 ms
        waitBusy();

        // Set DIO2 as RF switch control (if using internal switch)
        if (_pins.rxen < 0 && _pins.txen < 0)
        {
            setDio2AsRfSwitch(true);
            waitBusy();
        }

        // Set packet type to LoRa
        setPacketType(true);
        waitBusy();

        // Set buffer base addresses
        setBufferBaseAddress(0x00, 0x80);
        waitBusy();

        // Set RX/TX fallback to standby RC
        setRxTxFallback(SX1262_RX_TX_FALLBACK_STDBY_RC);
        waitBusy();

        // Calibrate all blocks
        calibrate(SX1262_CALIBRATE_ALL);
        waitBusy();

        // Apply default configuration
        if (!setConfig(_config))
        {
            ESP_LOGE(TAG, "Failed to apply default config");
            return false;
        }

        // Configure IRQ: route all to DIO1
        setDioIrqParams(SX1262_IRQ_ALL, SX1262_IRQ_ALL, 0, 0);
        waitBusy();

        // Clear any pending IRQ
        clearIrqStatus(SX1262_IRQ_ALL);

        _state = SX1262State::STANDBY_RC;
        _mode = RadioMode::STANDBY;
        _initialized = true;

        ESP_LOGI(TAG, "SX1262 initialized successfully");
        return true;
    }

    void SX1262::deinit()
    {
        if (!_initialized)
            return;

        ESP_LOGI(TAG, "Deinitializing SX1262");

        // Put chip to sleep
        setSleep(true);

        // Remove ISR handler
        gpio_isr_handler_remove((gpio_num_t)_pins.dio1);

        // Deinitialize SPI
        spiDeinit();

        // Delete mutex
        if (_spi_mutex)
        {
            vSemaphoreDelete(_spi_mutex);
            _spi_mutex = nullptr;
        }

        _initialized = false;
        _state = SX1262State::UNINITIALIZED;
    }

    bool SX1262::spiInit()
    {
        // Initialize SPI bus if not already initialized (e.g., by SD card)
        // Try to initialize the bus - if it's already initialized, we'll get ESP_ERR_INVALID_STATE
        spi_bus_config_t bus_cfg = {};
        bus_cfg.mosi_io_num = _pins.mosi;
        bus_cfg.miso_io_num = _pins.miso;
        bus_cfg.sclk_io_num = _pins.sck;
        bus_cfg.quadwp_io_num = -1;
        bus_cfg.quadhd_io_num = -1;
        bus_cfg.data4_io_num = -1;
        bus_cfg.data5_io_num = -1;
        bus_cfg.data6_io_num = -1;
        bus_cfg.data7_io_num = -1;
        bus_cfg.max_transfer_sz = 512;
        bus_cfg.flags = SPICOMMON_BUSFLAG_SCLK | SPICOMMON_BUSFLAG_MOSI | SPICOMMON_BUSFLAG_MISO;
        bus_cfg.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO;
        bus_cfg.intr_flags = 0;

        esp_err_t ret = spi_bus_initialize(_pins.spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
            return false;
        }
        if (ret == ESP_ERR_INVALID_STATE)
        {
            ESP_LOGD(TAG, "SPI bus already initialized (likely by SD card)");
        }
        else
        {
            ESP_LOGD(TAG, "SPI bus initialized for radio");
        }

        // Configure SPI device
        spi_device_interface_config_t devcfg = {};
        devcfg.clock_speed_hz = 10000000; // 10 MHz
        devcfg.mode = 0;                  // SPI mode 0
        devcfg.spics_io_num = -1;         // Manual CS control
        devcfg.queue_size = 1;
        devcfg.pre_cb = nullptr;
        devcfg.post_cb = nullptr;

        // Configure CS pin manually
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << _pins.cs);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);
        gpio_set_level((gpio_num_t)_pins.cs, 1); // CS inactive

        ret = spi_bus_add_device(_pins.spi_host, &devcfg, &_spi_handle);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
            return false;
        }

        return true;
    }

    void SX1262::spiDeinit()
    {
        if (_spi_handle)
        {
            esp_err_t ret = spi_bus_remove_device(_spi_handle);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to remove SPI device: %s", esp_err_to_name(ret));
            }
            _spi_handle = nullptr;
        }
    }

    void SX1262::setCS(bool active) { gpio_set_level((gpio_num_t)_pins.cs, active ? 0 : 1); }

    bool SX1262::spiTransfer(const uint8_t* tx, uint8_t* rx, size_t len)
    {
        if (!_spi_handle || len == 0)
        {
            ESP_LOGE(TAG, "spiTransfer: invalid parameters (handle=%p, len=%lu)", _spi_handle, len);
            return false;
        }

        spi_transaction_t trans = {};
        trans.length = len * 8;
        trans.tx_buffer = tx;
        trans.rx_buffer = rx;
        // acquire spi bus lock for 5 seconds
        if (xSemaphoreTake(_spi_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to take SPI mutex");
            return false;
        }

        setCS(true);
        waitBusy(100);

        esp_err_t ret = spi_device_polling_transmit(_spi_handle, &trans);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "SPI transfer failed: %s", esp_err_to_name(ret));
        }

        setCS(false);
        xSemaphoreGive(_spi_mutex);
        return ret == ESP_OK;
    }

    void SX1262::writeCommand(uint8_t cmd, const uint8_t* data, size_t len)
    {
        uint8_t tx[256];
        uint8_t rx[256];

        tx[0] = cmd;
        if (data && len > 0)
        {
            memcpy(&tx[1], data, len);
        }

        if (!spiTransfer(tx, rx, 1 + len))
        {
            ESP_LOGE(TAG, "writeCommand failed: cmd=0x%02X", cmd);
        }
    }

    void SX1262::readCommand(uint8_t cmd, uint8_t* data, size_t len)
    {
        uint8_t tx[256] = {0};
        uint8_t rx[256] = {0};

        tx[0] = cmd;
        tx[1] = 0; // NOP for status

        if (!spiTransfer(tx, rx, 2 + len))
        {
            ESP_LOGE(TAG, "readCommand failed: cmd=0x%02X", cmd);
            if (data && len > 0)
            {
                memset(data, 0, len); // Zero out data on error
            }
            return;
        }

        if (data && len > 0)
        {
            memcpy(data, &rx[2], len);
        }
    }

    void SX1262::writeRegister(uint16_t addr, const uint8_t* data, size_t len)
    {
        uint8_t tx[256];
        uint8_t rx[256];

        tx[0] = SX1262_CMD_WRITE_REGISTER;
        tx[1] = (addr >> 8) & 0xFF;
        tx[2] = addr & 0xFF;
        if (data && len > 0)
        {
            memcpy(&tx[3], data, len);
        }

        if (!spiTransfer(tx, rx, 3 + len))
        {
            ESP_LOGE(TAG, "writeRegister failed: addr=0x%04X", addr);
        }
    }

    void SX1262::readRegister(uint16_t addr, uint8_t* data, size_t len)
    {
        uint8_t tx[256] = {0};
        uint8_t rx[256] = {0};

        tx[0] = SX1262_CMD_READ_REGISTER;
        tx[1] = (addr >> 8) & 0xFF;
        tx[2] = addr & 0xFF;
        tx[3] = 0; // NOP

        if (!spiTransfer(tx, rx, 4 + len))
        {
            ESP_LOGE(TAG, "readRegister failed: addr=0x%04X", addr);
            if (data && len > 0)
            {
                memset(data, 0, len); // Zero out data on error
            }
            return;
        }

        if (data && len > 0)
        {
            memcpy(data, &rx[4], len);
        }
    }

    void SX1262::writeBuffer(uint8_t offset, const uint8_t* data, size_t len)
    {
        uint8_t tx[256];
        uint8_t rx[256];

        tx[0] = SX1262_CMD_WRITE_BUFFER;
        tx[1] = offset;
        if (data && len > 0)
        {
            memcpy(&tx[2], data, len);
        }

        if (!spiTransfer(tx, rx, 2 + len))
        {
            ESP_LOGE(TAG, "writeBuffer failed: offset=0x%02X, len=%lu", offset, len);
        }
    }

    void SX1262::readBuffer(uint8_t offset, uint8_t* data, size_t len)
    {
        uint8_t tx[256] = {0};
        uint8_t rx[256] = {0};

        tx[0] = SX1262_CMD_READ_BUFFER;
        tx[1] = offset;
        tx[2] = 0; // NOP

        if (!spiTransfer(tx, rx, 3 + len))
        {
            ESP_LOGE(TAG, "readBuffer failed: offset=0x%02X, len=%lu", offset, len);
            if (data && len > 0)
            {
                memset(data, 0, len); // Zero out data on error
            }
            return;
        }

        if (data && len > 0)
        {
            memcpy(data, &rx[3], len);
        }
    }

    void SX1262::reset()
    {
        ESP_LOGI(TAG, "Resetting SX1262");
        gpio_set_level((gpio_num_t)_pins.rst, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)_pins.rst, 1);
        vTaskDelay(pdMS_TO_TICKS(20));
        waitBusy();
    }

    void SX1262::waitBusy(uint32_t timeout_ms)
    {
        uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;
        while (gpio_get_level((gpio_num_t)_pins.busy))
        {
            if ((xTaskGetTickCount() * portTICK_PERIOD_MS - start) > timeout_ms)
            {
                ESP_LOGW(TAG, "Busy timeout");
                break;
            }
            vTaskDelay(1);
        }
    }

    bool SX1262::isChipPresent()
    {
        // Try reading a register that should have a known value
        uint8_t status = getStatus();
        // Status should not be 0x00 or 0xFF if chip is present
        return (status != 0x00 && status != 0xFF);
    }

    uint8_t SX1262::getStatus()
    {
        uint8_t tx[2] = {SX1262_CMD_GET_STATUS, 0};
        uint8_t rx[2] = {0};
        if (!spiTransfer(tx, rx, 2))
        {
            ESP_LOGE(TAG, "getStatus failed");
            return 0xFF; // Return invalid status on error
        }
        uint8_t status = rx[1];
        // Decode status: mode=0x%02X cmd_status=0x%02X (mode: %s, cmd: %s)
        const char* mode_str = "Unused";
        switch (status >> 4 & 0x07)
        {
        case 0x01:
            mode_str = "Reserved";
            break;
        case 0x02:
            mode_str = "STBY_RC";
            break;
        case 0x03:
            mode_str = "STBY_XOSC";
            break;
        case 0x04:
            mode_str = "FS";
            break;
        case 0x05:
            mode_str = "RX";
            break;
        case 0x06:
            mode_str = "TX";
            break;
        }
        const char* cmd_str = "Reserved";
        switch ((status >> 1) & 0x07)
        {
        case 0x01:
            cmd_str = "Reserved";
        case 0x02:
            cmd_str = "DataAvail";
            break;
        case 0x03:
            cmd_str = "Timeout";
            break;
        case 0x04:
            cmd_str = "ProcError";
            break;
        case 0x05:
            cmd_str = "ExecFail";
            break;
        case 0x06:
            cmd_str = "TxDone";
            break;
        }
        ESP_LOGI(TAG, "Status: 0x%02X (mode=%s, cmd=%s)", status, mode_str, cmd_str);
        return status;
    }

    uint8_t SX1262::getChipVersion()
    {
        // Read chip version from register 0x0154
        // SX1262 should return 0x00 or 0x01
        // We read it directly using readRegister to verify SPI communication works
        uint8_t version = 0xFF; // Initialize to invalid value
        uint8_t tx[5] = {0};
        uint8_t rx[5] = {0};

        // Read register 0x0154 directly via SPI
        tx[0] = SX1262_CMD_READ_REGISTER;
        tx[1] = 0x01; // Address high byte
        tx[2] = 0x54; // Address low byte
        tx[3] = 0;    // NOP

        if (!spiTransfer(tx, rx, 5))
        {
            ESP_LOGE(TAG, "getChipVersion: SPI transfer failed");
            return 0xFF;
        }

        version = rx[4]; // Version is in the 5th byte

        // Verify SPI communication by reading again
        memset(tx, 0, sizeof(tx));
        memset(rx, 0, sizeof(rx));
        tx[0] = SX1262_CMD_READ_REGISTER;
        tx[1] = 0x01;
        tx[2] = 0x54;
        tx[3] = 0;

        if (!spiTransfer(tx, rx, 5))
        {
            ESP_LOGE(TAG, "getChipVersion: Second SPI transfer failed");
            return 0xFF;
        }

        uint8_t version2 = rx[4];

        if (version != version2)
        {
            ESP_LOGE(TAG, "Chip version read inconsistent: 0x%02X != 0x%02X", version, version2);
            return 0xFF;
        }

        ESP_LOGD(TAG, "Chip version read successfully: 0x%02X", version);
        return version;
    }

    void SX1262::setStandby(bool xosc)
    {
        uint8_t mode = xosc ? SX1262_STANDBY_XOSC : SX1262_STANDBY_RC;
        writeCommand(SX1262_CMD_SET_STANDBY, &mode, 1);
        _state = xosc ? SX1262State::STANDBY_XOSC : SX1262State::STANDBY_RC;
    }

    void SX1262::setSleep(bool warm_start)
    {
        uint8_t config = warm_start ? 0x04 : 0x00; // Warm start retains config
        writeCommand(SX1262_CMD_SET_SLEEP, &config, 1);
        _state = SX1262State::SLEEP;
    }

    void SX1262::setPacketType(bool lora)
    {
        uint8_t type = lora ? SX1262_PACKET_TYPE_LORA : SX1262_PACKET_TYPE_GFSK;
        writeCommand(SX1262_CMD_SET_PACKET_TYPE, &type, 1);
    }

    void SX1262::setRfFrequency(uint32_t freq_hz)
    {
        // Convert frequency to register value
        // RF_Freq = (Freq * 2^25) / 32000000
        uint32_t freq_reg = (uint32_t)((static_cast<uint64_t>(freq_hz) << 25) / 32000000ULL);

        uint8_t data[4] = {(uint8_t)((freq_reg >> 24) & 0xFF),
                           (uint8_t)((freq_reg >> 16) & 0xFF),
                           (uint8_t)((freq_reg >> 8) & 0xFF),
                           (uint8_t)(freq_reg & 0xFF)};
        writeCommand(SX1262_CMD_SET_RF_FREQUENCY, data, 4);
    }

    void SX1262::setPaConfig(int8_t power_dbm)
    {
        // SX1262 high power PA configuration
        // paDutyCycle, hpMax, deviceSel, paLut
        uint8_t data[4];
        if (power_dbm >= 20)
        {
            data[0] = 0x04; // paDutyCycle
            data[1] = 0x07; // hpMax
            data[2] = 0x00; // deviceSel = SX1262
            data[3] = 0x01; // paLut
        }
        else if (power_dbm >= 14)
        {
            data[0] = 0x03;
            data[1] = 0x05;
            data[2] = 0x00;
            data[3] = 0x01;
        }
        else
        {
            data[0] = 0x02;
            data[1] = 0x03;
            data[2] = 0x00;
            data[3] = 0x01;
        }
        writeCommand(SX1262_CMD_SET_PA_CONFIG, data, 4);
    }

    void SX1262::setTxParams(int8_t power_dbm, uint8_t ramp_time)
    {
        // Clamp power to valid range (-9 to +22 dBm for SX1262)
        if (power_dbm < -9)
            power_dbm = -9;
        if (power_dbm > 22)
            power_dbm = 22;

        uint8_t data[2] = {(uint8_t)power_dbm, ramp_time};
        writeCommand(SX1262_CMD_SET_TX_PARAMS, data, 2);
    }

    void SX1262::setModulationParams(uint8_t sf, uint8_t bw, uint8_t cr, bool low_data_rate)
    {
        uint8_t data[4] = {sf, bw, cr, (uint8_t)(low_data_rate ? 0x01 : 0x00)};
        writeCommand(SX1262_CMD_SET_MODULATION_PARAMS, data, 4);
    }

    void SX1262::setPacketParams(uint16_t preamble, bool implicit, uint8_t payload_len, bool crc, bool invert_iq)
    {
        // LoRa packet params are 6 bytes (preamble, header, payload, crc, iq)
        uint8_t data[6] = {(uint8_t)((preamble >> 8) & 0xFF),
                           (uint8_t)(preamble & 0xFF),
                           (uint8_t)(implicit ? 0x01 : 0x00),
                           payload_len,
                           (uint8_t)(crc ? 0x01 : 0x00),
                           (uint8_t)(invert_iq ? 0x01 : 0x00)};
        writeCommand(SX1262_CMD_SET_PACKET_PARAMS, data, sizeof(data));
    }

    void SX1262::setBufferBaseAddress(uint8_t tx_base, uint8_t rx_base)
    {
        uint8_t data[2] = {tx_base, rx_base};
        writeCommand(SX1262_CMD_SET_BUFFER_BASE_ADDR, data, 2);
    }

    void SX1262::setDioIrqParams(uint16_t irq_mask, uint16_t dio1_mask, uint16_t dio2_mask, uint16_t dio3_mask)
    {
        uint8_t data[8] = {(uint8_t)((irq_mask >> 8) & 0xFF),
                           (uint8_t)(irq_mask & 0xFF),
                           (uint8_t)((dio1_mask >> 8) & 0xFF),
                           (uint8_t)(dio1_mask & 0xFF),
                           (uint8_t)((dio2_mask >> 8) & 0xFF),
                           (uint8_t)(dio2_mask & 0xFF),
                           (uint8_t)((dio3_mask >> 8) & 0xFF),
                           (uint8_t)(dio3_mask & 0xFF)};
        writeCommand(SX1262_CMD_SET_DIO_IRQ_PARAMS, data, 8);
    }

    void SX1262::clearIrqStatus(uint16_t mask)
    {
        uint8_t data[2] = {(uint8_t)((mask >> 8) & 0xFF), (uint8_t)(mask & 0xFF)};
        writeCommand(SX1262_CMD_CLR_IRQ_STATUS, data, 2);
    }

    uint16_t SX1262::getIrqStatus()
    {
        uint8_t data[2] = {0};
        readCommand(SX1262_CMD_GET_IRQ_STATUS, data, 2);
        return ((uint16_t)data[0] << 8) | data[1];
    }

    void SX1262::setDio2AsRfSwitch(bool enable)
    {
        uint8_t data = (uint8_t)(enable ? 0x01 : 0x00);
        writeCommand(SX1262_CMD_SET_DIO2_AS_RF_SW, &data, 1);
    }

    void SX1262::setDio3AsTcxoCtrl(uint8_t voltage, uint32_t timeout)
    {
        uint8_t data[4] = {voltage,
                           (uint8_t)((timeout >> 16) & 0xFF),
                           (uint8_t)((timeout >> 8) & 0xFF),
                           (uint8_t)(timeout & 0xFF)};
        writeCommand(SX1262_CMD_SET_DIO3_AS_TCXO, data, 4);
    }

    void SX1262::calibrateImage(uint32_t freq_hz)
    {
        uint8_t data[2];
        if (freq_hz >= 902000000 && freq_hz <= 928000000)
        {
            data[0] = 0xE1;
            data[1] = 0xE9;
        }
        else if (freq_hz >= 863000000 && freq_hz <= 870000000)
        {
            data[0] = 0xD7;
            data[1] = 0xDB;
        }
        else if (freq_hz >= 779000000 && freq_hz <= 787000000)
        {
            data[0] = 0xC1;
            data[1] = 0xC5;
        }
        else if (freq_hz >= 470000000 && freq_hz <= 510000000)
        {
            data[0] = 0x75;
            data[1] = 0x81;
        }
        else if (freq_hz >= 430000000 && freq_hz <= 440000000)
        {
            data[0] = 0x6B;
            data[1] = 0x6F;
        }
        else
        {
            return;
        }
        writeCommand(SX1262_CMD_CALIBRATE_IMAGE, data, 2);
    }

    void SX1262::setRegulatorMode(bool use_dcdc)
    {
        uint8_t mode = use_dcdc ? SX1262_REG_MODE_DCDC : SX1262_REG_MODE_LDO;
        writeCommand(SX1262_CMD_SET_REGULATOR_MODE, &mode, 1);
    }

    void SX1262::setRxTxFallback(uint8_t mode) { writeCommand(SX1262_CMD_SET_RX_TX_FALLBACK, &mode, 1); }

    void SX1262::calibrate(uint8_t calib_mask) { writeCommand(SX1262_CMD_CALIBRATE, &calib_mask, 1); }

    void SX1262::setTx(uint32_t timeout_ms)
    {
        // Timeout in 15.625 us steps
        uint32_t timeout = (timeout_ms * 1000) / 15.625;
        uint8_t data[3] = {(uint8_t)((timeout >> 16) & 0xFF), (uint8_t)((timeout >> 8) & 0xFF), (uint8_t)(timeout & 0xFF)};
        writeCommand(SX1262_CMD_SET_TX, data, 3);
        _state = SX1262State::TX;
    }

    void SX1262::setRx(uint32_t timeout_ms)
    {
        uint32_t timeout;
        if (timeout_ms == 0)
        {
            timeout = 0xFFFFFF; // Continuous RX
        }
        else
        {
            timeout = (timeout_ms * 1000) / 15.625;
        }
        uint8_t data[3] = {(uint8_t)((timeout >> 16) & 0xFF), (uint8_t)((timeout >> 8) & 0xFF), (uint8_t)(timeout & 0xFF)};
        writeCommand(SX1262_CMD_SET_RX, data, 3);
        _state = SX1262State::RX;
    }

    void SX1262::setCad()
    {
        writeCommand(SX1262_CMD_SET_CAD);
        _state = SX1262State::CAD;
    }

    void SX1262::getRxBufferStatus(uint8_t* payload_len, uint8_t* start_ptr)
    {
        uint8_t data[2] = {0};
        readCommand(SX1262_CMD_GET_RX_BUFFER_STATUS, data, 2);
        if (payload_len)
            *payload_len = data[0];
        if (start_ptr)
            *start_ptr = data[1];
    }

    void SX1262::getPacketStatus(int16_t* rssi, float* snr)
    {
        uint8_t data[3] = {0};
        readCommand(SX1262_CMD_GET_PACKET_STATUS, data, 3);
        if (rssi)
            *rssi = -(int16_t)data[0] / 2;
        if (snr)
            *snr = (int8_t)data[1] / 4.0f; // SNR in 0.25 dB steps, convert to float
    }

    uint8_t SX1262::bandwidthToCode(uint32_t bw_hz)
    {
        if (bw_hz <= 7800)
            return SX1262_LORA_BW_7_8;
        if (bw_hz <= 10400)
            return SX1262_LORA_BW_10_4;
        if (bw_hz <= 15600)
            return SX1262_LORA_BW_15_6;
        if (bw_hz <= 20800)
            return SX1262_LORA_BW_20_8;
        if (bw_hz <= 31250)
            return SX1262_LORA_BW_31_25;
        if (bw_hz <= 41700)
            return SX1262_LORA_BW_41_7;
        if (bw_hz <= 62500)
            return SX1262_LORA_BW_62_5;
        if (bw_hz <= 125000)
            return SX1262_LORA_BW_125;
        if (bw_hz <= 250000)
            return SX1262_LORA_BW_250;
        return SX1262_LORA_BW_500;
    }

    bool SX1262::isLowDataRateOptimize(uint8_t sf, uint32_t bw_hz)
    {
        // Enable low data rate optimization when symbol time > 16.38 ms
        float symbol_time_ms = (float)(1 << sf) / ((float)bw_hz / 1000.0f);
        return symbol_time_ms > 16.38f;
    }

    void SX1262::setAntenna(bool tx)
    {
        if (_pins.rxen >= 0 && _pins.txen >= 0)
        {
            gpio_set_level((gpio_num_t)_pins.rxen, tx ? 0 : 1);
            gpio_set_level((gpio_num_t)_pins.txen, tx ? 1 : 0);
        }
    }

    // RadioInterface implementation

    bool SX1262::setConfig(const LoRaConfig& config)
    {
        ESP_LOGI(TAG,
                 "Setting config: %.3f MHz, BW=%lu, SF=%d, CR=4/%d, Power=%d dBm",
                 config.frequency_hz / 1000000.0f,
                 config.bandwidth_hz,
                 config.spreading_factor,
                 config.coding_rate,
                 config.tx_power_dbm);

        // Go to standby
        setStandby(true);
        waitBusy();

        // Set frequency
        calibrateImage(config.frequency_hz);
        waitBusy();
        setRfFrequency(config.frequency_hz);
        waitBusy();

        // Set PA and TX power
        setPaConfig(config.tx_power_dbm);
        waitBusy();
        setTxParams(config.tx_power_dbm, SX1262_RAMP_200_US);
        waitBusy();

        // Set modulation parameters
        uint8_t bw_code = bandwidthToCode(config.bandwidth_hz);
        bool low_dr = isLowDataRateOptimize(config.spreading_factor, config.bandwidth_hz);
        setModulationParams(config.spreading_factor, bw_code, config.coding_rate - 4, low_dr);
        waitBusy();

        // Set packet parameters
        setPacketParams(config.preamble_length, config.implicit_header, 255, config.crc_enabled, config.iq_inverted);
        waitBusy();

        // Set sync word
        uint8_t sync_word[2] = {(uint8_t)((config.sync_word & 0xF0) | 0x04),
                                (uint8_t)(((config.sync_word & 0x0F) << 4) | 0x04)};
        writeRegister(0x0740, sync_word, 2);
        waitBusy();

        // Set RX gain: 0x96 = boosted, 0x94 = power saving
        uint8_t rx_gain = config.rx_boosted_gain ? 0x96 : 0x94;
        writeRegister(0x08AC, &rx_gain, 1);
        waitBusy();

        _config = config;
        return true;
    }

    LoRaConfig SX1262::getConfig() const { return _config; }

    bool SX1262::setFrequency(uint32_t freq_hz)
    {
        _config.frequency_hz = freq_hz;

        setStandby(true);
        waitBusy();
        calibrateImage(freq_hz);
        waitBusy();
        setRfFrequency(freq_hz);
        waitBusy();

        return true;
    }

    bool SX1262::setTxPower(int8_t power_dbm)
    {
        _config.tx_power_dbm = power_dbm;

        setStandby(true);
        waitBusy();
        setPaConfig(power_dbm);
        waitBusy();
        setTxParams(power_dbm, SX1262_RAMP_200_US);
        waitBusy();

        return true;
    }

    bool SX1262::transmit(const uint8_t* data, uint8_t len)
    {
        if (!data || len == 0)
        {
            return false;
        }

        ESP_LOGD(TAG, "Transmitting %d bytes", len);

        // Go to standby
        setStandby(true);
        waitBusy();

        // Update packet length in packet params
        setPacketParams(_config.preamble_length, _config.implicit_header, len, _config.crc_enabled, _config.iq_inverted);
        waitBusy();

        // Write data to buffer
        writeBuffer(0x00, data, len);
        waitBusy();

        // Clear IRQ
        clearIrqStatus(SX1262_IRQ_ALL);

        // Set antenna for TX
        setAntenna(true);

        // Start transmission (3 second timeout)
        setTx(3000);

        _mode = RadioMode::TX;
        return true;
    }

    bool SX1262::startReceive(uint32_t timeout_ms)
    {
        ESP_LOGD(TAG, "Starting RX, timeout=%lu ms", timeout_ms);

        // Go to standby
        setStandby(true);
        waitBusy();

        // Set max packet length
        setPacketParams(_config.preamble_length, _config.implicit_header, 255, _config.crc_enabled, _config.iq_inverted);
        waitBusy();

        // Clear IRQ
        clearIrqStatus(SX1262_IRQ_ALL);

        // Re-apply RX gain (can be reset after standby)
        uint8_t rx_gain = _config.rx_boosted_gain ? 0x96 : 0x94;
        writeRegister(0x08AC, &rx_gain, 1);
        waitBusy();

        // Set antenna for RX
        setAntenna(false);

        // Start receiving
        setRx(timeout_ms);

        _mode = RadioMode::RX;
        return true;
    }

    int SX1262::readPacket(uint8_t* buffer, uint8_t max_len, RxPacketInfo* info)
    {
        uint8_t payload_len = 0;
        uint8_t start_ptr = 0;

        // Get buffer status
        getRxBufferStatus(&payload_len, &start_ptr);

        if (payload_len == 0 || payload_len > max_len)
        {
            return -1;
        }

        // Get packet status
        int16_t rssi;
        float snr;
        getPacketStatus(&rssi, &snr);

        _last_rssi = rssi;
        _last_snr = snr;
        _last_rx_len = payload_len;
        _rx_buffer_ptr = start_ptr;

        // Read data from buffer
        readBuffer(start_ptr, buffer, payload_len);

        if (info)
        {
            info->rssi = rssi;
            info->snr = snr;
            info->frequency = _config.frequency_hz;
            info->timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
            info->crc_ok = true; // CRC error would trigger different IRQ
        }

        ESP_LOGD(TAG, "Received %d bytes, RSSI=%d, SNR=%.1f", payload_len, rssi, snr);

        return payload_len;
    }

    bool SX1262::startCAD()
    {
        ESP_LOGI(TAG, "Starting CAD");

        setStandby(true);
        waitBusy();

        // Configure CAD parameters
        // Symbol num, detect peak, detect min, exit mode, timeout
        uint8_t cad_params[7] = {0x03, 22, 10, 0x00, 0x00, 0x00, 0x00};
        writeCommand(SX1262_CMD_SET_CAD_PARAMS, cad_params, 7);
        waitBusy();

        clearIrqStatus(SX1262_IRQ_ALL);
        setAntenna(false);
        setCad();

        _mode = RadioMode::CAD;
        return true;
    }

    bool SX1262::setMode(RadioMode mode)
    {
        switch (mode)
        {
        case RadioMode::SLEEP:
            setSleep(true);
            _mode = RadioMode::SLEEP;
            break;
        case RadioMode::STANDBY:
            setStandby(true);
            _mode = RadioMode::STANDBY;
            break;
        case RadioMode::RX:
            return startReceive(0);
        case RadioMode::TX:
            // TX requires data, can't start without it
            return false;
        case RadioMode::CAD:
            return startCAD();
        }
        return true;
    }

    RadioMode SX1262::getMode() const { return _mode; }

    bool SX1262::isBusy() const { return gpio_get_level((gpio_num_t)_pins.busy) || _mode == RadioMode::TX; }

    int16_t SX1262::getRSSI() const { return _last_rssi; }

    float SX1262::getSNR() const { return _last_snr; }

    void SX1262::setEventCallback(RadioEventCallback callback) { _event_callback = callback; }

    void IRAM_ATTR SX1262::dio1_isr_handler(void* arg)
    {
        SX1262* radio = static_cast<SX1262*>(arg);
        radio->_irq_pending = true;
        if (radio->_notify_task)
        {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xTaskNotifyFromISR(radio->_notify_task, 1, eSetBits, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }

    void SX1262::handleDio1Interrupt()
    {
        uint16_t irq = getIrqStatus();
        clearIrqStatus(irq);

        ESP_LOGD(TAG, "IRQ status: 0x%04X", irq);

        RadioEvent event = RadioEvent::ERROR;
        bool has_event = false;

        if (irq & SX1262_IRQ_TX_DONE)
        {
            ESP_LOGD(TAG, "TX done");
            event = RadioEvent::TX_DONE;
            has_event = true;
            _mode = RadioMode::STANDBY;
            setStandby(true);
        }
        else if (irq & SX1262_IRQ_RX_DONE)
        {
            if (irq & SX1262_IRQ_CRC_ERR)
            {
                ESP_LOGW(TAG, "RX CRC error");
                event = RadioEvent::RX_ERROR;
            }
            else
            {
                ESP_LOGD(TAG, "RX done");
                event = RadioEvent::RX_DONE;
            }
            has_event = true;
        }
        else if (irq & SX1262_IRQ_TIMEOUT)
        {
            if (_state == SX1262State::RX)
            {
                ESP_LOGI(TAG, "RX timeout");
                event = RadioEvent::RX_TIMEOUT;
            }
            else if (_state == SX1262State::TX)
            {
                ESP_LOGW(TAG, "TX timeout");
                event = RadioEvent::TX_TIMEOUT;
            }
            has_event = true;
            _mode = RadioMode::STANDBY;
            setStandby(true);
        }
        else if (irq & SX1262_IRQ_CAD_DONE)
        {
            if (irq & SX1262_IRQ_CAD_DETECTED)
            {
                ESP_LOGI(TAG, "CAD detected activity");
                event = RadioEvent::CAD_DETECTED;
            }
            else
            {
                ESP_LOGI(TAG, "CAD done, channel free");
                event = RadioEvent::CAD_DONE;
            }
            has_event = true;
            _mode = RadioMode::STANDBY;
            setStandby(true);
        }

        if (has_event && _event_callback)
        {
            _event_callback(event);
        }
    }

    void SX1262::processEvents()
    {
        if (_irq_pending)
        {
            _irq_pending = false;
            handleDio1Interrupt();
        }
    }

} // namespace HAL
