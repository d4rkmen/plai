/**
 * @file app_stats.cpp
 * @author d4rkmen
 * @brief Statistics widget implementation
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "app_stats.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "APP_STATS";

#define STAT_ROW_HEIGHT 16
#define MAX_VISIBLE_STATS 6
#define UPDATE_INTERVAL_MS 5000

using namespace MOONCAKE::APPS;

void AppStats::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
    _data.scroll_offset = 0;
    _data.last_update_ms = 0;
}

void AppStats::onResume()
{
    ANIM_APP_OPEN();
    _data.hal->canvas()->fillScreen(THEME_COLOR_BG);
    _data.hal->canvas()->setFont(FONT_16);
    _data.hal->canvas()->setTextSize(1);
    _data.hal->canvas_update();
    
    _data.scroll_offset = 0;
    _data.last_update_ms = 0;
}

void AppStats::onRunning()
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    
    if (now - _data.last_update_ms > UPDATE_INTERVAL_MS || _data.last_update_ms == 0)
    {
        _render_stats();
        _data.hal->canvas_update();
        _data.last_update_ms = now;
    }
    
    _handle_input();
}

void AppStats::onDestroy()
{
}

void AppStats::_render_stats()
{
    auto* canvas = _data.hal->canvas();
    canvas->fillScreen(THEME_COLOR_BG);
    
    // Header
    canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    canvas->drawString("Statistics", 5, 0);
    
    auto& store = Mesh::MeshDataStore::getInstance();
    const auto& stats = store.getStats();
    
    // Build stats list
    struct StatItem
    {
        const char* label;
        std::string value;
        uint32_t color;
    };
    
    std::vector<StatItem> items = {
        {"Uptime", _format_uptime(stats.uptime_ms), TFT_CYAN},
        {"TX Packets", std::to_string(stats.tx_packets), TFT_GREEN},
        {"RX Packets", std::to_string(stats.rx_packets), TFT_CYAN},
        {"TX Errors", std::to_string(stats.tx_errors), stats.tx_errors > 0 ? (uint32_t)TFT_RED : (uint32_t)TFT_CYAN},
        {"RX Errors", std::to_string(stats.rx_errors), stats.rx_errors > 0 ? (uint32_t)TFT_RED : (uint32_t)TFT_CYAN},
        {"Msgs Sent", std::to_string(stats.messages_sent), TFT_GREEN},
        {"Msgs Recv", std::to_string(stats.messages_received), TFT_CYAN},
        {"Forwarded", std::to_string(stats.packets_forwarded), TFT_CYAN},
    };
    
    // Add duty cycle
    char duty[16];
    snprintf(duty, sizeof(duty), "%.1f%%", stats.duty_cycle_percent);
    items.push_back({"Duty Cycle", duty, TFT_CYAN});
    
    // Add air time
    items.push_back({"Air Time", _format_uptime(stats.air_time_ms), TFT_CYAN});
    
    // BLE stats
    items.push_back({"BLE Status", stats.ble_connected ? "Connected" : "Disconnected", 
                     stats.ble_connected ? (uint32_t)TFT_GREEN : (uint32_t)TFT_DARKGREY});
    items.push_back({"BLE TX", _format_bytes(stats.ble_bytes_tx), TFT_CYAN});
    items.push_back({"BLE RX", _format_bytes(stats.ble_bytes_rx), TFT_CYAN});
    
    // Node count
    if (_data.hal->nodedb())
    {
        char nodes[16];
        snprintf(nodes, sizeof(nodes), "%d", (int)_data.hal->nodedb()->getNodeCount());
        items.push_back({"Nodes", nodes, TFT_CYAN});
    }
    
    // Render visible items
    canvas->setFont(FONT_12);
    int y = 18;
    int visible_count = 0;
    
    for (size_t i = _data.scroll_offset; 
         i < items.size() && visible_count < MAX_VISIBLE_STATS; 
         i++, visible_count++)
    {
        const auto& item = items[i];
        
        // Label
        canvas->setTextColor(TFT_WHITE, THEME_COLOR_BG);
        canvas->drawString(item.label, 5, y);
        
        // Value
        canvas->setTextColor(item.color, THEME_COLOR_BG);
        canvas->drawRightString(item.value.c_str(), canvas->width() - 5, y);
        
        y += STAT_ROW_HEIGHT;
    }
    
    canvas->setFont(FONT_16);
    
    // Scrollbar
    if (items.size() > MAX_VISIBLE_STATS)
    {
        int sb_height = MAX_VISIBLE_STATS * STAT_ROW_HEIGHT;
        int thumb_height = sb_height * MAX_VISIBLE_STATS / items.size();
        int thumb_pos = 18 + sb_height * _data.scroll_offset / items.size();
        
        canvas->drawRect(canvas->width() - 3, 18, 2, sb_height, TFT_DARKGREY);
        canvas->fillRect(canvas->width() - 3, thumb_pos, 2, thumb_height, TFT_ORANGE);
    }
    
    // Bottom hint
    canvas->setFont(FONT_10);
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawCenterString("[ENTER] Reset  [DEL] Back", canvas->width() / 2, canvas->height() - 12);
    canvas->setFont(FONT_16);
}

void AppStats::_handle_input()
{
    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();
    
    if (!_data.hal->keyboard()->isPressed())
        return;
    
    if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE) ||
        _data.hal->home_button()->is_pressed())
    {
        _data.hal->playNextSound();
        _data.hal->keyboard()->waitForRelease(KEY_NUM_BACKSPACE);
        destroyApp();
        return;
    }
    
    if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
    {
        _data.hal->playNextSound();
        _data.hal->keyboard()->waitForRelease(KEY_NUM_ENTER);
        
        // Reset stats
        auto& store = Mesh::MeshDataStore::getInstance();
        store.resetStats();
        _data.last_update_ms = 0; // Force refresh
    }
    else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_UP))
    {
        if (_data.scroll_offset > 0)
        {
            _data.scroll_offset--;
            _data.last_update_ms = 0;
        }
    }
    else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
    {
        // Calculate max offset dynamically
        int max_offset = 14 - MAX_VISIBLE_STATS; // Approximate item count
        if (_data.scroll_offset < max_offset)
        {
            _data.scroll_offset++;
            _data.last_update_ms = 0;
        }
    }
}

std::string AppStats::_format_uptime(uint32_t ms)
{
    uint32_t secs = ms / 1000;
    uint32_t mins = secs / 60;
    uint32_t hours = mins / 60;
    uint32_t days = hours / 24;
    
    char buf[32];
    if (days > 0)
    {
        snprintf(buf, sizeof(buf), "%dd %dh", (int)days, (int)(hours % 24));
    }
    else if (hours > 0)
    {
        snprintf(buf, sizeof(buf), "%dh %dm", (int)hours, (int)(mins % 60));
    }
    else if (mins > 0)
    {
        snprintf(buf, sizeof(buf), "%dm %ds", (int)mins, (int)(secs % 60));
    }
    else
    {
        snprintf(buf, sizeof(buf), "%ds", (int)secs);
    }
    return buf;
}

std::string AppStats::_format_bytes(uint32_t bytes)
{
    char buf[16];
    if (bytes >= 1024 * 1024)
    {
        snprintf(buf, sizeof(buf), "%.1fMB", bytes / (1024.0f * 1024.0f));
    }
    else if (bytes >= 1024)
    {
        snprintf(buf, sizeof(buf), "%.1fKB", bytes / 1024.0f);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%dB", (int)bytes);
    }
    return buf;
}

