/**
 * @file radio_interface.h
 * @author d4rkmen
 * @brief Abstract radio interface for LoRa modems
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef RADIO_INTERFACE_H
#define RADIO_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include <functional>

namespace HAL
{

    /**
     * @brief Radio operating mode
     */
    enum class RadioMode
    {
        SLEEP,
        STANDBY,
        RX,
        TX,
        CAD
    };

    /**
     * @brief Radio event types
     */
    enum class RadioEvent
    {
        TX_DONE,      // Transmission complete
        RX_DONE,      // Packet received
        RX_TIMEOUT,   // RX timeout
        RX_ERROR,     // RX CRC error
        CAD_DONE,     // CAD complete, channel free
        CAD_DETECTED, // CAD detected activity
        TX_TIMEOUT,   // TX timeout
        ERROR         // General error
    };

    /**
     * @brief LoRa modulation parameters
     */
    struct LoRaConfig
    {
        uint32_t frequency_hz;    // Center frequency in Hz
        uint32_t bandwidth_hz;    // Bandwidth in Hz (7800-500000)
        uint8_t spreading_factor; // Spreading factor (5-12)
        uint8_t coding_rate;      // Coding rate (5-8, represents 4/5 to 4/8)
        int8_t tx_power_dbm;      // TX power in dBm
        uint16_t preamble_length; // Preamble length in symbols
        uint8_t sync_word;        // Sync word
        bool crc_enabled;         // Enable CRC
        bool implicit_header;     // Use implicit header mode
        bool iq_inverted;         // Invert IQ signals
        bool rx_boosted_gain;     // SX126x RX boosted gain mode
    };

    /**
     * @brief Received packet information
     */
    struct RxPacketInfo
    {
        int16_t rssi;       // RSSI in dBm
        float snr;          // SNR in dB
        uint32_t frequency; // Actual frequency
        uint32_t timestamp; // Receive timestamp (ms)
        bool crc_ok;        // CRC check passed
    };

    /**
     * @brief Radio event callback type
     */
    using RadioEventCallback = std::function<void(RadioEvent event)>;

    /**
     * @brief Abstract radio interface
     */
    class RadioInterface
    {
    public:
        virtual ~RadioInterface() = default;

        /**
         * @brief Initialize the radio hardware
         * @return true on success
         */
        virtual bool init() = 0;

        /**
         * @brief Deinitialize the radio hardware
         */
        virtual void deinit() = 0;

        /**
         * @brief Set the radio configuration
         * @param config LoRa configuration
         * @return true on success
         */
        virtual bool setConfig(const LoRaConfig& config) = 0;

        /**
         * @brief Get current configuration
         * @return Current LoRa configuration
         */
        virtual LoRaConfig getConfig() const = 0;

        /**
         * @brief Set operating frequency
         * @param freq_hz Frequency in Hz
         * @return true on success
         */
        virtual bool setFrequency(uint32_t freq_hz) = 0;

        /**
         * @brief Set TX power
         * @param power_dbm Power in dBm
         * @return true on success
         */
        virtual bool setTxPower(int8_t power_dbm) = 0;

        /**
         * @brief Transmit a packet
         * @param data Packet data
         * @param len Data length
         * @return true if transmission started
         */
        virtual bool transmit(const uint8_t* data, uint8_t len) = 0;

        /**
         * @brief Start receiving
         * @param timeout_ms Timeout in milliseconds (0 = continuous)
         * @return true on success
         */
        virtual bool startReceive(uint32_t timeout_ms = 0) = 0;

        /**
         * @brief Read received packet
         * @param buffer Buffer to store data
         * @param max_len Maximum buffer size
         * @param info Packet information output
         * @return Number of bytes received, or -1 on error
         */
        virtual int readPacket(uint8_t* buffer, uint8_t max_len, RxPacketInfo* info = nullptr) = 0;

        /**
         * @brief Start channel activity detection
         * @return true on success
         */
        virtual bool startCAD() = 0;

        /**
         * @brief Set radio mode
         * @param mode Operating mode
         * @return true on success
         */
        virtual bool setMode(RadioMode mode) = 0;

        /**
         * @brief Get current radio mode
         * @return Current mode
         */
        virtual RadioMode getMode() const = 0;

        /**
         * @brief Check if radio is busy (TX or RX in progress)
         * @return true if busy
         */
        virtual bool isBusy() const = 0;

        /**
         * @brief Get last RSSI value
         * @return RSSI in dBm
         */
        virtual int16_t getRSSI() const = 0;

        /**
         * @brief Get last SNR value
         * @return SNR in dB
         */
        virtual float getSNR() const = 0;

        /**
         * @brief Set event callback
         * @param callback Callback function
         */
        virtual void setEventCallback(RadioEventCallback callback) = 0;

        /**
         * @brief Process radio events (call from main loop or ISR)
         */
        virtual void processEvents() = 0;

        /**
         * @brief Get radio hardware name
         * @return Name string
         */
        virtual const char* getName() const = 0;
    };

} // namespace HAL

#endif // RADIO_INTERFACE_H
