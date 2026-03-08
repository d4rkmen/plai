/**
 * @file gps.cpp
 * @brief GPS driver for ATGM336H module via UART
 *
 * Parses NMEA 0183 sentences (GGA, RMC) from the GPS module
 * running on a background FreeRTOS task.
 */
#include "gps.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

static const char* TAG = "GPS";

// Background task parameters
#define GPS_TASK_STACK_SIZE 3072
#define GPS_TASK_PRIORITY 5
#define GPS_READ_TIMEOUT_MS 100

namespace HAL
{

    GPS::GPS(int rx_pin, int tx_pin, int uart_num, int baud_rate)
        : _rx_pin(rx_pin), _tx_pin(tx_pin), _uart_num(uart_num), _baud_rate(baud_rate), _initialized(false),
          _task_handle(nullptr), _task_running(false), _nmea_pos(0), _data{}
    {
        memset(_nmea_buf, 0, sizeof(_nmea_buf));
    }

    GPS::~GPS() { deinit(); }

    bool GPS::init()
    {
        if (_initialized)
        {
            return true;
        }

        ESP_LOGI(TAG, "Initializing GPS on UART%d (RX=%d, TX=%d, baud=%d)", _uart_num, _rx_pin, _tx_pin, _baud_rate);

        // Configure UART
        uart_config_t uart_config = {
            .baud_rate = _baud_rate,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_DEFAULT,
        };

        esp_err_t err = uart_param_config((uart_port_t)_uart_num, &uart_config);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(err));
            return false;
        }

        err = uart_set_pin((uart_port_t)_uart_num, _tx_pin, _rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(err));
            return false;
        }

        err = uart_driver_install((uart_port_t)_uart_num, Gps::RX_BUF_SIZE * 2, 0, 0, nullptr, 0);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
            return false;
        }

        // Clear GPS data
        memset((void*)&_data, 0, sizeof(GpsData));
        _nmea_pos = 0;

        // Start background task
        _task_running = true;
        BaseType_t ret = xTaskCreate(_uart_task, "gps_task", GPS_TASK_STACK_SIZE, this, GPS_TASK_PRIORITY, &_task_handle);

        if (ret != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create GPS task");
            uart_driver_delete((uart_port_t)_uart_num);
            return false;
        }

        _initialized = true;
        ESP_LOGI(TAG, "GPS initialized successfully");
        return true;
    }

    void GPS::deinit()
    {
        if (!_initialized)
        {
            return;
        }

        ESP_LOGI(TAG, "Deinitializing GPS");

        // Stop background task
        _task_running = false;
        if (_task_handle)
        {
            // Wait for task to exit
            vTaskDelay(pdMS_TO_TICKS(GPS_READ_TIMEOUT_MS * 2));
            _task_handle = nullptr;
        }

        // Remove UART driver
        uart_driver_delete((uart_port_t)_uart_num);

        _initialized = false;
    }

    void GPS::setDataCallback(DataCallback cb) { _data_callback = std::move(cb); }

    GpsData GPS::getData() const
    {
        // Simple copy - GpsData is small and updated atomically per-field
        GpsData copy;
        memcpy(&copy, (const void*)&_data, sizeof(GpsData));
        return copy;
    }

    uint32_t GPS::msSinceLastFix() const
    {
        if (_data.last_fix_ms == 0)
        {
            return UINT32_MAX;
        }
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        return now - _data.last_fix_ms;
    }

    void GPS::sendCommand(const char* cmd)
    {
        if (!_initialized || !cmd)
        {
            return;
        }

        // Calculate NMEA checksum
        uint8_t checksum = 0;
        for (const char* p = cmd; *p; p++)
        {
            checksum ^= (uint8_t)*p;
        }

        // Send: $CMD*XX\r\n
        char buf[128];
        int len = snprintf(buf, sizeof(buf), "$%s*%02X\r\n", cmd, checksum);
        if (len > 0 && len < (int)sizeof(buf))
        {
            uart_write_bytes((uart_port_t)_uart_num, buf, len);
            ESP_LOGI(TAG, "Sent: %.*s", len - 2, buf); // Trim \r\n for log
        }
    }

    // ========== Background Task ==========

    void GPS::_uart_task(void* arg)
    {
        GPS* self = static_cast<GPS*>(arg);
        ESP_LOGI(TAG, "GPS task started");

        while (self->_task_running)
        {
            self->_process_uart();
        }

        ESP_LOGI(TAG, "GPS task stopped");
        vTaskDelete(nullptr);
    }

    void GPS::_process_uart()
    {
        uint8_t buf[128];
        int len = uart_read_bytes((uart_port_t)_uart_num, buf, sizeof(buf), pdMS_TO_TICKS(GPS_READ_TIMEOUT_MS));

        if (len > 0)
        {
            ESP_LOGD(TAG, "Received: %.*s", len, buf);
            for (int i = 0; i < len; i++)
            {
                _process_byte((char)buf[i]);
            }
        }
    }

    // ========== NMEA Parser ==========

    void GPS::_process_byte(char c)
    {
        if (c == '$')
        {
            // Start of a new sentence
            _nmea_pos = 0;
            _nmea_buf[_nmea_pos++] = c;
        }
        else if (_nmea_pos > 0)
        {
            if (c == '\r' || c == '\n')
            {
                // End of sentence
                if (_nmea_pos > 6) // Minimum valid sentence: $GPXXX*XX
                {
                    _nmea_buf[_nmea_pos] = '\0';
                    _parse_sentence();
                }
                _nmea_pos = 0;
            }
            else if (_nmea_pos < (int)sizeof(_nmea_buf) - 1)
            {
                _nmea_buf[_nmea_pos++] = c;
            }
            else
            {
                // Buffer overflow - discard
                _nmea_pos = 0;
            }
        }
    }

    void GPS::_parse_sentence()
    {
        ESP_LOGD(TAG, "Parsing sentence: %s", _nmea_buf);
        // Validate checksum
        if (!_validate_checksum(_nmea_buf, _nmea_pos))
        {
            ESP_LOGW(TAG, "Invalid checksum: %s", _nmea_buf);
            return;
        }

        // Identify sentence type (skip $GN/$GP prefix, check the 3-letter ID)
        // Support: $GPGGA, $GNGGA, $GPRMC, $GNRMC
        const char* type_start = _nmea_buf + 3; // Skip "$GP" or "$GN"

        if (strncmp(type_start, "GGA", 3) == 0)
        {
            _parse_gga(_nmea_buf);
        }
        else if (strncmp(type_start, "RMC", 3) == 0)
        {
            _parse_rmc(_nmea_buf);
        }

        // Increment sentence counter (non-volatile, just diagnostic)
        uint32_t count = _data.sentence_count;
        _data.sentence_count = count + 1;
    }

    bool GPS::_validate_checksum(const char* sentence, int len)
    {
        // Find '*' that precedes the 2-digit hex checksum
        const char* star = nullptr;
        for (int i = len - 1; i > 0; i--)
        {
            if (sentence[i] == '*')
            {
                star = &sentence[i];
                break;
            }
        }

        if (!star || (star + 3 > sentence + len))
        {
            return false;
        }

        // Compute checksum: XOR of all characters between '$' and '*' (exclusive)
        uint8_t computed = 0;
        for (const char* p = sentence + 1; p < star; p++)
        {
            computed ^= (uint8_t)*p;
        }

        // Parse expected checksum
        uint8_t expected = (uint8_t)strtol(star + 1, nullptr, 16);

        return computed == expected;
    }

    // ========== GGA: Global Positioning System Fix Data ==========
    // $GPGGA,hhmmss.ss,llll.lll,a,yyyyy.yyy,b,q,ss,h.h,alt,M,geoid,M,age,ref*cs
    //   0: Time (hhmmss.ss)
    //   1: Latitude (ddmm.mmmmm)
    //   2: N/S
    //   3: Longitude (dddmm.mmmmm)
    //   4: E/W
    //   5: Fix quality (0=no fix, 1=GPS, 2=DGPS, ...)
    //   6: Number of satellites used
    //   7: HDOP
    //   8: Altitude above MSL
    //   9: M (altitude units)
    //  10: Geoid separation
    //  11: M (geoid units)

    void GPS::_parse_gga(const char* sentence)
    {
        ESP_LOGD(TAG, "Parsing GGA sentence: %s", sentence);
        const char* p = sentence;

        // Skip sentence ID
        p = _next_field(p); // field 0: time
        if (!p)
            return;

        // Parse time
        if (*p && *p != ',')
        {
            int h = _parse_int(p, 2);
            int m = _parse_int(p + 2, 2);
            int s = _parse_int(p + 4, 2);
            _data.hour = (uint8_t)h;
            _data.minute = (uint8_t)m;
            _data.second = (uint8_t)s;
        }

        p = _next_field(p); // field 1: latitude
        if (!p)
            return;
        const char* lat_str = p;

        p = _next_field(p); // field 2: N/S
        if (!p)
            return;
        char lat_dir = *p;

        p = _next_field(p); // field 3: longitude
        if (!p)
            return;
        const char* lon_str = p;

        p = _next_field(p); // field 4: E/W
        if (!p)
            return;
        char lon_dir = *p;

        p = _next_field(p); // field 5: fix quality
        if (!p)
            return;
        int fix = _parse_int(p, 1);

        p = _next_field(p); // field 6: satellites used
        if (!p)
            return;
        int sats = 0;
        if (*p && *p != ',')
        {
            sats = (int)_parse_float(p);
        }

        p = _next_field(p); // field 7: HDOP
        if (!p)
            return;
        double hdop = 0;
        if (*p && *p != ',')
        {
            hdop = _parse_float(p);
        }

        p = _next_field(p); // field 8: altitude MSL
        if (!p)
            return;
        double alt_msl = 0;
        if (*p && *p != ',')
        {
            alt_msl = _parse_float(p);
        }

        p = _next_field(p); // field 9: altitude unit (M)
        if (!p)
            return;

        p = _next_field(p); // field 10: geoid separation
        if (!p)
            return;
        double geoid_sep = 0;
        if (*p && *p != ',')
        {
            geoid_sep = _parse_float(p);
        }

        // Update fix data
        _data.fix_quality = static_cast<GpsFixQuality>(fix);
        _data.has_fix = (fix > 0);
        _data.sats_used = (uint32_t)sats;
        _data.hdop = (uint32_t)(hdop * 100.0);
        _data.altitude_msl = (int32_t)alt_msl;
        _data.altitude_hae = (int32_t)(alt_msl + geoid_sep);

        if (_data.has_fix && lat_str[0] && lon_str[0] && lat_str[0] != ',' && lon_str[0] != ',')
        {
            double lat = _parse_coord(lat_str, lat_dir);
            double lon = _parse_coord(lon_str, lon_dir);

            _data.latitude = lat;
            _data.longitude = lon;
            _data.latitude_i = (int32_t)(lat * 1e7);
            _data.longitude_i = (int32_t)(lon * 1e7);
            _data.last_fix_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
    }

    // ========== RMC: Recommended Minimum Navigation Information ==========
    // $GPRMC,hhmmss.ss,A,llll.lll,a,yyyyy.yyy,b,speed,course,ddmmyy,mag,e,mode*cs
    //   0: Time (hhmmss.ss)
    //   1: Status (A=active, V=void)
    //   2: Latitude
    //   3: N/S
    //   4: Longitude
    //   5: E/W
    //   6: Speed over ground (knots)
    //   7: Course over ground (degrees true)
    //   8: Date (ddmmyy)

    void GPS::_parse_rmc(const char* sentence)
    {
        ESP_LOGD(TAG, "Parsing RMC sentence: %s", sentence);
        const char* p = sentence;

        // Skip sentence ID
        p = _next_field(p); // field 0: time
        if (!p)
            return;

        // Parse time
        if (*p && *p != ',')
        {
            _data.hour = (uint8_t)_parse_int(p, 2);
            _data.minute = (uint8_t)_parse_int(p + 2, 2);
            _data.second = (uint8_t)_parse_int(p + 4, 2);
        }

        p = _next_field(p); // field 1: status
        if (!p)
            return;
        bool active = (*p == 'A');

        p = _next_field(p); // field 2: latitude
        if (!p)
            return;
        const char* lat_str = p;

        p = _next_field(p); // field 3: N/S
        if (!p)
            return;
        char lat_dir = *p;

        p = _next_field(p); // field 4: longitude
        if (!p)
            return;
        const char* lon_str = p;

        p = _next_field(p); // field 5: E/W
        if (!p)
            return;
        char lon_dir = *p;

        p = _next_field(p); // field 6: speed (knots)
        if (!p)
            return;
        double speed_knots = 0;
        if (*p && *p != ',')
        {
            speed_knots = _parse_float(p);
        }

        p = _next_field(p); // field 7: course (degrees)
        if (!p)
            return;
        double course = 0;
        if (*p && *p != ',')
        {
            course = _parse_float(p);
        }

        p = _next_field(p); // field 8: date (ddmmyy)
        if (!p)
            return;
        if (*p && *p != ',' && strlen(p) >= 6)
        {
            _data.day = (uint8_t)_parse_int(p, 2);
            _data.month = (uint8_t)_parse_int(p + 2, 2);
            int yy = _parse_int(p + 4, 2);
            _data.year = (uint16_t)(2000 + yy);
        }

        // Update motion data
        // Speed: knots -> m/s (* 0.514444), store as * 100
        _data.ground_speed = (uint32_t)(speed_knots * 0.514444 * 100.0);
        // Course: degrees, store as * 1e5 (Meshtastic format)
        _data.ground_track = (uint32_t)(course * 1e5);

        // Update position from RMC if active
        if (active && lat_str[0] && lon_str[0] && lat_str[0] != ',' && lon_str[0] != ',')
        {
            double lat = _parse_coord(lat_str, lat_dir);
            double lon = _parse_coord(lon_str, lon_dir);

            _data.latitude = lat;
            _data.longitude = lon;
            _data.latitude_i = (int32_t)(lat * 1e7);
            _data.longitude_i = (int32_t)(lon * 1e7);
            _data.has_fix = true;
            _data.last_fix_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
        else if (!active)
        {
            _data.has_fix = false;
        }

        // Compute Unix timestamp from date + time
        if (_data.year >= 2020 && _data.month > 0 && _data.day > 0)
        {
            ESP_LOGD(TAG,
                     "Converting date and time to Unix timestamp: %d-%d-%d %d:%d:%d",
                     _data.year,
                     _data.month,
                     _data.day,
                     _data.hour,
                     _data.minute,
                     _data.second);
            _data.time = _to_unix_time(_data.year, _data.month, _data.day, _data.hour, _data.minute, _data.second);
        }

        // Notify subscriber with a consistent snapshot
        if (_data.has_fix && _data.time > 0 && _data_callback)
        {
            GpsData snapshot;
            memcpy(&snapshot, (const void*)&_data, sizeof(GpsData));
            _data_callback(snapshot);
        }
    }

    // ========== Helpers ==========

    double GPS::_parse_coord(const char* field, char dir)
    {
        // NMEA format: ddmm.mmmmm (lat) or dddmm.mmmmm (lon)
        // Find the decimal point to determine degree digits
        const char* dot = strchr(field, '.');
        if (!dot)
        {
            return 0.0;
        }

        int deg_digits = (int)(dot - field) - 2; // 2 digits for minutes before dot
        if (deg_digits < 1 || deg_digits > 3)
        {
            return 0.0;
        }

        double degrees = (double)_parse_int(field, deg_digits);
        double minutes = _parse_float(field + deg_digits);
        double result = degrees + minutes / 60.0;

        if (dir == 'S' || dir == 'W')
        {
            result = -result;
        }

        return result;
    }

    int GPS::_parse_int(const char* str, int len)
    {
        int val = 0;
        for (int i = 0; i < len && str[i] >= '0' && str[i] <= '9'; i++)
        {
            val = val * 10 + (str[i] - '0');
        }
        return val;
    }

    double GPS::_parse_float(const char* str) { return strtod(str, nullptr); }

    const char* GPS::_next_field(const char* p)
    {
        // Advance to next comma-separated field
        while (*p && *p != ',')
        {
            p++;
        }
        if (*p == ',')
        {
            return p + 1;
        }
        return nullptr;
    }

    uint32_t GPS::_to_unix_time(int year, int month, int day, int hour, int min, int sec)
    {
        // Simple conversion from date/time to Unix timestamp
        // Handles years 2000-2099
        // Days per month (non-leap)
        static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

        uint32_t days = 0;

        // Years since 1970
        for (int y = 1970; y < year; y++)
        {
            days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
        }

        // Months in current year
        for (int m = 1; m < month; m++)
        {
            days += days_in_month[m - 1];
            if (m == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
            {
                days += 1; // Leap year February
            }
        }

        // Days in current month
        days += (day - 1);

        return days * 86400 + hour * 3600 + min * 60 + sec;
    }

} // namespace HAL
