/**
 * @file board.h
 * @brief Hardware pin definitions and board configuration
 */
#pragma once

#include "driver/spi_master.h"

namespace HAL
{
    /**
     * @brief Board type
     */
    enum class BoardType
    {
        AUTO_DETECT,   // Auto-detect board type
        CARDPUTER,     // Original M5Cardputer with IOMatrix
        CARDPUTER_ADV, // M5Cardputer ADV with TCA8418
    };

    /**
     * @brief LoRa SX1262 HAT pin definitions
     * M5Stack LoRa HAT connected via expansion port
     * Uses SPI2 bus (shared with SD card, different CS)
     */
    namespace LoRa
    {
        // SPI Configuration
        constexpr spi_host_device_t SPI_HOST = SPI2_HOST;
        constexpr int SPI_FREQ_HZ = 10000000; // 10 MHz

        // SPI Pins (shared with SD card on SPI2)
        constexpr int PIN_SCK = 40;
        constexpr int PIN_MOSI = 14;
        constexpr int PIN_MISO = 39;

        // SX1262 Control Pins
        constexpr int PIN_CS = 5;   // Chip Select (active low)
        constexpr int PIN_RST = 3;  // Reset (active low)
        constexpr int PIN_BUSY = 6; // Busy indicator (active high)
        constexpr int PIN_DIO1 = 4; // IRQ pin

        // Optional antenna switch pins (set to -1 if not used)
        constexpr int PIN_RXEN = -1; // RX enable for antenna switch
        constexpr int PIN_TXEN = -1; // TX enable for antenna switch

        // Default LoRa configuration
        constexpr float DEFAULT_FREQ_MHZ = 915.0f;   // US frequency
        constexpr int DEFAULT_BANDWIDTH_KHZ = 250;   // 250 kHz
        constexpr int DEFAULT_SPREADING_FACTOR = 11; // SF11
        constexpr int DEFAULT_CODING_RATE = 5;       // 4/5
        constexpr int DEFAULT_TX_POWER_DBM = 22;     // Max power
        constexpr int DEFAULT_PREAMBLE_LENGTH = 8;
        constexpr int DEFAULT_SYNC_WORD = 0x2B; // Meshtastic sync word
    } // namespace LoRa

    /**
     * @brief GPS ATGM336H pin definitions
     * Connected via UART on expansion port
     */
    namespace Gps
    {
        constexpr int PIN_RX = 15;           // GPS TX -> ESP32 RX
        constexpr int PIN_TX = 13;           // ESP32 TX -> GPS RX
        constexpr int UART_NUM = 2;          // UART2 (UART0 = console, UART1 = reserved)
        constexpr int BAUD_RATE = 115200;      // ATGM336H default baud rate
        constexpr int RX_BUF_SIZE = 1024;    // UART RX ring buffer size
    } // namespace GPS

} // namespace HAL
