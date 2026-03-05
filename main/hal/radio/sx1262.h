/**
 * @file sx1262.h
 * @author d4rkmen
 * @brief SX1262 LoRa radio driver
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef SX1262_H
#define SX1262_H

#include "radio_interface.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace HAL
{

    /**
     * @brief SX1262 hardware pin configuration
     */
    struct SX1262Pins
    {
        spi_host_device_t spi_host;
        int sck;
        int mosi;
        int miso;
        int cs;
        int rst;
        int busy;
        int dio1;
        int rxen; // Optional antenna switch RX enable (-1 if unused)
        int txen; // Optional antenna switch TX enable (-1 if unused)
    };

    /**
     * @brief SX1262 internal state
     */
    enum class SX1262State
    {
        UNINITIALIZED,
        SLEEP,
        STANDBY_RC,
        STANDBY_XOSC,
        FS,
        TX,
        RX,
        CAD
    };

    /**
     * @brief SX1262 LoRa radio driver
     */
    class SX1262 : public RadioInterface
    {
    public:
        /**
         * @brief Construct SX1262 driver with pin configuration
         * @param pins Hardware pin configuration
         */
        explicit SX1262(const SX1262Pins& pins);
        ~SX1262() override;

        // RadioInterface implementation
        bool init() override;
        void deinit() override;
        bool setConfig(const LoRaConfig& config) override;
        LoRaConfig getConfig() const override;
        bool setFrequency(uint32_t freq_hz) override;
        bool setTxPower(int8_t power_dbm) override;
        bool transmit(const uint8_t* data, uint8_t len) override;
        bool startReceive(uint32_t timeout_ms = 0) override;
        int readPacket(uint8_t* buffer, uint8_t max_len, RxPacketInfo* info = nullptr) override;
        bool startCAD() override;
        bool setMode(RadioMode mode) override;
        RadioMode getMode() const override;
        bool isBusy() const override;
        int16_t getRSSI() const override;
        float getSNR() const override;
        void setEventCallback(RadioEventCallback callback) override;
        void processEvents() override;
        const char* getName() const override { return "SX1262"; }

        /**
         * @brief Check if chip is present and responding
         * @return true if chip is detected
         */
        bool isChipPresent();

        /**
         * @brief Get chip version/status
         * @return Status byte
         */
        uint8_t getStatus();

        /**
         * @brief Read chip version register to verify SPI communication
         * @return Chip version byte (0x00 or 0x01 for SX1262), 0xFF on error
         */
        uint8_t getChipVersion();

        /**
         * @brief Set antenna switch for TX or RX
         * @param tx true for TX, false for RX
         */
        void setAntenna(bool tx);

    private:
        // SPI communication
        bool spiInit();
        void spiDeinit();
        bool spiTransfer(const uint8_t* tx, uint8_t* rx, size_t len);
        void writeCommand(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0);
        void readCommand(uint8_t cmd, uint8_t* data, size_t len);
        void writeRegister(uint16_t addr, const uint8_t* data, size_t len);
        void readRegister(uint16_t addr, uint8_t* data, size_t len);
        void writeBuffer(uint8_t offset, const uint8_t* data, size_t len);
        void readBuffer(uint8_t offset, uint8_t* data, size_t len);

        // Hardware control
        void reset();
        void waitBusy(uint32_t timeout_ms = 1000);
        void setCS(bool active);

        // SX1262 commands
        void setStandby(bool xosc = false);
        void setSleep(bool warm_start = true);
        void setPacketType(bool lora = true);
        void setRfFrequency(uint32_t freq_hz);
        void setPaConfig(int8_t power_dbm);
        void setTxParams(int8_t power_dbm, uint8_t ramp_time);
        void setModulationParams(uint8_t sf, uint8_t bw, uint8_t cr, bool low_data_rate);
        void setPacketParams(uint16_t preamble, bool implicit, uint8_t payload_len, bool crc, bool invert_iq);
        void setBufferBaseAddress(uint8_t tx_base, uint8_t rx_base);
        void setDioIrqParams(uint16_t irq_mask, uint16_t dio1_mask, uint16_t dio2_mask, uint16_t dio3_mask);
        void clearIrqStatus(uint16_t mask);
        uint16_t getIrqStatus();
        void setDio2AsRfSwitch(bool enable);
        void setDio3AsTcxoCtrl(uint8_t voltage, uint32_t timeout);
        void calibrateImage(uint32_t freq_hz);
        void setRegulatorMode(bool use_dcdc);
        void setRxTxFallback(uint8_t mode);
        void calibrate(uint8_t calib_mask);
        void setTx(uint32_t timeout_ms);
        void setRx(uint32_t timeout_ms);
        void setCad();
        void getRxBufferStatus(uint8_t* payload_len, uint8_t* start_ptr);
        void getPacketStatus(int16_t* rssi, float* snr);

        // Utility
        uint8_t bandwidthToCode(uint32_t bw_hz);
        uint32_t codeToBandwidth(uint8_t code);
        bool isLowDataRateOptimize(uint8_t sf, uint32_t bw_hz);

        // ISR handler
        static void IRAM_ATTR dio1_isr_handler(void* arg);
        void handleDio1Interrupt();

        // Member variables
        SX1262Pins _pins;
        spi_device_handle_t _spi_handle;
        SemaphoreHandle_t _spi_mutex;
        LoRaConfig _config;
        RadioMode _mode;
        SX1262State _state;
        RadioEventCallback _event_callback;
        volatile bool _irq_pending;
        int16_t _last_rssi;
        float _last_snr;
        uint8_t _last_rx_len;
        uint8_t _rx_buffer_ptr;
        bool _initialized;
    };

} // namespace HAL

#endif // SX1262_H
