/**
 * @file gps.h
 * @brief GPS driver for ATGM336H module via UART
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../board.h"

namespace HAL
{
    /**
     * @brief GPS fix quality (from NMEA GGA)
     */
    enum class GpsFixQuality : uint8_t
    {
        NO_FIX = 0,
        GPS_FIX = 1,    // Standard GPS
        DGPS_FIX = 2,   // Differential GPS
        PPS_FIX = 3,    // Pulse per second
        RTK_FIX = 4,    // Real-time kinematic
        FLOAT_RTK = 5,  // Float RTK
        ESTIMATED = 6,  // Dead reckoning
        MANUAL = 7,     // Manual input
        SIMULATION = 8, // Simulation mode
    };

    /**
     * @brief GPS position data
     */
    struct GpsData
    {
        // Position
        bool has_fix;              // True if GPS has a valid fix
        GpsFixQuality fix_quality; // Fix quality from GGA
        double latitude;           // Degrees (positive = N, negative = S)
        double longitude;          // Degrees (positive = E, negative = W)
        int32_t latitude_i;        // Meshtastic format: degrees * 1e7
        int32_t longitude_i;       // Meshtastic format: degrees * 1e7
        int32_t altitude_msl;      // Altitude above mean sea level in meters
        int32_t altitude_hae;      // Altitude above ellipsoid (HAE) in meters

        // Accuracy
        uint32_t hdop;         // Horizontal DOP * 100
        uint32_t sats_in_view; // Satellites in view
        uint32_t sats_used;    // Satellites used for fix

        // Motion
        uint32_t ground_speed; // Ground speed in m/s * 100
        uint32_t ground_track; // Course over ground in degrees * 1e5

        // Time (UTC)
        uint32_t time; // Unix timestamp (seconds since epoch)
        uint8_t hour;
        uint8_t minute;
        uint8_t second;
        uint8_t day;
        uint8_t month;
        uint16_t year;

        // Status
        uint32_t last_fix_ms;    // Tick count of last valid fix
        uint32_t sentence_count; // Total parsed NMEA sentences

        void clear() { memset(this, 0, sizeof(GpsData)); }
    };

    /**
     * @brief Callback invoked from the GPS background task whenever a new
     *        complete fix is available (after each valid RMC sentence).
     */
    using DataCallback = std::function<void(const GpsData&)>;

    /**
     * @brief GPS driver for ATGM336H (NMEA over UART)
     */
    class GPS
    {
    public:
        /**
         * @brief Construct GPS driver
         * @param rx_pin ESP32 RX pin (GPS TX)
         * @param tx_pin ESP32 TX pin (GPS RX)
         * @param uart_num UART port number
         * @param baud_rate UART baud rate
         */
        GPS(int rx_pin = Gps::PIN_RX, int tx_pin = Gps::PIN_TX, int uart_num = Gps::UART_NUM, int baud_rate = Gps::BAUD_RATE);

        ~GPS();

        /**
         * @brief Initialize UART and start background parsing task
         * @return true on success
         */
        bool init();

        /**
         * @brief Deinitialize UART and stop background task
         */
        void deinit();

        /**
         * @brief Check if GPS is initialized
         */
        bool isInitialized() const { return _initialized; }

        /**
         * @brief Check if GPS has a valid fix
         */
        bool hasFix() const { return _data.has_fix; }

        /**
         * @brief Get current GPS data (thread-safe copy)
         */
        GpsData getData() const;

        /**
         * @brief Get latitude in degrees
         */
        double getLatitude() const { return _data.latitude; }

        /**
         * @brief Get longitude in degrees
         */
        double getLongitude() const { return _data.longitude; }

        /**
         * @brief Get latitude in Meshtastic integer format (degrees * 1e7)
         */
        int32_t getLatitudeI() const { return _data.latitude_i; }

        /**
         * @brief Get longitude in Meshtastic integer format (degrees * 1e7)
         */
        int32_t getLongitudeI() const { return _data.longitude_i; }

        /**
         * @brief Get altitude above MSL in meters
         */
        int32_t getAltitude() const { return _data.altitude_msl; }

        /**
         * @brief Get number of satellites used
         */
        uint32_t getSatellites() const { return _data.sats_used; }

        /**
         * @brief Get HDOP * 100
         */
        uint32_t getHDOP() const { return _data.hdop; }

        /**
         * @brief Get ground speed in m/s * 100
         */
        uint32_t getGroundSpeed() const { return _data.ground_speed; }

        /**
         * @brief Get course over ground in degrees * 1e5
         */
        uint32_t getGroundTrack() const { return _data.ground_track; }

        /**
         * @brief Get fix quality
         */
        GpsFixQuality getFixQuality() const { return _data.fix_quality; }

        /**
         * @brief Get Unix timestamp from GPS
         */
        uint32_t getTime() const { return _data.time; }

        /**
         * @brief Get total parsed sentence count (for diagnostics)
         */
        uint32_t getSentenceCount() const { return _data.sentence_count; }

        /**
         * @brief Get milliseconds since last valid fix
         * @return ms since last fix, or UINT32_MAX if never had fix
         */
        uint32_t msSinceLastFix() const;

        /**
         * @brief Register a callback invoked on every new GPS fix (from the GPS task).
         *        Pass nullptr to unregister. The callback receives a snapshot of GpsData.
         * @param cb Callback function (or nullptr)
         */
        void setDataCallback(DataCallback cb);

        /**
         * @brief Send a NMEA command to GPS module
         * @param cmd Command without $ prefix or *checksum suffix (e.g. "PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0")
         */
        void sendCommand(const char* cmd);

    private:
        // UART configuration
        int _rx_pin;
        int _tx_pin;
        int _uart_num;
        int _baud_rate;

        // State
        bool _initialized;
        TaskHandle_t _task_handle;
        volatile bool _task_running;

        // NMEA parser state
        char _nmea_buf[128]; // Current NMEA sentence buffer
        int _nmea_pos;       // Current position in buffer

        // GPS data (updated by background task)
        volatile GpsData _data;

        // Optional callback fired after each valid RMC fix update
        DataCallback _data_callback;

        // Background task
        static void _uart_task(void* arg);
        void _process_uart();

        // NMEA parsing
        void _process_byte(char c);
        void _parse_sentence();
        bool _validate_checksum(const char* sentence, int len);

        // Sentence handlers
        void _parse_gga(const char* sentence); // Fix data
        void _parse_rmc(const char* sentence); // Recommended minimum

        // NMEA field helpers
        static double _parse_coord(const char* field, char dir);
        static int _parse_int(const char* str, int len);
        static double _parse_float(const char* str);
        static const char* _next_field(const char* p);

        // Time conversion
        static uint32_t _to_unix_time(int year, int month, int day, int hour, int min, int sec);
    };

} // namespace HAL
