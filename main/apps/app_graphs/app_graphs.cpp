/**
 * @file app_graphs.cpp
 * @author d4rkmen
 * @brief Graphs widget implementation
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "app_graphs.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "APP_GRAPHS";

#define UPDATE_INTERVAL_MS 2000

using namespace MOONCAKE::APPS;

void AppGraphs::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
    _data.current_graph = GraphType::MENU;
    _data.menu_selection = 0;
    _data.last_update_ms = 0;
}

void AppGraphs::onResume()
{
    ANIM_APP_OPEN();
    _data.hal->canvas()->fillScreen(THEME_COLOR_BG);
    _data.hal->canvas()->setFont(FONT_16);
    _data.hal->canvas()->setTextSize(1);
    _data.hal->canvas_update();

    _data.current_graph = GraphType::MENU;
    _data.menu_selection = 0;
    _data.last_update_ms = 0;
}

void AppGraphs::onRunning()
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    switch (_data.current_graph)
    {
    case GraphType::MENU:
        _render_menu();
        _data.hal->canvas_update();
        _handle_menu_input();
        break;

    case GraphType::BATTERY:
    case GraphType::CHANNEL_ACTIVITY:
        if (now - _data.last_update_ms > UPDATE_INTERVAL_MS || _data.last_update_ms == 0)
        {
            _update_graph_data();
            _render_graph();
            _data.hal->canvas_update();
            _data.last_update_ms = now;
        }
        _handle_graph_input();
        break;
    }
}

void AppGraphs::onDestroy() {}

void AppGraphs::_render_menu()
{
    auto* canvas = _data.hal->canvas();
    canvas->fillScreen(THEME_COLOR_BG);

    // Header
    canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    canvas->drawString("Graphs", 5, 0);

    // Menu items
    const char* items[] = {"Battery Voltage", "Channel Activity"};
    const int item_count = 2;

    int y = 30;
    for (int i = 0; i < item_count; i++)
    {
        if (i == _data.menu_selection)
        {
            canvas->fillRect(0, y, canvas->width(), 22, THEME_COLOR_BG_SELECTED);
            canvas->setTextColor(TFT_BLACK, THEME_COLOR_BG_SELECTED);
        }
        else
        {
            canvas->setTextColor(TFT_CYAN, THEME_COLOR_BG);
        }

        canvas->drawString(items[i], 10, y + 3);
        y += 24;
    }

    // Hint
    canvas->setFont(FONT_10);
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawCenterString("[ENTER] Select  [DEL] Back", canvas->width() / 2, canvas->height() - 12);
    canvas->setFont(FONT_16);
}

void AppGraphs::_render_graph()
{
    auto* canvas = _data.hal->canvas();
    canvas->fillScreen(THEME_COLOR_BG);

    // Header
    canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    canvas->drawString("<", 5, 0);

    const char* title = "";
    switch (_data.current_graph)
    {
    case GraphType::BATTERY:
        title = "Battery";
        break;
    case GraphType::CHANNEL_ACTIVITY:
        title = "Activity";
        break;
    default:
        break;
    }
    canvas->drawString(title, 20, 0);

    // Graph area
    int graph_x = 5;
    int graph_y = 20;
    int graph_w = canvas->width() - 10;
    int graph_h = canvas->height() - 40;

    _data.graph.render(canvas, graph_x, graph_y, graph_w, graph_h);

    // Y-axis labels
    canvas->setFont(FONT_10);
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);

    auto& store = Mesh::MeshDataStore::getInstance();

    if (_data.current_graph == GraphType::BATTERY)
    {
        const auto& history = store.getBatteryHistory();
        if (!history.empty())
        {
            float min_v = history[0].value, max_v = history[0].value;
            for (const auto& pt : history)
            {
                if (pt.value < min_v)
                    min_v = pt.value;
                if (pt.value > max_v)
                    max_v = pt.value;
            }

            char buf[16];
            snprintf(buf, sizeof(buf), "%.1fV", max_v);
            canvas->drawRightString(buf, canvas->width() - 5, graph_y);
            snprintf(buf, sizeof(buf), "%.1fV", min_v);
            canvas->drawRightString(buf, canvas->width() - 5, graph_y + graph_h - 10);
        }
    }

    // Hint
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawCenterString("[DEL] Back", canvas->width() / 2, canvas->height() - 12);
    canvas->setFont(FONT_16);
}

void AppGraphs::_handle_menu_input()
{
    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    if (!_data.hal->keyboard()->isPressed())
        return;

    if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE) || _data.hal->home_button()->is_pressed())
    {
        _data.hal->playNextSound();
        _data.hal->keyboard()->waitForRelease(KEY_NUM_BACKSPACE);
        destroyApp();
        return;
    }

    if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_UP))
    {
        _data.hal->playNextSound();
        if (_data.menu_selection > 0)
        {
            _data.menu_selection--;
        }
    }
    else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
    {
        _data.hal->playNextSound();
        if (_data.menu_selection < 1)
        {
            _data.menu_selection++;
        }
    }
    else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
    {
        _data.hal->playNextSound();
        _data.hal->keyboard()->waitForRelease(KEY_NUM_ENTER);

        switch (_data.menu_selection)
        {
        case 0:
            _data.current_graph = GraphType::BATTERY;
            break;
        case 1:
            _data.current_graph = GraphType::CHANNEL_ACTIVITY;
            break;
        }
        _data.last_update_ms = 0;
    }
}

void AppGraphs::_handle_graph_input()
{
    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    if (!_data.hal->keyboard()->isPressed())
        return;

    if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
    {
        _data.hal->playNextSound();
        _data.hal->keyboard()->waitForRelease(KEY_NUM_BACKSPACE);
        _data.current_graph = GraphType::MENU;
    }
}

void AppGraphs::_update_graph_data()
{
    auto& store = Mesh::MeshDataStore::getInstance();

    switch (_data.current_graph)
    {
    case GraphType::BATTERY:
        _data.graph.setData(store.getBatteryHistory());
        _data.graph.setLineColor(0x07E0); // Green
        _data.graph.setAutoScale(true);
        break;

    case GraphType::CHANNEL_ACTIVITY:
        _data.graph.setData(store.getChannelActivityHistory());
        _data.graph.setLineColor(0x07FF); // Cyan
        _data.graph.setAutoScale(true);
        break;

    default:
        break;
    }
}
