/**
 * @file app_monitor.cpp
 * @author d4rkmen
 * @brief Monitor widget - live radio packet feed with detail view
 * @version 2.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "app_monitor.h"
#include "apps/utils/theme/theme_define.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "meshtastic/portnums.pb.h"
#include "mesh/node_db.h"
#include "mesh/mesh_service.h"
#include "apps/utils/ui/draw_helper.h"
#include "apps/utils/ui/key_repeat.h"
#include <algorithm>

static const char* TAG = "APP_MONITOR";

#define LIST_ITEM_HEIGHT 14
#define SCROLL_BAR_WIDTH 4
#define SCROLLBAR_MIN_HEIGHT 10
#define COL_SHORT_NAME_WIDTH (4 * 6 + 6)

static const char* HINT_LIST = "[\u2191][\u2193][\u2190][\u2192] [ENTER] [ESC]";
static const char* HINT_DETAIL = "[\u2191][\u2193][\u2190][\u2192] [ESC]";

using namespace MOONCAKE::APPS;

// ============================================================================
// Lifecycle
// ============================================================================

void AppMonitor::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
    _data.view_state = ViewState::PACKET_LIST;
    _data.selected_index = 0;
    _data.scroll_offset = 0;
    _data.last_log_size = 0;
    _data.focused_pkt_id = 0;
    _data.focused_at_bottom = false;
    _data.update_list = true;
    _data.detail_scroll = 0;
    _data.detail_scroll_max = 0;

    hl_text_init(&_data.hint_hl_ctx, _data.hal->canvas(), 20, 1500);
}

void AppMonitor::onResume()
{
    ANIM_APP_OPEN();
    _data.hal->canvas()->fillScreen(THEME_COLOR_BG);
    _data.hal->canvas()->setFont(FONT_12);
    _data.hal->canvas()->setTextSize(1);
    _data.hal->canvas_update();

    _data.view_state = ViewState::PACKET_LIST;
    _data.selected_index = 0;
    _data.scroll_offset = 0;
    _data.last_log_size = 0;
    _data.focused_pkt_id = 0;
    _data.focused_at_bottom = false;
    _data.update_list = true;
}

void AppMonitor::onRunning()
{
    auto& log = Mesh::MeshDataStore::getInstance().getPacketLog();
    uint32_t cur_gen = log.generation();
    if (cur_gen != _data.last_log_size)
    {
        if (_data.view_state == ViewState::PACKET_LIST)
        {
            int total = (int)log.size();
            if (_data.selected_index == 0)
            {
                // At top: stay on newest
            }
            else if (_data.focused_at_bottom)
            {
                _data.selected_index = total - 1;
            }
            else if (_data.focused_pkt_id != 0 && total > 0)
            {
                int found = -1;
                for (int i = 0; i < total; i++)
                {
                    if (log[total - 1 - i].id == _data.focused_pkt_id)
                    {
                        found = i;
                        break;
                    }
                }
                _data.selected_index = (found >= 0) ? found : total - 1;
            }
            _data.update_list = true;
        }
        _data.last_log_size = cur_gen;
    }

    bool updated = false;

    switch (_data.view_state)
    {
    case ViewState::PACKET_LIST:
        updated |= _render_packet_list();
        updated |= _render_list_hint();
        if (updated)
            _data.hal->canvas_update();
        _handle_list_input();
        break;

    case ViewState::PACKET_DETAIL:
        updated |= _render_packet_detail();
        updated |= _render_detail_hint();
        if (updated)
            _data.hal->canvas_update();
        _handle_detail_input();
        break;
    }
}

void AppMonitor::onDestroy() { hl_text_free(&_data.hint_hl_ctx); }

// ============================================================================
// Helpers
// ============================================================================

const char* AppMonitor::_port_name(uint8_t port)
{
    switch (port)
    {
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
        return "TEXT";
    case meshtastic_PortNum_POSITION_APP:
        return "POS";
    case meshtastic_PortNum_NODEINFO_APP:
        return "NODE";
    case meshtastic_PortNum_TELEMETRY_APP:
        return "TELE";
    case meshtastic_PortNum_ROUTING_APP:
        return "ROUT";
    case meshtastic_PortNum_ADMIN_APP:
        return "ADMN";
    case meshtastic_PortNum_TRACEROUTE_APP:
        return "TRAC";
    case meshtastic_PortNum_WAYPOINT_APP:
        return "WAPT";
    case meshtastic_PortNum_NEIGHBORINFO_APP:
        return "NEIG";
    case meshtastic_PortNum_STORE_FORWARD_APP:
        return "S&F";
    case meshtastic_PortNum_RANGE_TEST_APP:
        return "RNGE";
    case meshtastic_PortNum_MAP_REPORT_APP:
        return "MAP";
    case meshtastic_PortNum_DETECTION_SENSOR_APP:
        return "SENS";
    case meshtastic_PortNum_REMOTE_HARDWARE_APP:
        return "HWRD";
    case meshtastic_PortNum_ATAK_PLUGIN:
        return "ATAK";
    case meshtastic_PortNum_SERIAL_APP:
        return "SERL";
    case meshtastic_PortNum_PAXCOUNTER_APP:
        return "PAX";
    case 0:
        return "";
    default:
        return nullptr;
    }
}

const char* AppMonitor::_direction_str(const Mesh::PacketLogEntry& pkt) { return pkt.is_tx ? "TX>" : "RX<"; }

uint32_t AppMonitor::_direction_color(const Mesh::PacketLogEntry& pkt)
{
    return lgfx::v1::convert_to_rgb888(pkt.is_tx ? TFT_GREEN : TFT_CYAN);
}

static uint32_t packet_id_color(uint32_t id)
{
    uint8_t r = (uint8_t)((id * 37) ^ (id >> 8));
    uint8_t g = (uint8_t)((id * 59) ^ (id >> 16));
    uint8_t b = (uint8_t)((id * 101) ^ (id >> 24));
    r = 40 + (r % 80);
    g = 40 + (g % 80);
    b = 40 + (b % 80);
    return (r << 16) | (g << 8) | b;
}

// ============================================================================
// Packet List Rendering
// ============================================================================

bool AppMonitor::_render_packet_list()
{
    if (!_data.update_list)
        return false;
    _data.update_list = false;

    auto* canvas = _data.hal->canvas();
    canvas->fillScreen(THEME_COLOR_BG);
    canvas->setFont(FONT_12);

    auto& log = Mesh::MeshDataStore::getInstance().getPacketLog();
    // auto* nodedb = _data.hal->nodedb();
    int total = (int)log.size();
    if (total == 0)
    {
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawCenterString("<no packets>", canvas->width() / 2, canvas->height() / 2 - 6);
        return true;
    }

    // Clamp selection (newest = total-1)
    if (_data.selected_index >= total)
        _data.selected_index = total - 1;
    if (_data.selected_index < 0)
        _data.selected_index = 0;

    _data.focused_pkt_id = log[total - 1 - _data.selected_index].id;
    _data.focused_at_bottom = (_data.selected_index == total - 1);

    const int item_y_start = 0;
    const int max_visible = (canvas->height() - item_y_start - 9) / (LIST_ITEM_HEIGHT + 1);

    // Adjust scroll window
    if (_data.selected_index < _data.scroll_offset)
        _data.scroll_offset = _data.selected_index;
    if (_data.selected_index >= _data.scroll_offset + max_visible)
        _data.scroll_offset = _data.selected_index - max_visible + 1;

    uint32_t our_id = _data.hal->mesh() ? _data.hal->mesh()->getNodeId() : 0;
    int y = item_y_start;
    for (int i = 0; i < max_visible && (_data.scroll_offset + i) < total; i++)
    {
        int idx = _data.scroll_offset + i;
        // Ring buffer: [0]=oldest, [total-1]=newest; display newest at top
        const auto& pkt = log[total - 1 - idx];
        bool selected = (idx == _data.selected_index);

        uint32_t bg = selected ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG;
        uint32_t fg = selected ? THEME_COLOR_SELECTED : THEME_COLOR_UNSELECTED;

        if (selected)
            canvas->fillRect(2, y, canvas->width() - 4, LIST_ITEM_HEIGHT, THEME_COLOR_BG_SELECTED);
        // check if packet involves us
        bool involves_us = pkt.is_tx ? (pkt.from == our_id) : (pkt.to == our_id || pkt.to == 0xFFFFFFFF);
        // Direction indicator (TX>/RX<)
        uint32_t direction_color = _direction_color(pkt);
        canvas->setTextColor(selected ? THEME_COLOR_SELECTED : direction_color, bg);
        canvas->drawString(_direction_str(pkt), 4, y + 1);

        // Port name with packet-ID color band
        const char* pname = nullptr;
        char port_buf[6];
        if (pkt.crc_error)
        {
            pname = "CRC";
        }
        else
        {
            pname = _port_name(pkt.port);
            if (!pname)
            {
                snprintf(port_buf, sizeof(port_buf), "P%02X", pkt.port);
                pname = port_buf;
            }
        }
        uint32_t id_bg = pkt.crc_error ? THEME_COLOR_SIGNAL_NONE : (pkt.port == 0 ? bg : packet_id_color(pkt.id));
        canvas->fillRect(27, y, 29, LIST_ITEM_HEIGHT, id_bg);
        canvas->setTextColor(fg, id_bg);
        canvas->drawCenterString(pname, 28 + (28 - 2) / 2, y + 1);

        // From node (short hex)
        bool known_from = pkt.is_tx ? (pkt.from == our_id) : false;
        uint32_t nc = UTILS::UI::node_color(pkt.from);
        uint32_t ntc = UTILS::UI::node_text_color(pkt.from);
        std::string from_label;
        Mesh::NodeInfo ni;
        if (pkt.crc_error && pkt.from == 0)
        {
            from_label = "????";
        }
        else if (_data.hal->mesh() && _data.hal->mesh()->getNode(pkt.from, ni))
        {
            known_from = true;
            from_label = Mesh::NodeDB::getLabel(ni);
        }
        else
            from_label = std::format("{:04x}", (unsigned)(pkt.from & 0xFFFF));
        // coloring only known nodes
        if (selected || !known_from)
        {
            nc = THEME_COLOR_BG_SELECTED_DARK;
            ntc = THEME_COLOR_SELECTED;
        }
        int pill_w = 4 * 6 + 4;
        int pill_x = 60;
        canvas->fillRoundRect(pill_x, y, pill_w, LIST_ITEM_HEIGHT, 4, nc);
        canvas->setTextColor(ntc, nc);
        canvas->drawCenterString(from_label.c_str(), pill_x + pill_w / 2, y + 1);
        // arrow: colored if packet involves us, orange otherwise
        uint32_t arrow_color = involves_us ? direction_color : lgfx::v1::convert_to_rgb888(TFT_ORANGE);
        canvas->setTextColor(selected ? THEME_COLOR_SELECTED : arrow_color, bg);
        canvas->drawString("\u2192", pill_x + pill_w + 1, y + 1);

        // To node
        bool known_to = pkt.is_tx ? false : (pkt.to == our_id);
        int pill2_x = pill_x + pill_w + 10;
        uint32_t nc2 = UTILS::UI::node_color(pkt.to);
        uint32_t ntc2 = UTILS::UI::node_text_color(pkt.to);
        std::string to_label;

        if (pkt.crc_error && pkt.to == 0)
            to_label = "????";
        else if (pkt.to == 0xFFFFFFFF)
            to_label = "\u2192\u2192\u2192";
        else if (_data.hal->mesh() && _data.hal->mesh()->getNode(pkt.to, ni))
        {
            known_to = true;
            to_label = Mesh::NodeDB::getLabel(ni);
        }
        else
            to_label = std::format("{:04x}", (unsigned)(pkt.to & 0xFFFF));
        if (selected || !known_to)
        {
            nc2 = THEME_COLOR_BG_SELECTED_DARK;
            ntc2 = THEME_COLOR_SELECTED;
        }

        canvas->fillRoundRect(pill2_x, y, pill_w, LIST_ITEM_HEIGHT, 4, nc2);
        canvas->setTextColor(ntc2, nc2);
        canvas->drawCenterString(to_label.c_str(), pill2_x + pill_w / 2, y + 1);

        // Hops info
        std::string hop_str;
        if (pkt.hop_start > 0)
        {
            int hops_used = pkt.hop_start - pkt.hop_limit;
            hop_str = std::format("{:d}/{:d}", hops_used, pkt.hop_start);
        }
        else
            hop_str = std::format("[{:d}]", pkt.hop_limit);
        canvas->setTextColor(selected ? THEME_COLOR_SELECTED : direction_color, bg);
        canvas->drawString(hop_str.c_str(), pill2_x + pill_w + 2, y + 1);

        // channel (for broadcasts and traces)
        if (pkt.channel != 0)
        {
            std::string channel_str = std::format("#{:02X}", pkt.channel);
            canvas->setTextColor(selected ? THEME_COLOR_SELECTED : direction_color, bg);
            canvas->drawRightString(channel_str.c_str(), canvas->width() - 68, y + 1);
        }
        // Size
        std::string size_str = std::format("{:d}B", pkt.size);
        canvas->setTextColor(selected ? THEME_COLOR_SELECTED : direction_color, bg);
        canvas->drawRightString(size_str.c_str(), canvas->width() - 38, y + 1);

        // Signal: 2-line SNR/RSSI widget (RX only)
        if (!pkt.is_tx && (pkt.snr != 0.0f || pkt.rssi != 0))
        {
            int sig_x = canvas->width() - 6;
            canvas->setFont(FONT_6);

            char snr_buf[10];
            snprintf(snr_buf, sizeof(snr_buf), "%.1f", pkt.snr);
            uint32_t snr_color = (pkt.snr > -7.5f)     ? THEME_COLOR_SIGNAL_GOOD
                                 : (pkt.snr > -13.0f)  ? THEME_COLOR_SIGNAL_FAIR
                                 : (pkt.snr >= -15.0f) ? THEME_COLOR_SIGNAL_BAD
                                                       : THEME_COLOR_SIGNAL_NONE;
            canvas->setTextColor(selected ? THEME_COLOR_SELECTED : snr_color, bg);
            canvas->drawRightString(snr_buf, sig_x, y + 1);

            char rssi_buf[10];
            snprintf(rssi_buf, sizeof(rssi_buf), "%d", (int)pkt.rssi);
            uint32_t rssi_color = (pkt.rssi > -115)   ? THEME_COLOR_SIGNAL_GOOD
                                  : (pkt.rssi > -120) ? THEME_COLOR_SIGNAL_FAIR
                                  : (pkt.rssi > -126) ? THEME_COLOR_SIGNAL_BAD
                                                      : THEME_COLOR_SIGNAL_NONE;
            canvas->setTextColor(selected ? THEME_COLOR_SELECTED : rssi_color, bg);
            canvas->drawRightString(rssi_buf, sig_x, y + 8);

            canvas->setFont(FONT_12);
        }

        y += LIST_ITEM_HEIGHT + 1;
    }

    // Scrollbar
    UTILS::UI::draw_scrollbar(canvas,
                              canvas->width() - SCROLL_BAR_WIDTH - 1,
                              item_y_start,
                              SCROLL_BAR_WIDTH,
                              max_visible * (LIST_ITEM_HEIGHT + 1),
                              total,
                              max_visible,
                              _data.scroll_offset,
                              SCROLLBAR_MIN_HEIGHT);

    return true;
}

bool AppMonitor::_render_list_hint()
{
    return hl_text_render(&_data.hint_hl_ctx,
                          HINT_LIST,
                          0,
                          _data.hal->canvas()->height() - 9,
                          TFT_DARKGREY,
                          TFT_WHITE,
                          THEME_COLOR_BG);
}

// ============================================================================
// Packet Detail Rendering
// ============================================================================

bool AppMonitor::_render_packet_detail()
{
    if (!_data.update_list)
        return false;
    _data.update_list = false;

    auto* canvas = _data.hal->canvas();
    canvas->fillScreen(THEME_COLOR_BG);
    canvas->setFont(FONT_12);

    const auto& pkt = _data.detail_pkt;

    // Header
    canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    canvas->drawString("<", 2, 0);
    canvas->drawString("Packet detail", 14, 0);

    // Direction badge
    canvas->setTextColor(_direction_color(pkt), THEME_COLOR_BG);
    canvas->drawRightString(_direction_str(pkt), canvas->width() - 2, 0);

    canvas->drawFastHLine(0, 14, canvas->width(), THEME_COLOR_BG_SELECTED);

    // Build detail rows
    struct DetailRow
    {
        const char* label;
        char value[32];
        uint32_t value_color;
        bool is_header; // section divider row
    };

    DetailRow rows[28];
    int row_count = 0;

    auto add_row = [&](const char* label, const char* val, uint32_t color = THEME_COLOR_UNSELECTED)
    {
        if (row_count < 28)
        {
            rows[row_count].label = label;
            strncpy(rows[row_count].value, val, 31);
            rows[row_count].value[31] = '\0';
            rows[row_count].value_color = color;
            rows[row_count].is_header = false;
            row_count++;
        }
    };

    auto add_header = [&](const char* label)
    {
        if (row_count < 28)
        {
            rows[row_count].label = label;
            rows[row_count].value[0] = '\0';
            rows[row_count].value_color = lgfx::v1::convert_to_rgb888(TFT_ORANGE);
            rows[row_count].is_header = true;
            row_count++;
        }
    };

    auto* nodedb = _data.hal->nodedb();
    uint32_t our_id = _data.hal->mesh() ? _data.hal->mesh()->getNodeId() : 0;
    // CRC error notice
    if (pkt.crc_error)
    {
        add_row("Status", "CRC error (corrupted)", (uint32_t)THEME_COLOR_SIGNAL_NONE);
    }
    // From
    {
        char buf[32];
        Mesh::NodeInfo ni;
        if (pkt.crc_error && pkt.from == 0)
            snprintf(buf, sizeof(buf), "unknown (header corrupt)");
        else if (nodedb && nodedb->getNode(pkt.from, ni) && ni.info.user.short_name[0])
            snprintf(buf, sizeof(buf), "%s (!%08lx)", ni.info.user.short_name, (unsigned long)pkt.from);
        else if (pkt.from == our_id && _data.hal->mesh() && _data.hal->mesh()->getConfig().short_name[0])
            snprintf(buf, sizeof(buf), "%s (!%08lx)", _data.hal->mesh()->getConfig().short_name, (unsigned long)pkt.from);
        else
            snprintf(buf, sizeof(buf), "!%08lx", (unsigned long)pkt.from);
        add_row("From", buf);
    }
    // To
    {
        char buf[32];
        Mesh::NodeInfo ni;
        if (pkt.crc_error && pkt.to == 0)
            snprintf(buf, sizeof(buf), "unknown (header corrupt)");
        else if (pkt.to == 0xFFFFFFFF)
            snprintf(buf, sizeof(buf), "BROADCAST");
        else if (nodedb && nodedb->getNode(pkt.to, ni) && ni.info.user.short_name[0])
            snprintf(buf, sizeof(buf), "%s (!%08lx)", ni.info.user.short_name, (unsigned long)pkt.to);
        else if (pkt.to == our_id && _data.hal->mesh() && _data.hal->mesh()->getConfig().short_name[0])
            snprintf(buf, sizeof(buf), "%s (!%08lx)", _data.hal->mesh()->getConfig().short_name, (unsigned long)pkt.to);
        else
            snprintf(buf, sizeof(buf), "!%08lx", (unsigned long)pkt.to);
        add_row("To", buf);
    }
    // Packet ID
    {
        char buf[12];
        snprintf(buf, sizeof(buf), "0x%08lx", (unsigned long)pkt.id);
        add_row("ID", buf);
    }
    // Port
    {
        const char* pname = pkt.crc_error ? "CRC" : _port_name(pkt.port);
        char buf[16];
        if (pname)
            snprintf(buf, sizeof(buf), "%s (%d)", pname, pkt.port);
        else
            snprintf(buf, sizeof(buf), "%d", pkt.port);
        add_row("Port",
                buf,
                pkt.crc_error ? (uint32_t)THEME_COLOR_SIGNAL_NONE
                              : lgfx::v1::convert_to_rgb888((pkt.decoded ? TFT_WHITE : TFT_DARKGREY)));
    }
    // Size
    {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d bytes", pkt.size);
        add_row("Size", buf);
    }
    // Channel
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "0x%02X", pkt.channel);
        add_row("Ch", buf);
    }
    // Hops
    {
        char buf[16];
        if (pkt.hop_start > 0)
        {
            int hops_used = pkt.hop_start - pkt.hop_limit;
            snprintf(buf, sizeof(buf), "%d/%d", hops_used, pkt.hop_start);
        }
        else
            snprintf(buf, sizeof(buf), "lim %d", pkt.hop_limit);
        add_row("Hops", buf);
    }
    // Want ACK
    add_row("WantACK", pkt.want_ack ? "yes" : "no", lgfx::v1::convert_to_rgb888(pkt.want_ack ? TFT_YELLOW : TFT_DARKGREY));
    // Decoded
    add_row("Decoded", pkt.decoded ? "yes" : "no", lgfx::v1::convert_to_rgb888(pkt.decoded ? TFT_GREEN : TFT_RED));
    // RSSI / SNR (RX only)
    if (!pkt.is_tx)
    {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d dBm", pkt.rssi);
        uint32_t rssi_color;
        if (pkt.rssi > -90)
            rssi_color = THEME_COLOR_SIGNAL_GOOD;
        else if (pkt.rssi > -110)
            rssi_color = THEME_COLOR_SIGNAL_FAIR;
        else if (pkt.rssi > -120)
            rssi_color = THEME_COLOR_SIGNAL_BAD;
        else
            rssi_color = THEME_COLOR_SIGNAL_NONE;
        add_row("RSSI", buf, rssi_color);

        snprintf(buf, sizeof(buf), "%.1f dB", pkt.snr);
        add_row("SNR", buf);
    }

    // Payload section (port-specific decoded data)
    if (pkt.decoded && !pkt.crc_error)
    {
        auto& store = Mesh::MeshDataStore::getInstance();
        switch (pkt.port)
        {
        case meshtastic_PortNum_TEXT_MESSAGE_APP:
        {
            add_header("TEXT");
            std::string text_found;
            if (pkt.to != 0xFFFFFFFF)
            {
                // DM: peer is the other party
                uint32_t peer = pkt.is_tx ? pkt.to : pkt.from;
                store.forEachDMMessage(peer,
                                       [&](uint32_t, const Mesh::TextMessage& msg) -> bool
                                       {
                                           if (msg.id == pkt.id)
                                           {
                                               text_found = msg.text;
                                               return false;
                                           }
                                           return true;
                                       });
            }
            else
            {
                // Channel broadcast: search all channels
                for (uint8_t ch = 0; ch < 8 && text_found.empty(); ch++)
                {
                    store.forEachChannelMessage(ch,
                                                [&](uint32_t, const Mesh::TextMessage& msg) -> bool
                                                {
                                                    if (msg.id == pkt.id)
                                                    {
                                                        text_found = msg.text;
                                                        return false;
                                                    }
                                                    return true;
                                                });
                }
            }
            if (!text_found.empty())
            {
                char tbuf[32];
                strncpy(tbuf, text_found.c_str(), 31);
                tbuf[31] = '\0';
                add_row("Text", tbuf);
                if (text_found.size() > 31)
                    add_row("", "...(truncated)", lgfx::v1::convert_to_rgb888(TFT_DARKGREY));
            }
            else
            {
                add_row("Text", "(not cached)", lgfx::v1::convert_to_rgb888(TFT_DARKGREY));
            }
            break;
        }
        case meshtastic_PortNum_POSITION_APP:
        {
            add_header("POSITION");
            Mesh::NodeInfo ni;
            if (_data.hal->mesh() && _data.hal->mesh()->getNode(pkt.from, ni) && ni.info.has_position)
            {
                const auto& pos = ni.info.position;
                char buf[32];
                if (pos.has_latitude_i)
                {
                    snprintf(buf, sizeof(buf), "%.5f", pos.latitude_i * 1e-7);
                    add_row("Lat", buf);
                }
                if (pos.has_longitude_i)
                {
                    snprintf(buf, sizeof(buf), "%.5f", pos.longitude_i * 1e-7);
                    add_row("Lon", buf);
                }
                if (pos.has_altitude)
                {
                    snprintf(buf, sizeof(buf), "%dm", (int)pos.altitude);
                    add_row("Alt", buf);
                }
                if (pos.sats_in_view > 0)
                {
                    snprintf(buf, sizeof(buf), "%u", (unsigned)pos.sats_in_view);
                    add_row("Sats", buf);
                }
            }
            else
            {
                add_row("", "(no position data)", lgfx::v1::convert_to_rgb888(TFT_DARKGREY));
            }
            break;
        }
        case meshtastic_PortNum_NODEINFO_APP:
        {
            add_header("NODEINFO");
            Mesh::NodeInfo ni;
            if (_data.hal->mesh() && _data.hal->mesh()->getNode(pkt.from, ni) && ni.info.has_user)
            {
                const auto& user = ni.info.user;
                if (user.long_name[0])
                    add_row("Name", user.long_name);
                if (user.short_name[0])
                    add_row("Short", user.short_name);
                add_row("Role", Mesh::NodeDB::getRoleName(user.role));
            }
            else
            {
                add_row("", "(no user data)", lgfx::v1::convert_to_rgb888(TFT_DARKGREY));
            }
            break;
        }
        case meshtastic_PortNum_TELEMETRY_APP:
        {
            add_header("TELEMETRY");
            Mesh::NodeInfo ni;
            if (_data.hal->mesh() && _data.hal->mesh()->getNode(pkt.from, ni) && ni.info.has_device_metrics)
            {
                const auto& dm = ni.info.device_metrics;
                char buf[32];
                if (dm.has_battery_level)
                {
                    snprintf(buf, sizeof(buf), "%u%%", (unsigned)dm.battery_level);
                    uint32_t bc = lgfx::v1::convert_to_rgb888(dm.battery_level > 50   ? TFT_GREEN
                                                              : dm.battery_level > 20 ? TFT_YELLOW
                                                                                      : TFT_RED);
                    add_row("Batt", buf, bc);
                }
                if (dm.has_voltage)
                {
                    snprintf(buf, sizeof(buf), "%.2fV", dm.voltage);
                    add_row("Volt", buf);
                }
                if (dm.has_uptime_seconds)
                {
                    uint32_t s = dm.uptime_seconds;
                    if (s >= 86400)
                        snprintf(buf, sizeof(buf), "%lud %luh", s / 86400, (s % 86400) / 3600);
                    else if (s >= 3600)
                        snprintf(buf, sizeof(buf), "%luh %lum", s / 3600, (s % 3600) / 60);
                    else
                        snprintf(buf, sizeof(buf), "%lum %lus", s / 60, s % 60);
                    add_row("Up", buf);
                }
            }
            else
            {
                add_row("", "(no metrics)", lgfx::v1::convert_to_rgb888(TFT_DARKGREY));
            }
            break;
        }
        case meshtastic_PortNum_ROUTING_APP:
        {
            add_header("ROUTING");
            bool is_ack = (pkt.routing_error == 0);
            add_row("Type", is_ack ? "ACK" : "NACK",
                    lgfx::v1::convert_to_rgb888(is_ack ? TFT_GREEN : TFT_RED));
            if (!is_ack)
            {
                static const struct { uint8_t code; const char* name; } routing_errors[] = {
                    {1,  "NO_ROUTE"},
                    {2,  "GOT_NAK"},
                    {3,  "TIMEOUT"},
                    {4,  "NO_IFACE"},
                    {5,  "MAX_RETRY"},
                    {6,  "NO_CHAN"},
                    {7,  "TOO_LARGE"},
                    {8,  "NO_RESP"},
                    {9,  "DUTY_LIM"},
                    {32, "BAD_REQ"},
                    {33, "UNAUTH"},
                    {34, "PKI_FAIL"},
                    {35, "NO_KEY"},
                    {36, "BAD_SESS"},
                    {37, "KEY_UNAUTH"},
                    {38, "RATE_LIM"},
                    {39, "PKI_SEND"},
                };
                const char* err_name = nullptr;
                for (const auto& e : routing_errors)
                    if (e.code == pkt.routing_error) { err_name = e.name; break; }
                char ebuf[16];
                if (err_name)
                    snprintf(ebuf, sizeof(ebuf), "%s", err_name);
                else
                    snprintf(ebuf, sizeof(ebuf), "ERR_%u", (unsigned)pkt.routing_error);
                add_row("Error", ebuf, lgfx::v1::convert_to_rgb888(TFT_RED));
            }
            if (pkt.request_id != 0)
            {
                char rbuf[12];
                snprintf(rbuf, sizeof(rbuf), "0x%08lx", (unsigned long)pkt.request_id);
                add_row("For", rbuf, lgfx::v1::convert_to_rgb888(TFT_DARKGREY));
            }
            break;
        }
        case meshtastic_PortNum_TRACEROUTE_APP:
        {
            add_header("TRACEROUTE");
            // Direction: TX = outgoing request, RX = incoming response
            add_row("Dir", pkt.is_tx ? "Request" : "Response");
            // Hops consumed so far
            if (pkt.hop_start > 0)
            {
                char buf[16];
                int used = pkt.hop_start - pkt.hop_limit;
                snprintf(buf, sizeof(buf), "%d / %d", used, pkt.hop_start);
                add_row("Hops", buf);
            }
            break;
        }
        case meshtastic_PortNum_WAYPOINT_APP:
        {
            add_header("WAYPOINT");
            add_row("", "(position)", lgfx::v1::convert_to_rgb888(TFT_DARKGREY));
            break;
        }
        case meshtastic_PortNum_NEIGHBORINFO_APP:
        {
            add_header("NEIGHBORINFO");
            add_row("", "(mesh topology)", lgfx::v1::convert_to_rgb888(TFT_DARKGREY));
            break;
        }
        case meshtastic_PortNum_RANGE_TEST_APP:
        {
            add_header("RANGE TEST");
            add_row("", "(range probe)", lgfx::v1::convert_to_rgb888(TFT_DARKGREY));
            break;
        }
        case meshtastic_PortNum_STORE_FORWARD_APP:
        {
            add_header("STORE&FORWARD");
            add_row("", "(relay queue)", lgfx::v1::convert_to_rgb888(TFT_DARKGREY));
            break;
        }
        case meshtastic_PortNum_ADMIN_APP:
        {
            add_header("ADMIN");
            add_row("", "(encrypted)", lgfx::v1::convert_to_rgb888(TFT_DARKGREY));
            break;
        }
        default:
            break;
        }
    }

    // Render visible rows
    const int row_height = 14;
    const int y_start = 15;
    const int max_visible = (canvas->height() - y_start - 9) / (row_height + 1);

    int max_scroll = std::max(0, row_count - max_visible);
    _data.detail_scroll_max = max_scroll;
    if (_data.detail_scroll > max_scroll)
        _data.detail_scroll = max_scroll;
    if (_data.detail_scroll < 0)
        _data.detail_scroll = 0;

    int y = y_start;
    for (int i = 0; i < max_visible && (_data.detail_scroll + i) < row_count; i++)
    {
        const auto& row = rows[_data.detail_scroll + i];

        if (row.is_header)
        {
            canvas->drawFastHLine(0, y + row_height / 2, canvas->width(), THEME_COLOR_BG_SELECTED);
            canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
            canvas->drawString(row.label, 4, y + 1);
        }
        else
        {
            canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
            canvas->drawString(row.label, 4, y + 1);
            canvas->setTextColor(row.value_color, THEME_COLOR_BG);
            canvas->drawString(row.value, 60, y + 1);
        }

        y += row_height + 1;
    }

    // Scrollbar
    UTILS::UI::draw_scrollbar(canvas,
                              canvas->width() - SCROLL_BAR_WIDTH - 1,
                              y_start,
                              SCROLL_BAR_WIDTH,
                              max_visible * (row_height + 1),
                              row_count,
                              max_visible,
                              _data.detail_scroll,
                              SCROLLBAR_MIN_HEIGHT);

    return true;
}

bool AppMonitor::_render_detail_hint()
{
    return hl_text_render(&_data.hint_hl_ctx,
                          HINT_DETAIL,
                          0,
                          _data.hal->canvas()->height() - 9,
                          TFT_DARKGREY,
                          TFT_WHITE,
                          THEME_COLOR_BG);
}

// ============================================================================
// Input Handling
// ============================================================================

void AppMonitor::_handle_list_input()
{
    static bool is_repeat = false;
    static uint32_t next_fire_ts = 0;

    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    if (_data.hal->keyboard()->isPressed())
    {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ESC) || _data.hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE) ||
            _data.hal->home_button()->is_pressed())
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_BACKSPACE);
            destroyApp();
            return;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
        {
            auto& log = Mesh::MeshDataStore::getInstance().getPacketLog();
            int total = (int)log.size();
            if (total > 0 && _data.selected_index < total)
            {
                _data.hal->playNextSound();
                _data.hal->keyboard()->waitForRelease(KEY_NUM_ENTER);
                _data.detail_pkt = log[total - 1 - _data.selected_index];
                _data.detail_scroll = 0;
                _data.view_state = ViewState::PACKET_DETAIL;
                _data.update_list = true;
                hl_text_reset(&_data.hint_hl_ctx);
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
        {
            auto& log = Mesh::MeshDataStore::getInstance().getPacketLog();
            int total = (int)log.size();
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.selected_index < total - 1)
            {
                _data.selected_index++;
                _data.hal->playNextSound();
                _data.update_list = true;
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_UP))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.selected_index > 0)
            {
                _data.selected_index--;
                _data.hal->playNextSound();
                _data.update_list = true;
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
        {
            auto& log = Mesh::MeshDataStore::getInstance().getPacketLog();
            int total = (int)log.size();
            int page = (_data.hal->canvas()->height() - 9) / (LIST_ITEM_HEIGHT + 1);
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.selected_index < total - 1)
            {
                _data.selected_index = (_data.selected_index + page < total - 1) ? _data.selected_index + page : total - 1;
                _data.hal->playNextSound();
                _data.update_list = true;
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.selected_index > 0)
            {
                int page = (_data.hal->canvas()->height() - 9) / (LIST_ITEM_HEIGHT + 1);
                _data.selected_index = (_data.selected_index - page > 0) ? _data.selected_index - page : 0;
                _data.hal->playNextSound();
                _data.update_list = true;
            }
        }
    }
    else
    {
        is_repeat = false;
    }
}

void AppMonitor::_handle_detail_input()
{
    static bool is_repeat = false;
    static uint32_t next_fire_ts = 0;

    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    if (_data.hal->keyboard()->isPressed())
    {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ESC) || _data.hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ESC);
            _data.view_state = ViewState::PACKET_LIST;
            _data.update_list = true;
            hl_text_reset(&_data.hint_hl_ctx);
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.detail_scroll < _data.detail_scroll_max)
            {
                _data.detail_scroll++;
                _data.hal->playNextSound();
                _data.update_list = true;
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_UP))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.detail_scroll > 0)
            {
                _data.detail_scroll--;
                _data.hal->playNextSound();
                _data.update_list = true;
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.detail_scroll < _data.detail_scroll_max)
            {
                const int page = ((_data.hal->canvas()->height() - 15 - 9) / 15);
                _data.detail_scroll = std::min(_data.detail_scroll + page, _data.detail_scroll_max);
                _data.hal->playNextSound();
                _data.update_list = true;
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.detail_scroll > 0)
            {
                const int page = ((_data.hal->canvas()->height() - 15 - 9) / 15);
                _data.detail_scroll = std::max(_data.detail_scroll - page, 0);
                _data.hal->playNextSound();
                _data.update_list = true;
            }
        }
    }
    else
    {
        is_repeat = false;
    }
}
