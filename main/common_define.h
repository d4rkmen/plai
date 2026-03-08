/**
 */
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <ctime>

#define delay(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#define millis() (esp_timer_get_time() / 1000)

// ---------------------------------------------------------------------------
// Compile-time build timestamp derived from __DATE__ / __TIME__.
// Used to reject GPS fixes whose reported time predates this firmware build.
// ---------------------------------------------------------------------------
namespace
{
    constexpr int _buildMonth(const char* d)
    {
        // clang-format off
        return d[0]=='J' ? (d[1]=='a' ? 1 : (d[2]=='n' ? 6 : 7)) :
               d[0]=='F' ? 2 :
               d[0]=='M' ? (d[2]=='r' ? 3 : 5) :
               d[0]=='A' ? (d[1]=='p' ? 4 : 8) :
               d[0]=='S' ? 9  :
               d[0]=='O' ? 10 :
               d[0]=='N' ? 11 : 12;
        // clang-format on
    }
    constexpr int _daysFromEpoch(int y, int m, int d)
    {
        y -= m <= 2;
        const int era = y / 400;
        const int yoe = y - era * 400;
        const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }
    constexpr time_t _buildTimestamp()
    {
        const char* D = __DATE__; // "Mmm DD YYYY"
        const char* T = __TIME__; // "HH:MM:SS"
        int y  = (D[7]-'0')*1000 + (D[8]-'0')*100 + (D[9]-'0')*10 + (D[10]-'0');
        int m  = _buildMonth(D);
        int d  = (D[4]==' ' ? 0 : D[4]-'0') * 10 + (D[5]-'0');
        int h  = (T[0]-'0')*10 + (T[1]-'0');
        int mi = (T[3]-'0')*10 + (T[4]-'0');
        int s  = (T[6]-'0')*10 + (T[7]-'0');
        return (time_t)_daysFromEpoch(y, m, d) * 86400 + h * 3600 + mi * 60 + s;
    }
} // anonymous namespace

// Compile-time Unix timestamp of this firmware build.
// GPS fixes reporting a time before this value are considered invalid.
#define BUILD_TIMESTAMP (_buildTimestamp())

// Minimum GPS-vs-system-clock drift (seconds) that triggers a time adjustment.
static constexpr time_t GPS_SIGNIFICANT_DRIFT_S = 60;