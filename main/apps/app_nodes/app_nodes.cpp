/**
 * @file app_nodes.cpp
 * @author d4rkmen
 * @brief Nodes widget implementation
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "app_nodes.h"
#include "apps/utils/theme/theme_define.h"
#include "apps/utils/ui/dialog.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "keyboard/keyboard.h"
#include "lgfx/v1/misc/enum.hpp"
#include "mbedtls/base64.h"
#include "mesh/mesh_service.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/mesh.pb.h"
#include <algorithm>
#include <time.h>

#include "apps/utils/text/text_utils.h"
#include "apps/utils/ui/draw_helper.h"
#include "apps/utils/ui/key_repeat.h"
#include "assets/trace_pending.h"
#include "assets/trace_ok.h"
#include "assets/trace_err.h"

static const char* TAG = "APP_NODES";

static const char* HINT_LIST = "[Fn] [\u2191][\u2193][\u2190][\u2192] [1..8.F.T.R.N.P][ENT][DEL][ESC]";
static const char* HINT_LIST_FN = "[\u2191]HOME [\u2193]END [T]RACE";
static const char* HINT_DM = "[Fn] [^] [\u2191][\u2193][\u2190][\u2192] [I] [ENTER][DEL] [ESC]";
static const char* HINT_DM_FN = "[\u2191]HOME [\u2193]END";
static const char* HINT_DETAIL = "[T]RACE [ENTER]DM [ESC]";
static const char* HINT_TR_LOG = "[\u2191][\u2193][\u2190][\u2192] [ENTER] [T]RACE [ESC]";
static const char* HINT_TR_DETAIL = "[\u2191][\u2193][\u2190][\u2192] [ESC]";

// Sort order selection dialog
static const std::vector<std::string> sort_labels = {
    "None", "Short name", "Long name", "Role", "Signal", "Hops away", "Last seen", "Favorites first"};

using UTILS::TEXT::utf8_char_count;
using UTILS::TEXT::utf8_char_len;
using UTILS::TEXT::utf8_truncate_len;
using UTILS::TEXT::wrap_text;

// UI Constants - compact layout matching flood app
#define SCROLL_BAR_WIDTH 4
#define LIST_HEADER_HEIGHT 0
#define LIST_ITEM_HEIGHT 14
#define LIST_ITEM_LEFT_PADDING 4
#define LIST_ICON_WIDTH 12
#define LIST_ICON_HEIGHT 12
#define LIST_SCROLL_PAUSE 1000
#define LIST_SCROLL_SPEED 25
#define LIST_MAX_VISIBLE_ITEMS 7
#define LIST_MAX_DISPLAY_CHARS 10
#define SCROLLBAR_MIN_HEIGHT 10

// DM chat layout constants (same as flood app)
#define DM_HEADER_HEIGHT 14
#define DM_FOOTER_HEIGHT 9
#define DM_ITEM_HEIGHT 12
#define DM_MAX_VISIBLE_LINES 7

// Column widths
#define COL_SHORT_NAME_WIDTH (4 * 6 + 6)       // 4 chars + padding
#define COL_SHORT_RELAY_NAME_WIDTH (4 * 4 + 4) // 4 chars + padding
#define COL_ROLE_WIDTH LIST_ICON_WIDTH
#define COL_KEY_WIDTH LIST_ICON_WIDTH      // Encryption key type icon
#define COL_BATTERY_WIDTH 14               // Battery icon (12 + 2 tip)
#define COL_POSITION_WIDTH LIST_ICON_WIDTH // Position/GPS icon
#define COL_SIGNAL_GAUGE_WIDTH 4           // Signal bar gauge
#define COL_SIGNAL_TEXT_WIDTH (3 * 7)      // SNR/RSSI
#define COL_HOPS_WIDTH LIST_ICON_WIDTH
#define COL_LAST_SEEN_WIDTH (3 * 6) // "99m" etc
#define COL_FAVORITE_WIDTH 8        // Favorite star icon

// Role icon colors (matching docs/nodes.svelte)
#define ROLE_COLOR_BLUE (uint32_t)(0x3B82F6)   // bg-blue-500 - Client, Router_Client, Client_Base
#define ROLE_COLOR_INDIGO (uint32_t)(0x6366F1) // bg-indigo-500 - Client_Mute, Tracker, Sensor, TAK, etc.
#define ROLE_COLOR_RED (uint32_t)(0xEF4444)    // bg-red-500 - Router, Repeater, Router_Late

// Role icon rendering helper
struct RoleInfo
{
    const char* label;
    uint32_t bg_color;
    uint32_t text_color;
};

static RoleInfo _get_role_info(meshtastic_Config_DeviceConfig_Role role)
{
    switch (role)
    {
    case meshtastic_Config_DeviceConfig_Role_CLIENT:
        return {"C", ROLE_COLOR_BLUE, (uint32_t)0xFFFFFF};
    case meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE:
        return {"CM", ROLE_COLOR_INDIGO, (uint32_t)0xA5B4FC}; // text-indigo-300
    case meshtastic_Config_DeviceConfig_Role_ROUTER:
        return {"R", ROLE_COLOR_RED, (uint32_t)0xFECACA}; // text-red-200
    case meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT:
        return {"RC", ROLE_COLOR_BLUE, (uint32_t)0xFFFFFF};
    case meshtastic_Config_DeviceConfig_Role_REPEATER:
        return {"Re", ROLE_COLOR_RED, (uint32_t)0xFECACA};
    case meshtastic_Config_DeviceConfig_Role_TRACKER:
        return {"T", ROLE_COLOR_INDIGO, (uint32_t)0xA5B4FC};
    case meshtastic_Config_DeviceConfig_Role_SENSOR:
        return {"S", ROLE_COLOR_INDIGO, (uint32_t)0xA5B4FC};
    case meshtastic_Config_DeviceConfig_Role_TAK:
        return {"TK", ROLE_COLOR_INDIGO, (uint32_t)0xA5B4FC};
    case meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN:
        return {"CH", ROLE_COLOR_INDIGO, (uint32_t)0xA5B4FC};
    case meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND:
        return {"LF", ROLE_COLOR_INDIGO, (uint32_t)0xA5B4FC};
    case meshtastic_Config_DeviceConfig_Role_TAK_TRACKER:
        return {"TT", ROLE_COLOR_INDIGO, (uint32_t)0xA5B4FC};
    case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:
        return {"RL", ROLE_COLOR_RED, (uint32_t)0xFECACA};
    case meshtastic_Config_DeviceConfig_Role_CLIENT_BASE:
        return {"CB", ROLE_COLOR_BLUE, (uint32_t)0xFFFFFF};
    default:
        return {"?", THEME_COLOR_BG_DARK, (uint32_t)0xFFFFFF};
    }
}

// Signal bars for detail view
#define SIGNAL_BAR_WIDTH 3
#define SIGNAL_BAR_GAP 1

static bool is_repeat = false;
static uint32_t next_fire_ts = 0xFFFFFFFF;

using namespace MOONCAKE::APPS;

void AppNodes::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
    _data.view_state = ViewState::NODE_LIST;
    _data.selected_index = 0;
    _data.scroll_offset = 0;
    _data.total_node_count = 0;
    _data.list_selected_node_id = 0;
    _data.update_list = true;
    _data.last_nodedb_change = 0;
    _data.last_msgstore_change = 0;
    _data.dm_msg_count = 0;
    _data.dm_cur_line = 0;
    _data.dm_total_lines = 0;
    _data.dm_chars_per_line = 20;
    _data.selected_node_valid = false;
    _data.sort_order = Mesh::SortOrder::LAST_HEARD;

    // Initialize scrolling text context for node names (FONT_12)
    scroll_text_init_ex(&_data.name_scroll_ctx,
                        _data.hal->canvas(),
                        LIST_MAX_DISPLAY_CHARS * 6, // FONT_12 width
                        12,                         // FONT_12 height
                        LIST_SCROLL_SPEED,
                        LIST_SCROLL_PAUSE,
                        FONT_12);
    hl_text_init(&_data.hint_hl_ctx, _data.hal->canvas(), 20, 1500);
}

void AppNodes::onResume()
{
    ANIM_APP_OPEN();
    _data.hal->canvas()->fillScreen(THEME_COLOR_BG);
    _data.hal->canvas()->setFont(FONT_12);
    _data.hal->canvas()->setTextSize(1);
    _data.hal->canvas_update();

    _data.view_state = ViewState::NODE_LIST;
    _data.selected_index = 0;
    _data.scroll_offset = 0;
    _data.list_selected_node_id = 0;
    _data.update_list = true;
    _data.last_nodedb_change = 0;
    _data.last_msgstore_change = 0;
    _data.dm_msg_count = 0;
    _data.dm_cur_line = 0;
    _data.dm_total_lines = 0;
    _data.dm_chars_per_line = 20;
    _data.selected_node_valid = false;
    _refresh_nodes();
}

void AppNodes::onRunning()
{
    // last view state
    static ViewState last_view_state;
    // On-demand refresh: check change counters instead of periodic timer
    bool nodedb_changed = false;
    bool msgstore_changed = false;
    bool view_state_changed = last_view_state != _data.view_state;
    last_view_state = _data.view_state;

    if (_data.hal->nodedb())
    {
        uint32_t nodedb_ver = _data.hal->nodedb()->getChangeCounter();
        if (nodedb_ver != _data.last_nodedb_change)
        {
            _data.last_nodedb_change = nodedb_ver;
            nodedb_changed = true;
        }
    }

    {
        uint32_t msg_ver = Mesh::MeshDataStore::getInstance().getChangeCounter();
        if (msg_ver != _data.last_msgstore_change)
        {
            _data.last_msgstore_change = msg_ver;
            msgstore_changed = true;
        }
    }

    bool updated = false;

    switch (_data.view_state)
    {
    case ViewState::NODE_LIST:
        if (nodedb_changed || msgstore_changed || view_state_changed)
        {
            _refresh_nodes(); // Refreshes node list including unread badges
        }
        updated |= _render_node_list();
        updated |= _render_scrolling_name(updated);
        updated |= _render_list_hint();
        if (updated)
        {
            _data.hal->canvas_update();
        }
        _handle_node_list_input();
        break;

    case ViewState::NODE_DETAIL:
        updated |= _render_node_detail();
        updated |= _render_node_detail_hint();
        if (updated)
        {
            _data.hal->canvas_update();
        }
        _handle_node_detail_input();
        break;

    case ViewState::DIRECT_MESSAGE:
    {
        auto* canvas = _data.hal->canvas();
        int max_visible = (canvas->height() - DM_HEADER_HEIGHT - DM_FOOTER_HEIGHT) / (DM_ITEM_HEIGHT + 1);
        bool was_at_bottom = (_data.dm_cur_line >= _data.dm_total_lines - max_visible);
        if (msgstore_changed || view_state_changed)
        {
            _refresh_messages();
        }
        if (was_at_bottom)
        {
            _data.dm_cur_line = _data.dm_total_lines > max_visible ? _data.dm_total_lines - max_visible : 0;
        }
        updated |= _render_dm_view();
        updated |= _render_dm_hint();
        if (updated)
        {
            _data.hal->canvas_update();
        }
        _handle_dm_input();
        break;
    }

    case ViewState::TRACEROUTE_LOG:
    {
        auto& store = Mesh::MeshDataStore::getInstance();
        _data.tr_total_count = store.getTraceRouteCount(_data.selected_node_id);

        // Expire pending traceroutes after 30s (throttled to once per second)
        {
            static uint32_t last_expiry_ms = 0;
            uint32_t now_ms = millis();
            if (now_ms - last_expiry_ms > 1000)
            {
                last_expiry_ms = now_ms;
                uint32_t now_ts = (uint32_t)time(nullptr);
                for (int i = (int)_data.tr_total_count - 1; i >= 0; i--)
                {
                    Mesh::TraceRouteResult tr;
                    if (!store.getTraceRouteByIndex(_data.selected_node_id, (uint32_t)i, tr))
                        continue;
                    if (tr.status != Mesh::TraceRouteResult::Status::PENDING)
                        break; // older entries are already resolved
                    if (now_ts - tr.timestamp > 30)
                    {
                        tr.status = Mesh::TraceRouteResult::Status::FAILED;
                        store.updateTraceRoute(_data.selected_node_id, (uint32_t)i, tr);
                        _data.update_list = true;
                    }
                }
            }
        }
        updated |= _render_traceroute_log();
        updated |= _render_traceroute_hint();
        if (updated)
        {
            _data.hal->canvas_update();
        }
        _handle_traceroute_log_input();
        break;
    }

    case ViewState::TRACEROUTE_DETAIL:
        updated |= _render_traceroute_detail();
        updated |= _render_traceroute_detail_hint();
        if (updated)
        {
            _data.hal->canvas_update();
        }
        _handle_traceroute_detail_input();
        break;
    }
}

void AppNodes::onDestroy()
{
    scroll_text_free(&_data.name_scroll_ctx);
    hl_text_free(&_data.hint_hl_ctx);
}

bool AppNodes::_render_list_hint()
{
    static bool last_fn = false;
    auto c = _data.hal->canvas();
    auto keys_state = _data.hal->keyboard()->keysState();
    // clear before put text
    if (last_fn != keys_state.fn)
    {
        last_fn = keys_state.fn;
        c->fillRect(0, c->height() - 9, c->width(), 10, THEME_COLOR_BG);
    }
    return hl_text_render(&_data.hint_hl_ctx,
                          last_fn ? HINT_LIST_FN : HINT_LIST,
                          0,
                          _data.hal->canvas()->height() - 9,
                          TFT_DARKGREY,
                          TFT_WHITE,
                          THEME_COLOR_BG);
}

bool AppNodes::_render_dm_hint()
{
    static bool last_fn = false;
    auto c = _data.hal->canvas();
    auto keys_state = _data.hal->keyboard()->keysState();
    // clear before put text
    if (last_fn != keys_state.fn)
    {
        last_fn = keys_state.fn;
        c->fillRect(0, c->height() - 9, c->width(), 10, THEME_COLOR_BG);
    }
    return hl_text_render(&_data.hint_hl_ctx,
                          last_fn ? HINT_DM_FN : HINT_DM,
                          0,
                          _data.hal->canvas()->height() - 9,
                          TFT_DARKGREY,
                          TFT_WHITE,
                          THEME_COLOR_BG);
}

void AppNodes::_refresh_nodes()
{
    if (!_data.hal->mesh() || !_data.hal->nodedb())
    {
        _data.total_node_count = 0;
        return;
    }

    _data.total_node_count = _data.hal->nodedb()->getNodeCount();
    // Selection resolution is handled in _render_node_list() before drawing
    _data.update_list = true;
}

void AppNodes::_refresh_messages()
{
    auto& store = Mesh::MeshDataStore::getInstance();
    store.markMessagesRead(_data.selected_node_id, false);
    _refresh_dm_line_counts();
    _data.update_list = true;
}

// Count wrapped lines for a text without allocating wrapped strings vector
static uint16_t count_wrapped_lines(const std::string& text, int chars_per_line)
{
    if (text.empty())
        return 1;

    uint16_t lines = 0;
    size_t pos = 0;
    while (pos < text.length())
    {
        // Check for explicit newline
        size_t nl = text.find('\n', pos);
        if (nl != std::string::npos && nl - pos <= (size_t)chars_per_line)
        {
            lines++;
            pos = nl + 1;
            continue;
        }

        size_t line_len = std::min((size_t)chars_per_line, text.length() - pos);

        if (pos + line_len < text.length() && text[pos + line_len] != '\n')
        {
            size_t last_space = text.rfind(' ', pos + line_len);
            if (last_space != std::string::npos && last_space > pos)
            {
                line_len = last_space - pos + 1;
            }
        }

        lines++;
        pos += line_len;
    }
    return lines > 0 ? lines : 1;
}

void AppNodes::_refresh_dm_line_counts()
{
    auto& store = Mesh::MeshDataStore::getInstance();
    auto* canvas = _data.hal->canvas();

    // Calculate chars per line (same layout as flood chat)
    const int name_col_width = 4 * 6 + 6;
    const int text_start_x = name_col_width + 2;
    const int max_text_width = canvas->width() - text_start_x - SCROLL_BAR_WIDTH - 2;
    _data.dm_chars_per_line = max_text_width / 6; // FONT_12 is 6px per char

    _data.dm_line_counts.clear();
    _data.dm_total_lines = 0;
    _data.dm_msg_count = 0;

    // Single sequential file pass: load one message at a time, count wrapped lines, discard text
    int cpl = _data.dm_chars_per_line;
    _data.dm_msg_count = store.forEachDMMessage(_data.selected_node_id,
                                                [this, cpl](uint32_t /*index*/, const Mesh::TextMessage& msg) -> bool
                                                {
                                                    uint16_t lc = count_wrapped_lines(msg.text, cpl);
                                                    _data.dm_line_counts.push_back(lc);
                                                    _data.dm_total_lines += lc;
                                                    return true; // continue iterating
                                                });
}

// Draw battery icon: level 0-100, >100 means powered
static void draw_battery_icon(lgfx::LovyanGFX* canvas, int x, int y, uint32_t level, bool selected)
{
    uint32_t outline_color = selected ? THEME_COLOR_SELECTED : THEME_COLOR_UNSELECTED;
    // uint32_t bg_color = selected ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG;

    if (level > 100)
    {
        canvas->pushImage(x + 1, y - 2, 12, 12, image_data_pwr, TFT_WHITE);
    }
    else
    {
        // Battery outline
        canvas->drawRoundRect(x, y, 12, 7, 2, outline_color);
        canvas->fillRect(x + 12, y + 2, 2, 3, outline_color); // tip
        // Calculate fill level (1-5 segments of 2px each)
        uint8_t filled = (level >= 100 ? 5 : level >= 75 ? 4 : level >= 50 ? 3 : level >= 25 ? 2 : level > 0 ? 1 : 0);

        if (filled > 0)
        {
            // Color: red if low, otherwise normal
            uint16_t fill_color =
                (filled == 1) ? THEME_COLOR_BATTERY_LOW : (selected ? THEME_COLOR_SELECTED : THEME_COLOR_UNSELECTED);
            canvas->fillRect(x + 1, y + 1, 2 * filled, 5, fill_color);
        }
    }
}

bool AppNodes::_render_node_list()
{
    if (!_data.update_list)
        return false;
    _data.update_list = false;

    auto* canvas = _data.hal->canvas();
    int panel_x = 0;
    int panel_width = canvas->width();
    canvas->fillRect(panel_x, 0, panel_width, canvas->height(), THEME_COLOR_BG);
    canvas->setFont(FONT_12);

    // Update node count (may have changed externally)
    if (_data.hal->nodedb())
    {
        _data.total_node_count = _data.hal->nodedb()->getNodeCount();
    }

    if (_data.total_node_count == 0)
    {
        _data.list_selected_node_id = 0;
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawCenterString("<no nodes found>",
                                 panel_x + panel_width / 2,
                                 LIST_HEADER_HEIGHT + (LIST_MAX_VISIBLE_ITEMS / 2) * (LIST_ITEM_HEIGHT + 1));
        return true;
    }

    // Load only visible nodes from storage
    auto* nodedb = _data.hal->nodedb();
    if (!nodedb)
        return true;

    // Resolve tracked node ID to current sorted index before rendering
    if (_data.list_selected_node_id != 0)
    {
        int resolved = nodedb->getSortedIndexForNode(_data.list_selected_node_id, _data.sort_order);
        if (resolved >= 0)
        {
            _data.selected_index = resolved;
        }
        // If not found, keep current index but clamp
        if (_data.selected_index >= (int)_data.total_node_count)
        {
            _data.selected_index = (int)_data.total_node_count - 1;
        }
        // Adjust scroll to keep selection visible
        if (_data.selected_index < _data.scroll_offset)
        {
            _data.scroll_offset = _data.selected_index;
        }
        else if (_data.selected_index >= _data.scroll_offset + LIST_MAX_VISIBLE_ITEMS)
        {
            _data.scroll_offset = _data.selected_index - LIST_MAX_VISIBLE_ITEMS + 1;
        }
    }

    std::vector<Mesh::NodeInfo> visible_nodes;
    size_t loaded = nodedb->getNodesInRange(_data.scroll_offset, LIST_MAX_VISIBLE_ITEMS, visible_nodes, _data.sort_order);
    uint32_t our_id = _data.hal->mesh() ? _data.hal->mesh()->getNodeId() : 0;

    auto& store = Mesh::MeshDataStore::getInstance();

    // Calculate column positions
    int short_x = LIST_ITEM_LEFT_PADDING;
    int role_x = short_x + COL_SHORT_NAME_WIDTH + 2;
    int key_x = role_x + COL_ROLE_WIDTH + 2;
    int name_x = key_x + COL_KEY_WIDTH + 2;
    int name_width = LIST_MAX_DISPLAY_CHARS * 6;
    int batt_x = name_x + name_width + 2;
    int pos_x = batt_x + COL_BATTERY_WIDTH + 1;
    int signal_x = pos_x + COL_POSITION_WIDTH + 1;
    int signal_text_x = signal_x + COL_SIGNAL_GAUGE_WIDTH + 1;
    int hops_x = signal_text_x + COL_SIGNAL_TEXT_WIDTH + 2;
    int last_seen_x = hops_x + COL_HOPS_WIDTH;
    int fav_x = last_seen_x + COL_LAST_SEEN_WIDTH + 2;

    // Draw node list
    int y_offset = LIST_HEADER_HEIGHT;
    int items_drawn = 0;

    for (size_t i = 0; i < loaded && items_drawn < LIST_MAX_VISIBLE_ITEMS; i++)
    {
        const auto& node = visible_nodes[i];
        size_t actual_index = _data.scroll_offset + i;
        bool is_selected = ((int)actual_index == _data.selected_index);

        // Track selected node by ID for stable selection across refreshes
        if (is_selected)
        {
            _data.list_selected_node_id = node.info.num;
        }

        // Selection highlight
        if (is_selected)
        {
            canvas->fillRect(panel_x + 2,
                             y_offset + 1,
                             panel_width - 2 - SCROLL_BAR_WIDTH - 1,
                             LIST_ITEM_HEIGHT,
                             THEME_COLOR_BG_SELECTED);
        }

        // 1. Node ID / Short name with colored background
        uint32_t node_color = _get_node_color(node.info.num);
        uint32_t node_text_color = _get_node_text_color(node.info.num);
        std::string short_name = Mesh::NodeDB::getLabel(node);
        canvas->fillRoundRect(short_x, y_offset + 1, COL_SHORT_NAME_WIDTH, LIST_ITEM_HEIGHT, 4, node_color);
        canvas->setTextColor(node_text_color, node_color);
        canvas->drawCenterString(short_name.c_str(), short_x + COL_SHORT_NAME_WIDTH / 2, y_offset + 1);

        // 2. Role icon
        {
            RoleInfo ri = _get_role_info(node.info.user.role);
            // uint32_t role_bg = is_selected ? THEME_COLOR_BG_SELECTED : ri.bg_color;
            // uint32_t role_fg = is_selected ? THEME_COLOR_SELECTED : ri.text_color;
            canvas->fillRect(role_x, y_offset + 2, COL_ROLE_WIDTH, LIST_ICON_HEIGHT, ri.bg_color);
            canvas->setFont(FONT_10);
            canvas->setTextColor(ri.text_color, ri.bg_color);
            canvas->drawCenterString(ri.label, role_x + COL_ROLE_WIDTH / 2, y_offset + 3);
            canvas->setFont(FONT_12);
        }

        // 3. Encryption key type icon placeholder
        // Types: S = Shared key, P = Public key encryption, X = Key mismatch
        {
            uint16_t* key_image_data = nullptr;
            if (node.info.has_user && node.info.user.public_key.size > 0)
            {
                key_image_data = (uint16_t*)image_data_key_pke;
            }
            else
            {
                key_image_data = (uint16_t*)image_data_key_shared;
            }
            // Key mismatch would be: key_str = "X"; key_color = TFT_RED;
            if (key_image_data == nullptr)
            {
                canvas->drawRect(key_x,
                                 y_offset + 2,
                                 COL_KEY_WIDTH,
                                 LIST_ICON_HEIGHT,
                                 is_selected ? THEME_COLOR_SELECTED : THEME_COLOR_SIGNAL_BAD);
                canvas->setFont(FONT_10);
                canvas->setTextColor(is_selected ? THEME_COLOR_SELECTED : THEME_COLOR_SIGNAL_BAD,
                                     is_selected ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG);
                canvas->drawCenterString("?", key_x + COL_KEY_WIDTH / 2, y_offset + 2);
                canvas->setFont(FONT_12);
            }
            else
            {
                canvas->pushImage(key_x, y_offset + 1, COL_KEY_WIDTH, LIST_ICON_HEIGHT, key_image_data, TFT_WHITE);
            }
        }

        // 4. Long name (scrolling handled separately for selected item)
        std::string display_name = node.info.user.long_name;
        if (display_name.empty())
        {
            display_name = _format_node_id(node.info.num);
        }
        if (is_selected)
        {
            // Cache display name for scrolling text renderer
            _data.selected_display_name = display_name;
        }
        if (canvas->textWidth(display_name.c_str()) > name_width)
        {
            // UTF-8 safe truncation
            size_t trunc_len =
                utf8_truncate_len(display_name.c_str(), is_selected ? LIST_MAX_DISPLAY_CHARS : LIST_MAX_DISPLAY_CHARS - 1);
            display_name = display_name.substr(0, trunc_len);
            if (!is_selected)
                display_name += ">";
        }
        canvas->setTextColor(is_selected ? THEME_COLOR_SELECTED : THEME_COLOR_UNSELECTED,
                             is_selected ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG);
        canvas->drawString(display_name.c_str(), name_x, y_offset + 1);

        // 5. Battery level icon
        if (node.info.has_device_metrics && node.info.device_metrics.has_battery_level)
        {
            draw_battery_icon(canvas, batt_x, y_offset + 4, node.info.device_metrics.battery_level, is_selected);
        }

        // 5b. Position icon (shown when node has GPS/position data)
        if (node.info.has_position && (node.info.position.has_latitude_i || node.info.position.has_longitude_i))
        {
            uint16_t* pos_image_data = nullptr;
            meshtastic_Position_LocSource location_source = node.info.position.location_source;
            if (location_source == meshtastic_Position_LocSource_LOC_INTERNAL)
            {
                pos_image_data = (uint16_t*)image_data_pos_internal;
            }
            else if (location_source == meshtastic_Position_LocSource_LOC_EXTERNAL)
            {
                pos_image_data = (uint16_t*)image_data_pos_external;
            }
            else if (location_source == meshtastic_Position_LocSource_LOC_MANUAL)
            {
                pos_image_data = (uint16_t*)image_data_pos_manual;
            }

            if (pos_image_data != nullptr)
            {
                canvas->pushImage(pos_x, y_offset + 2, COL_POSITION_WIDTH, LIST_ICON_HEIGHT, pos_image_data, TFT_WHITE);
            }
            else
            {
                canvas->drawRect(pos_x,
                                 y_offset + 2,
                                 COL_POSITION_WIDTH,
                                 LIST_ICON_HEIGHT,
                                 is_selected ? THEME_COLOR_SELECTED : THEME_COLOR_SIGNAL_BAD);
                canvas->setFont(FONT_10);
                canvas->setTextColor(is_selected ? THEME_COLOR_SELECTED : THEME_COLOR_SIGNAL_BAD,
                                     is_selected ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG);
                canvas->drawCenterString("?", pos_x + COL_POSITION_WIDTH / 2, y_offset + 3);
                canvas->setFont(FONT_12);
            }
        }

        // 6. Signal level icon placeholder (vertical bar gauge)
        int signal_height = LIST_ITEM_HEIGHT - 2;
        int signal_level = _get_signal_level(node.info.snr, node.last_rssi);
        int filled_height = (signal_level * signal_height) / 100;
        if (filled_height < 1 && signal_level > 0)
            filled_height = 1;

        // Background
        canvas->fillRect(signal_x,
                         y_offset + 2,
                         4,
                         signal_height,
                         is_selected ? THEME_COLOR_BG_SELECTED_DARK : THEME_COLOR_BG_DARK);

        // Filled portion
        if (filled_height > 0)
        {
            uint32_t signal_color;
            if (signal_level > 60)
                signal_color = THEME_COLOR_SIGNAL_GOOD;
            else if (signal_level > 40)
                signal_color = THEME_COLOR_SIGNAL_FAIR;
            else if (signal_level > 20)
                signal_color = THEME_COLOR_SIGNAL_BAD;
            else
                signal_color = THEME_COLOR_SIGNAL_NONE;

            canvas->fillRect(signal_x,
                             y_offset + 2 + signal_height - filled_height,
                             4,
                             filled_height,
                             is_selected ? THEME_COLOR_SELECTED : signal_color);
        }

        // Draw SNR/RSSI text widget if hops away is 0
        // 4b. SNR/RSSI text widget (compact 2-line display) with color-coded values
        canvas->setFont(FONT_6);
        int signal_text_center = signal_text_x + COL_SIGNAL_TEXT_WIDTH / 2;
        uint32_t bg_color = is_selected ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG;
        uint32_t text_color = is_selected                ? THEME_COLOR_SELECTED
                              : node.info.hops_away == 0 ? _get_snr_color(node.info.snr)
                                                         : THEME_COLOR_SIGNAL_TEXT;
        // Try to resolve relay node short name from relay_node byte
        bool have_relay_node = false;
        uint32_t relay_node_id = 0;
        if (node.relay_node != 0 && node.info.hops_away > 0)
        {
            relay_node_id = nodedb->findNodeByRelayByte(node.relay_node);
            if (relay_node_id != 0)
            {
                Mesh::NodeInfo relay_info;
                // using nodedb (not mesh wrapper) because no our node in the list
                if (nodedb->getNode(relay_node_id, relay_info))
                {
                    have_relay_node = true;
                    short_name = Mesh::NodeDB::getLabel(relay_info);
                }
            }
        }

        if (have_relay_node)
        {
            // Relay node name - color based on relay node ID
            uint32_t relay_node_color = _get_node_color(relay_node_id);
            uint32_t relay_node_text_color = _get_node_text_color(relay_node_id);
            canvas->fillRoundRect(signal_text_x + 1, y_offset + 3, COL_SHORT_RELAY_NAME_WIDTH, 9, 3, relay_node_color);
            canvas->setTextColor(relay_node_text_color, relay_node_color);
            canvas->drawCenterString(short_name.c_str(), signal_text_x + 3 + COL_SHORT_RELAY_NAME_WIDTH / 2, y_offset + 5);
            // clear the background after text
            canvas->fillRect(signal_text_x + 1 + COL_SHORT_RELAY_NAME_WIDTH, y_offset + 4, 9, 9, bg_color);
        }
        else
        {
            // Line 1: SNR or relay name (top) - color based on SNR quality
            char snr_str[8];
            snprintf(snr_str, sizeof(snr_str), "%.1f", node.info.snr);
            canvas->setTextColor(text_color, bg_color);
            canvas->drawCenterString(snr_str, signal_text_center, y_offset + 2);

            // Line 2: RSSI (bottom) - color based on RSSI quality
            char rssi_str[8];
            snprintf(rssi_str, sizeof(rssi_str), "%d", (int)node.last_rssi);
            text_color = is_selected                ? THEME_COLOR_SELECTED
                         : node.info.hops_away == 0 ? _get_rssi_color(node.last_rssi)
                                                    : THEME_COLOR_SIGNAL_TEXT;
            canvas->setTextColor(text_color, bg_color);
            canvas->drawCenterString(rssi_str, signal_text_center, y_offset + 9);
        }
        // Restore font
        canvas->setFont(FONT_12);
        // 5. Hops icon placeholder (number in box)
        canvas->setTextColor(is_selected                ? THEME_COLOR_SELECTED
                             : node.info.hops_away == 0 ? THEME_COLOR_SIGNAL_TEXT
                                                        : THEME_COLOR_UNSELECTED,
                             is_selected ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG);
        char hops_str[4];
        snprintf(hops_str, sizeof(hops_str), "%d", node.info.hops_away);
        canvas->drawString(hops_str, hops_x + 2, y_offset + 1);
        canvas->drawRect(hops_x,
                         y_offset + 2,
                         COL_HOPS_WIDTH,
                         LIST_ITEM_HEIGHT - 2,
                         is_selected                ? THEME_COLOR_SELECTED
                         : node.info.hops_away == 0 ? THEME_COLOR_SIGNAL_TEXT
                                                    : THEME_COLOR_UNSELECTED);

        // 6. Last seen
        canvas->setTextColor(is_selected ? THEME_COLOR_SELECTED : THEME_COLOR_UNSELECTED,
                             is_selected ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG);
        std::string last_seen = _format_last_seen(node.info.last_heard);
        canvas->drawCenterString(last_seen.c_str(), last_seen_x + COL_LAST_SEEN_WIDTH / 2, y_offset + 1);

        // 7. Favorite indicator
        if (node.info.is_favorite)
        {
            // todo: use a favorite icon
            canvas->setTextColor(is_selected ? THEME_COLOR_SELECTED : THEME_COLOR_FAVORITE,
                                 is_selected ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG);
            canvas->drawString("*", fav_x, y_offset + 1);
        }

        // 8. Unread messages indicator
        uint32_t unread = store.getUnreadDMCount(node.info.num);
        if (unread > 0)
        {
            char ticker_str[8];
            snprintf(ticker_str, sizeof(ticker_str), "+%d", unread > 99 ? 99 : (int)unread);
            int ticker_width = strlen(ticker_str) * 6 + 6;
            canvas->fillRoundRect(panel_x + panel_width - ticker_width - 1 - SCROLL_BAR_WIDTH - 1,
                                  y_offset + 1,
                                  ticker_width,
                                  LIST_ITEM_HEIGHT,
                                  3,
                                  TFT_RED);
            canvas->setTextColor(is_selected ? THEME_COLOR_SELECTED : THEME_COLOR_UNSELECTED, TFT_RED);
            canvas->drawRightString(ticker_str, panel_x + panel_width - 1 - SCROLL_BAR_WIDTH - 6, y_offset + 1);
        }
        // if node has messages, draw a message icon
        else if (store.getDMMessageCount(node.info.num) > 0)
        {
            canvas->pushImage(panel_x + panel_width - 1 - SCROLL_BAR_WIDTH - LIST_ITEM_HEIGHT,
                              y_offset + 2,
                              LIST_ICON_HEIGHT,
                              LIST_ICON_HEIGHT,
                              image_data_chat,
                              TFT_WHITE);
        }

        y_offset += LIST_ITEM_HEIGHT + 1;
        items_drawn++;
    }

    // Draw scrollbar
    _render_scrollbar(panel_x, panel_width);
    return true;
}

bool AppNodes::_render_scrolling_name(bool force)
{
    if (_data.total_node_count == 0 || _data.selected_display_name.empty())
    {
        return false;
    }

    // Calculate column positions
    int short_x = LIST_ITEM_LEFT_PADDING;
    int role_x = short_x + COL_SHORT_NAME_WIDTH + 2;
    int key_x = role_x + COL_ROLE_WIDTH + 2;
    int name_x = key_x + COL_KEY_WIDTH + 2;

    // Calculate y position of selected item
    int relative_pos = _data.selected_index - _data.scroll_offset;
    int y_offset = LIST_HEADER_HEIGHT + (relative_pos * (LIST_ITEM_HEIGHT + 1));

    // Render the scrolling text using cached display name
    return scroll_text_render(&_data.name_scroll_ctx,
                              _data.selected_display_name.c_str(),
                              name_x,
                              y_offset + 1,
                              THEME_COLOR_SELECTED,
                              THEME_COLOR_BG_SELECTED,
                              force);
}

void AppNodes::_render_scrollbar(int panel_x, int panel_width)
{
    UTILS::UI::draw_scrollbar(_data.hal->canvas(),
                              panel_x + panel_width - SCROLL_BAR_WIDTH - 1,
                              LIST_HEADER_HEIGHT,
                              SCROLL_BAR_WIDTH,
                              (LIST_ITEM_HEIGHT + 1) * LIST_MAX_VISIBLE_ITEMS,
                              (int)_data.total_node_count,
                              LIST_MAX_VISIBLE_ITEMS,
                              _data.scroll_offset,
                              SCROLLBAR_MIN_HEIGHT);
}

uint32_t AppNodes::_get_node_color(uint32_t node_id) { return UTILS::UI::node_color(node_id); }

uint32_t AppNodes::_get_node_text_color(uint32_t node_id) { return UTILS::UI::node_text_color(node_id); }

// SNR limit for typical LoRa preset (LongFast)
// Different presets have different limits, but -7.5 is common
static constexpr float SNR_LIMIT = -7.5f;

int AppNodes::_get_signal_level(float snr, int16_t rssi)
{
    // Calculate signal level 0-100 based on both SNR and RSSI
    // Based on Meshtastic LoRaSignalStrengthIndicator.swift
    //
    // RSSI thresholds: -115 (good), -120 (fair), -126 (bad)
    // SNR thresholds relative to SNR_LIMIT: 0 (good), -5.5 (fair), -7.5 (bad)

    // Calculate RSSI component (0-100)
    // Map: -126 or below = 0%, -115 or above = 100%
    int rssi_level = 0;
    if (rssi >= -115)
        rssi_level = 100;
    else if (rssi <= -126)
        rssi_level = 0;
    else
        rssi_level = (rssi + 126) * 100 / 11; // Linear interpolation

    // Calculate SNR component (0-100)
    // Map: (SNR_LIMIT - 7.5) or below = 0%, SNR_LIMIT or above = 100%
    float snr_good = SNR_LIMIT;
    float snr_bad = SNR_LIMIT - 7.5f;
    int snr_level = 0;
    if (snr >= snr_good)
        snr_level = 100;
    else if (snr <= snr_bad)
        snr_level = 0;
    else
        snr_level = (int)((snr - snr_bad) * 100.0f / 7.5f); // Linear interpolation

    // If RSSI is 0 (not available), use only SNR
    if (rssi == 0)
        return snr_level;

    // Combine both metrics - use the worse of the two as the limiting factor
    // but weight them to get a smoother result
    return std::min(rssi_level, snr_level);
}

uint32_t AppNodes::_get_snr_color(float snr)
{
    // Color based on SNR relative to SNR_LIMIT
    // Based on Meshtastic LoRaSignalStrengthIndicator.swift
    if (snr > SNR_LIMIT)
        return THEME_COLOR_SIGNAL_GOOD; // Good
    else if (snr > SNR_LIMIT - 5.5f)
        return THEME_COLOR_SIGNAL_FAIR; // Fair
    else if (snr >= SNR_LIMIT - 7.5f)
        return THEME_COLOR_SIGNAL_BAD; // Bad
    else
        return THEME_COLOR_SIGNAL_NONE; // None
}

uint32_t AppNodes::_get_rssi_color(int16_t rssi)
{
    // Color based on RSSI value
    // Based on Meshtastic LoRaSignalStrengthIndicator.swift
    if (rssi > -115)
        return THEME_COLOR_SIGNAL_GOOD; // Good
    else if (rssi > -120)
        return THEME_COLOR_SIGNAL_FAIR; // Fair
    else if (rssi > -126)
        return THEME_COLOR_SIGNAL_BAD; // Bad
    else
        return THEME_COLOR_SIGNAL_NONE; // None
}

void AppNodes::_render_signal_bars(int x, int y, int16_t rssi)
{
    auto* canvas = _data.hal->canvas();
    int bars = 0;

    if (rssi > -70)
        bars = 4;
    else if (rssi > -85)
        bars = 3;
    else if (rssi > -100)
        bars = 2;
    else if (rssi > -120)
        bars = 1;

    for (int i = 0; i < 4; i++)
    {
        int bar_height = 4 + i * 2;
        int bar_x = x + i * (SIGNAL_BAR_WIDTH + SIGNAL_BAR_GAP);
        int bar_y = y + 12 - bar_height;

        if (i < bars)
        {
            canvas->fillRect(bar_x, bar_y, SIGNAL_BAR_WIDTH, bar_height, TFT_GREEN);
        }
        else
        {
            canvas->drawRect(bar_x, bar_y, SIGNAL_BAR_WIDTH, bar_height, TFT_DARKGREY);
        }
    }
}

static const char* _hw_model_name(meshtastic_HardwareModel model)
{
    switch (model)
    {
    case meshtastic_HardwareModel_UNSET:
        return "Unset";
    case meshtastic_HardwareModel_TLORA_V2:
        return "T-LoRa V2";
    case meshtastic_HardwareModel_TLORA_V1:
        return "T-LoRa V1";
    case meshtastic_HardwareModel_TLORA_V2_1_1P6:
        return "T-LoRa V2.1";
    case meshtastic_HardwareModel_TBEAM:
        return "T-Beam";
    case meshtastic_HardwareModel_HELTEC_V2_0:
        return "Heltec V2";
    case meshtastic_HardwareModel_TBEAM_V0P7:
        return "T-Beam 0.7";
    case meshtastic_HardwareModel_T_ECHO:
        return "T-Echo";
    case meshtastic_HardwareModel_TLORA_V1_1P3:
        return "T-LoRa V1.3";
    case meshtastic_HardwareModel_RAK4631:
        return "RAK4631";
    case meshtastic_HardwareModel_HELTEC_V2_1:
        return "Heltec V2.1";
    case meshtastic_HardwareModel_HELTEC_V1:
        return "Heltec V1";
    case meshtastic_HardwareModel_LILYGO_TBEAM_S3_CORE:
        return "T-Beam S3";
    case meshtastic_HardwareModel_RAK11200:
        return "RAK11200";
    case meshtastic_HardwareModel_NANO_G1:
        return "Nano G1";
    case meshtastic_HardwareModel_TLORA_V2_1_1P8:
        return "T-LoRa V2.1.8";
    case meshtastic_HardwareModel_TLORA_T3_S3:
        return "T3-S3";
    case meshtastic_HardwareModel_NANO_G1_EXPLORER:
        return "Nano G1 Exp";
    case meshtastic_HardwareModel_NANO_G2_ULTRA:
        return "Nano G2 Ultra";
    case meshtastic_HardwareModel_LORA_TYPE:
        return "LoRa Type";
    case meshtastic_HardwareModel_WIPHONE:
        return "WiPhone";
    case meshtastic_HardwareModel_WIO_WM1110:
        return "Wio WM1110";
    case meshtastic_HardwareModel_RAK2560:
        return "RAK2560";
    case meshtastic_HardwareModel_HELTEC_HRU_3601:
        return "Heltec HRU3601";
    case meshtastic_HardwareModel_HELTEC_WIRELESS_BRIDGE:
        return "Heltec Bridge";
    case meshtastic_HardwareModel_STATION_G1:
        return "Station G1";
    case meshtastic_HardwareModel_RAK11310:
        return "RAK11310";
    case meshtastic_HardwareModel_SENSELORA_RP2040:
        return "SenseLora 2040";
    case meshtastic_HardwareModel_SENSELORA_S3:
        return "SenseLora S3";
    case meshtastic_HardwareModel_CANARYONE:
        return "CanaryOne";
    case meshtastic_HardwareModel_RP2040_LORA:
        return "RP2040 LoRa";
    case meshtastic_HardwareModel_STATION_G2:
        return "Station G2";
    case meshtastic_HardwareModel_LORA_RELAY_V1:
        return "LoRa Relay V1";
    case meshtastic_HardwareModel_T_ECHO_PLUS:
        return "T-Echo+";
    case meshtastic_HardwareModel_PPR:
        return "PPR";
    case meshtastic_HardwareModel_GENIEBLOCKS:
        return "GenieBlocks";
    case meshtastic_HardwareModel_NRF52_UNKNOWN:
        return "nRF52";
    case meshtastic_HardwareModel_PORTDUINO:
        return "Portduino";
    case meshtastic_HardwareModel_ANDROID_SIM:
        return "Android Sim";
    case meshtastic_HardwareModel_DIY_V1:
        return "DIY V1";
    case meshtastic_HardwareModel_NRF52840_PCA10059:
        return "nRF52840 DK";
    case meshtastic_HardwareModel_DR_DEV:
        return "DR-Dev";
    case meshtastic_HardwareModel_M5STACK:
        return "M5Stack";
    case meshtastic_HardwareModel_HELTEC_V3:
        return "Heltec V3";
    case meshtastic_HardwareModel_HELTEC_WSL_V3:
        return "Heltec WSL V3";
    case meshtastic_HardwareModel_BETAFPV_2400_TX:
        return "BetaFPV 2400";
    case meshtastic_HardwareModel_BETAFPV_900_NANO_TX:
        return "BetaFPV 900";
    case meshtastic_HardwareModel_RPI_PICO:
        return "RPi Pico";
    case meshtastic_HardwareModel_HELTEC_WIRELESS_TRACKER:
        return "Heltec Tracker";
    case meshtastic_HardwareModel_HELTEC_WIRELESS_PAPER:
        return "Heltec Paper";
    case meshtastic_HardwareModel_T_DECK:
        return "T-Deck";
    case meshtastic_HardwareModel_T_WATCH_S3:
        return "T-Watch S3";
    case meshtastic_HardwareModel_PICOMPUTER_S3:
        return "PiComputer S3";
    case meshtastic_HardwareModel_HELTEC_HT62:
        return "Heltec HT62";
    case meshtastic_HardwareModel_EBYTE_ESP32_S3:
        return "Ebyte S3";
    case meshtastic_HardwareModel_ESP32_S3_PICO:
        return "ESP32-S3 Pico";
    case meshtastic_HardwareModel_CHATTER_2:
        return "Chatter 2";
    case meshtastic_HardwareModel_HELTEC_WIRELESS_PAPER_V1_0:
        return "Heltec Paper1";
    case meshtastic_HardwareModel_HELTEC_WIRELESS_TRACKER_V1_0:
        return "Heltec Trk1";
    case meshtastic_HardwareModel_UNPHONE:
        return "unPhone";
    case meshtastic_HardwareModel_TD_LORAC:
        return "TD-LoRaC";
    case meshtastic_HardwareModel_CDEBYTE_EORA_S3:
        return "CDEBYTE S3";
    case meshtastic_HardwareModel_TWC_MESH_V4:
        return "TWC Mesh V4";
    case meshtastic_HardwareModel_NRF52_PROMICRO_DIY:
        return "nRF52 ProMicro";
    case meshtastic_HardwareModel_RADIOMASTER_900_BANDIT_NANO:
        return "RM900 Nano";
    case meshtastic_HardwareModel_HELTEC_CAPSULE_SENSOR_V3:
        return "Heltec Capsule";
    case meshtastic_HardwareModel_HELTEC_VISION_MASTER_T190:
        return "Heltec VM T190";
    case meshtastic_HardwareModel_HELTEC_VISION_MASTER_E213:
        return "Heltec VM E213";
    case meshtastic_HardwareModel_HELTEC_VISION_MASTER_E290:
        return "Heltec VM E290";
    case meshtastic_HardwareModel_HELTEC_MESH_NODE_T114:
        return "Heltec T114";
    case meshtastic_HardwareModel_SENSECAP_INDICATOR:
        return "SenseCap Ind";
    case meshtastic_HardwareModel_TRACKER_T1000_E:
        return "Tracker T1000";
    case meshtastic_HardwareModel_RAK3172:
        return "RAK3172";
    case meshtastic_HardwareModel_WIO_E5:
        return "Wio-E5";
    case meshtastic_HardwareModel_RADIOMASTER_900_BANDIT:
        return "RM900 Bandit";
    case meshtastic_HardwareModel_ME25LS01_4Y10TD:
        return "ME25LS01";
    case meshtastic_HardwareModel_RP2040_FEATHER_RFM95:
        return "RP2040 RFM95";
    case meshtastic_HardwareModel_M5STACK_COREBASIC:
        return "M5 CoreBasic";
    case meshtastic_HardwareModel_M5STACK_CORE2:
        return "M5 Core2";
    case meshtastic_HardwareModel_RPI_PICO2:
        return "RPi Pico2";
    case meshtastic_HardwareModel_M5STACK_CORES3:
        return "M5 CoreS3";
    case meshtastic_HardwareModel_SEEED_XIAO_S3:
        return "Xiao S3";
    case meshtastic_HardwareModel_MS24SF1:
        return "MS24SF1";
    case meshtastic_HardwareModel_TLORA_C6:
        return "T-LoRa C6";
    case meshtastic_HardwareModel_WISMESH_TAP:
        return "WisMesh Tap";
    case meshtastic_HardwareModel_ROUTASTIC:
        return "Routastic";
    case meshtastic_HardwareModel_MESH_TAB:
        return "Mesh Tab";
    case meshtastic_HardwareModel_MESHLINK:
        return "MeshLink";
    case meshtastic_HardwareModel_XIAO_NRF52_KIT:
        return "Xiao nRF52";
    case meshtastic_HardwareModel_THINKNODE_M1:
        return "ThinkNode M1";
    case meshtastic_HardwareModel_THINKNODE_M2:
        return "ThinkNode M2";
    case meshtastic_HardwareModel_T_ETH_ELITE:
        return "T-ETH Elite";
    case meshtastic_HardwareModel_HELTEC_SENSOR_HUB:
        return "Heltec SenHub";
    case meshtastic_HardwareModel_MUZI_BASE:
        return "Muzi Base";
    case meshtastic_HardwareModel_HELTEC_MESH_POCKET:
        return "Heltec Pocket";
    case meshtastic_HardwareModel_SEEED_SOLAR_NODE:
        return "Seeed Solar";
    case meshtastic_HardwareModel_NOMADSTAR_METEOR_PRO:
        return "NomadStar Pro";
    case meshtastic_HardwareModel_CROWPANEL:
        return "CrowPanel";
    case meshtastic_HardwareModel_LINK_32:
        return "Link 32";
    case meshtastic_HardwareModel_SEEED_WIO_TRACKER_L1:
        return "Wio Tracker L1";
    case meshtastic_HardwareModel_SEEED_WIO_TRACKER_L1_EINK:
        return "Wio Trk eInk";
    case meshtastic_HardwareModel_MUZI_R1_NEO:
        return "Muzi R1 Neo";
    case meshtastic_HardwareModel_T_DECK_PRO:
        return "T-Deck Pro";
    case meshtastic_HardwareModel_T_LORA_PAGER:
        return "T-LoRa Pager";
    case meshtastic_HardwareModel_M5STACK_RESERVED:
        return "M5Stack Rsv";
    case meshtastic_HardwareModel_WISMESH_TAG:
        return "WisMesh Tag";
    case meshtastic_HardwareModel_RAK3312:
        return "RAK3312";
    case meshtastic_HardwareModel_THINKNODE_M5:
        return "ThinkNode M5";
    case meshtastic_HardwareModel_HELTEC_MESH_SOLAR:
        return "Heltec Solar";
    case meshtastic_HardwareModel_T_ECHO_LITE:
        return "T-Echo Lite";
    case meshtastic_HardwareModel_HELTEC_V4:
        return "Heltec V4";
    case meshtastic_HardwareModel_M5STACK_C6L:
        return "M5Stack C6L";
    case meshtastic_HardwareModel_M5STACK_CARDPUTER_ADV:
        return "CardPuter+";
    case meshtastic_HardwareModel_HELTEC_WIRELESS_TRACKER_V2:
        return "Heltec Trk V2";
    case meshtastic_HardwareModel_T_WATCH_ULTRA:
        return "T-Watch Ultra";
    case meshtastic_HardwareModel_THINKNODE_M3:
        return "ThinkNode M3";
    case meshtastic_HardwareModel_WISMESH_TAP_V2:
        return "WisMesh TapV2";
    case meshtastic_HardwareModel_RAK3401:
        return "RAK3401";
    case meshtastic_HardwareModel_RAK6421:
        return "RAK6421";
    case meshtastic_HardwareModel_THINKNODE_M4:
        return "ThinkNode M4";
    case meshtastic_HardwareModel_THINKNODE_M6:
        return "ThinkNode M6";
    case meshtastic_HardwareModel_MESHSTICK_1262:
        return "MeshStick 1262";
    case meshtastic_HardwareModel_TBEAM_1_WATT:
        return "T-Beam 1W";
    case meshtastic_HardwareModel_T5_S3_EPAPER_PRO:
        return "T5-S3 ePaper";
    case meshtastic_HardwareModel_PRIVATE_HW:
        return "Custom";
    default:
    {
        static char buf[8];
        snprintf(buf, sizeof(buf), "HW#%d", (int)model);
        return buf;
    }
    }
}

bool AppNodes::_render_node_detail()
{
    if (!_data.update_list)
        return false;
    _data.update_list = false;

    auto* canvas = _data.hal->canvas();
    canvas->fillScreen(THEME_COLOR_BG);
    canvas->setFont(FONT_12);

    if (!_data.selected_node_valid)
        return true;

    const auto& node = _data.selected_node;
    const auto& user = node.info.user;
    const auto& pos = node.info.position;
    const auto& metrics = node.info.device_metrics;
    const int rh = 13; // row height
    const int label_w = 38;
    const int val_x = label_w + 4;

    // Header: back arrow + color badge + long name
    canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    canvas->drawString("<", 2, 0);

    uint32_t badge_bg = _get_node_color(node.info.num);
    uint32_t badge_fg = _get_node_text_color(node.info.num);
    int badge_w = COL_SHORT_NAME_WIDTH;
    canvas->fillRoundRect(14, 0, badge_w, rh, 4, badge_bg);
    canvas->setTextColor(badge_fg, badge_bg);
    canvas->drawCenterString(user.short_name, 14 + badge_w / 2, 0);

    canvas->setTextColor(TFT_WHITE, THEME_COLOR_BG);
    canvas->drawString(user.long_name, 14 + badge_w + 4, 0);
    canvas->drawFastHLine(0, 13, canvas->width(), THEME_COLOR_BG_SELECTED);

    int y = 15;

    // Row: ID
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawString("ID", 4, y);
    canvas->setTextColor(TFT_WHITE, THEME_COLOR_BG);
    canvas->drawString(_format_node_id(node.info.num).c_str(), val_x, y);
    // Role on the right
    canvas->setTextColor(TFT_CYAN, THEME_COLOR_BG);
    canvas->drawRightString(Mesh::NodeDB::getRoleName(user.role), canvas->width() - 4, y);
    y += rh;

    // Row: HW model + favorite
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawString("HW", 4, y);
    canvas->setTextColor(TFT_WHITE, THEME_COLOR_BG);
    canvas->drawString(_hw_model_name(user.hw_model), val_x, y);
    if (node.info.is_favorite)
    {
        canvas->setTextColor(TFT_YELLOW, THEME_COLOR_BG);
        canvas->drawRightString("\u2605", canvas->width() - 4, y);
    }
    y += rh;

    // Row: Signal (RSSI + SNR + hops)
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawString("Sig", 4, y);
    if (node.last_rssi != 0 || node.info.snr != 0)
    {
        char sig[40];
        snprintf(sig, sizeof(sig), "%d dBm / %.1f dB", node.last_rssi, node.info.snr);
        canvas->setTextColor(_get_rssi_color(node.last_rssi), THEME_COLOR_BG);
        canvas->drawString(sig, val_x, y);
    }
    else
    {
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawString("---", val_x, y);
    }
    // Hops on the right (number in rect, matching node list style)
    if (node.info.has_hops_away)
    {
        uint32_t hops_color = node.info.hops_away == 0 ? THEME_COLOR_SIGNAL_TEXT : THEME_COLOR_UNSELECTED;
        int hops_x = canvas->width() - COL_HOPS_WIDTH - 4;
        std::string hops_str = std::format("{}", node.info.hops_away);
        canvas->setTextColor(hops_color, THEME_COLOR_BG);
        canvas->drawString(hops_str.c_str(), hops_x + 2, y);
        canvas->drawRect(hops_x, y, COL_HOPS_WIDTH, rh, hops_color);
    }
    y += rh;

    // Row: Last seen + via MQTT
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawString("Seen", 4, y);
    std::string seen = _format_last_seen(node.info.last_heard);
    canvas->setTextColor(seen == "now" ? THEME_COLOR_SIGNAL_GOOD : TFT_WHITE, THEME_COLOR_BG);
    canvas->drawString(seen.empty() ? "never" : seen.c_str(), val_x, y);
    if (node.info.via_mqtt)
    {
        canvas->setTextColor(TFT_MAGENTA, THEME_COLOR_BG);
        canvas->drawRightString("MQTT", canvas->width() - 4, y);
    }
    y += rh;

    // Row: Battery + Voltage + Channel util
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawString("Bat", 4, y);
    if (node.info.has_device_metrics && metrics.has_battery_level)
    {
        char bat[32];
        if (metrics.battery_level > 100)
            snprintf(bat, sizeof(bat), "PWR");
        else if (metrics.has_voltage)
            snprintf(bat, sizeof(bat), "%lu%% %.1fV", (unsigned long)metrics.battery_level, metrics.voltage);
        else
            snprintf(bat, sizeof(bat), "%lu%%", (unsigned long)metrics.battery_level);
        uint32_t bat_color = metrics.battery_level > 100  ? TFT_CYAN
                             : metrics.battery_level > 20 ? THEME_COLOR_SIGNAL_GOOD
                                                          : TFT_RED;
        canvas->setTextColor(bat_color, THEME_COLOR_BG);
        canvas->drawString(bat, val_x, y);
    }
    else
    {
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawString("---", val_x, y);
    }
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawString("Ch.util", canvas->width() / 2, y);
    if (node.info.has_device_metrics && metrics.has_channel_utilization)
    {
        std::string ch_str = std::format("{:.2f}%", metrics.channel_utilization);
        canvas->setTextColor(TFT_WHITE, THEME_COLOR_BG);
        canvas->drawRightString(ch_str.c_str(), canvas->width() - 4, y);
    }
    else
    {
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawRightString("---", canvas->width() - 4, y);
    }
    y += rh;

    // Row: Position
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawString("Pos", 4, y);
    if (node.info.has_position && pos.has_latitude_i && pos.has_longitude_i && (pos.latitude_i != 0 || pos.longitude_i != 0))
    {
        std::string pos_str;
        if (pos.has_altitude && pos.altitude != 0)
        {
            pos_str = std::format("{:.7f}, {:.7f} \u2191 {}m", pos.latitude_i * 1e-7, pos.longitude_i * 1e-7, pos.altitude);
        }
        else
        {
            pos_str = std::format("{:.7f}, {:.7f}", pos.latitude_i * 1e-7, pos.longitude_i * 1e-7);
        }

        canvas->setTextColor(TFT_WHITE, THEME_COLOR_BG);
        canvas->drawString(pos_str.c_str(), val_x, y);
    }
    else
    {
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawString("---", val_x, y);
    }
    y += rh;

    // Row: Metrics (air util TX + uptime)
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawString("Air", 4, y);
    if (node.info.has_device_metrics && metrics.has_air_util_tx)
    {
        std::string air_str = std::format("{:.2f}% TX", metrics.air_util_tx);
        canvas->setTextColor(TFT_WHITE, THEME_COLOR_BG);
        canvas->drawString(air_str.c_str(), val_x, y);
    }
    else
    {
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawString("---", val_x, y);
    }
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawString("Uptime", canvas->width() / 2, y);
    if (node.info.has_device_metrics && metrics.has_uptime_seconds)
    {
        uint32_t up = metrics.uptime_seconds;
        char uptime[16];
        if (up < 3600)
            snprintf(uptime, sizeof(uptime), "%lum", (unsigned long)(up / 60));
        else if (up < 86400)
            snprintf(uptime, sizeof(uptime), "%luh%lum", (unsigned long)(up / 3600), (unsigned long)((up % 3600) / 60));
        else
            snprintf(uptime, sizeof(uptime), "%lud%luh", (unsigned long)(up / 86400), (unsigned long)((up % 86400) / 3600));
        canvas->setTextColor(TFT_WHITE, THEME_COLOR_BG);
        canvas->drawRightString(uptime, canvas->width() - 4, y);
    }
    else
    {
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawRightString("---", canvas->width() - 4, y);
    }

    return true;
}

bool AppNodes::_render_node_detail_hint()
{
    return hl_text_render(&_data.hint_hl_ctx,
                          HINT_DETAIL,
                          0,
                          _data.hal->canvas()->height() - 9,
                          TFT_DARKGREY,
                          TFT_WHITE,
                          THEME_COLOR_BG);
}

bool AppNodes::_render_dm_view()
{
    if (!_data.update_list)
        return false;
    _data.update_list = false;

    auto* canvas = _data.hal->canvas();
    canvas->fillScreen(THEME_COLOR_BG);

    if (!_data.selected_node_valid)
        return true;

    const auto& node = _data.selected_node;
    canvas->setFont(FONT_12);

    // Header: short_name + "DM" label
    canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    canvas->drawString("<", 2, 0);
    canvas->setTextColor(TFT_WHITE, THEME_COLOR_BG);
    canvas->drawString(node.info.user.long_name, 14, 0);

    // Message count on the right
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->pushImage(canvas->width() - 1 - 12, 0, 12, 12, image_data_chat, TFT_WHITE);
    canvas->drawRightString(std::format("{}", _data.dm_msg_count).c_str(), canvas->width() - 2 - 12, 0);

    canvas->drawFastHLine(0, DM_HEADER_HEIGHT - 1, canvas->width() - 1, THEME_COLOR_BG_SELECTED);

    // Messages area
    const int messages_area_top = DM_HEADER_HEIGHT;
    // const int messages_area_bottom = canvas->height() - DM_HEADER_HEIGHT - 1;
    const int messages_area_height = canvas->height() - DM_HEADER_HEIGHT - DM_FOOTER_HEIGHT;
    const int max_visible = messages_area_height / (DM_ITEM_HEIGHT + 1);

    // Layout: left column = sender short_name, right column = wrapped text
    const int name_col_width = 4 * 6 + 6;
    const int text_start_x = name_col_width + 2;

    if (_data.dm_msg_count == 0 || _data.dm_line_counts.empty())
    {
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawCenterString("<no messages yet>", canvas->width() / 2, canvas->height() / 2);
    }
    else
    {
        // Determine which messages overlap the visible line window [dm_cur_line .. dm_cur_line + max_visible)
        // Walk dm_line_counts to find first/last message index needed
        int line_acc = 0;
        uint32_t first_msg = 0;

        // Find first message that overlaps the visible window
        for (uint32_t i = 0; i < _data.dm_msg_count; i++)
        {
            int msg_lines = _data.dm_line_counts[i];
            if (line_acc + msg_lines > _data.dm_cur_line)
            {
                first_msg = i;
                break;
            }
            line_acc += msg_lines;
        }

        // Find last message that overlaps (estimate: load a few extra)
        int visible_end = _data.dm_cur_line + max_visible;
        uint32_t last_msg = first_msg;
        int running = line_acc;
        for (uint32_t i = first_msg; i < _data.dm_msg_count; i++)
        {
            last_msg = i;
            running += _data.dm_line_counts[i];
            if (running >= visible_end)
                break;
        }

        // Load only the needed messages from file
        uint32_t range_count = last_msg - first_msg + 1;
        std::vector<Mesh::TextMessage> visible_msgs;
        visible_msgs.reserve(range_count);
        auto& store = Mesh::MeshDataStore::getInstance();
        store.getDMMessageRange(_data.selected_node_id, first_msg, range_count, visible_msgs);

        // Render
        uint32_t our_id = _data.hal->mesh()->getNodeId();
        int y = messages_area_top;
        int current_line = line_acc; // global line counter starting from first_msg's start

        for (uint32_t mi = 0; mi < visible_msgs.size(); mi++)
        {
            const auto& msg = visible_msgs[mi];
            auto wrapped = wrap_text(msg.text, _data.dm_chars_per_line);

            bool is_ours = (msg.from == our_id);
            uint32_t sender_bg = _get_node_color(msg.from);
            uint32_t sender_fg = _get_node_text_color(msg.from);
            const char* sender_name = is_ours ? "Me" : node.info.user.short_name;

            for (size_t line_idx = 0; line_idx < wrapped.size(); line_idx++)
            {
                if (current_line < _data.dm_cur_line)
                {
                    current_line++;
                    continue;
                }
                if (current_line >= _data.dm_cur_line + max_visible)
                    break;

                // Draw message text line, first to make datetime visible
                canvas->setTextColor(TFT_WHITE, THEME_COLOR_BG);
                canvas->drawString(wrapped[line_idx].c_str(), text_start_x, y + 1);
                // Draw sender name tag only on first line of message
                if (line_idx == 0)
                {
                    canvas->setTextColor(sender_fg, sender_bg);
                    if (_data.dm_ctrl && msg.timestamp > 0)
                    {
                        std::string dt = std::string(sender_name) + " \u2192 " + UTILS::TEXT::format_timestamp(msg.timestamp);
                        canvas->fillRoundRect(2, y + 1, canvas->textWidth(dt.c_str()) + 6, DM_ITEM_HEIGHT, 3, sender_bg);
                        canvas->drawString(dt.c_str(), 2 + 3, y + 1);
                    }
                    else
                    {
                        canvas->fillRoundRect(2, y + 1, name_col_width, DM_ITEM_HEIGHT, 3, sender_bg);
                        canvas->drawString(sender_name, 2 + 3, y + 1);

                        // Draw delivery status indicator
                        if (is_ours)
                        {
                            auto si = UTILS::UI::message_status_info((int)msg.status);
                            canvas->setFont(FONT_6);
                            canvas->setTextColor(si.color, sender_bg);
                            canvas->drawRightString(si.icon, name_col_width + 1, y + 2);
                            // repair bg
                            canvas->setFont(FONT_12);
                        }
                    }
                }

                y += DM_ITEM_HEIGHT + 1;
                current_line++;
            }
        }

        // Draw scroll bar if needed
        UTILS::UI::draw_scrollbar(canvas,
                                  canvas->width() - SCROLL_BAR_WIDTH - 1,
                                  messages_area_top,
                                  SCROLL_BAR_WIDTH,
                                  messages_area_height,
                                  _data.dm_total_lines,
                                  max_visible,
                                  _data.dm_cur_line,
                                  SCROLLBAR_MIN_HEIGHT);
    }
    return true;
}

void AppNodes::_apply_sort_order(Mesh::SortOrder new_order)
{
    _data.sort_order = new_order;
    // Selection will be resolved from list_selected_node_id in _render_node_list()
    scroll_text_reset(&_data.name_scroll_ctx);
    _data.update_list = true;
}

void AppNodes::_handle_node_list_input()
{
    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    if (_data.hal->keyboard()->isPressed())
    {
        uint32_t now = millis();
        // check Fn key hold
        auto keys_state = _data.hal->keyboard()->keysState();

        if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ESC) || _data.hal->home_button()->is_pressed())
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ESC);
            destroyApp();
            return;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_BACKSPACE);
            // Load the selected node from storage
            _data.selected_node_valid = _data.hal->nodedb()->getNodeByIndex(_data.selected_index, _data.selected_node);
            // delete selected node
            if (_data.selected_node_valid)
            {
                _data.selected_node_id = _data.selected_node.info.num;
                std::string name = _data.selected_node.info.user.long_name;
                if (name.empty())
                {
                    name = _data.selected_node.info.user.short_name;
                    if (name.empty())
                    {
                        name = std::format("!{:08x}", _data.selected_node_id);
                    }
                }
                if (UTILS::UI::show_confirmation_dialog(_data.hal, name, "Delete node?", "Delete", "Cancel"))
                {
                    if (!_data.hal->nodedb()->removeNode(_data.selected_node_id))
                    {
                        std::string error_message = std::format("to delete {}", _data.selected_node.info.user.long_name);
                        UTILS::UI::show_error_dialog(_data.hal, "Failed", error_message);
                    }
                }
                // have to recover from dialog by redrawing the list
                _data.update_list = true;
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                bool sound = false;
                // Fn holding? = to the end
                if (keys_state.fn)
                {
                    // to the end
                    if (_data.selected_index < (int)_data.total_node_count - 1)
                    {
                        _data.selected_index = (int)_data.total_node_count - 1;
                        _data.list_selected_node_id = 0; // Let render capture new node
                        _data.scroll_offset = std::max(0, _data.selected_index - LIST_MAX_VISIBLE_ITEMS + 1);
                        scroll_text_reset(&_data.name_scroll_ctx);
                        _data.update_list = true;
                        sound = true;
                    }
                }
                else
                {
                    if (_data.selected_index < (int)_data.total_node_count - 1)
                    {
                        _data.selected_index++;
                        _data.list_selected_node_id = 0; // Let render capture new node
                        if (_data.selected_index >= _data.scroll_offset + LIST_MAX_VISIBLE_ITEMS)
                        {
                            _data.scroll_offset++;
                        }
                        sound = true;
                    }
                }
                if (sound)
                {
                    _data.hal->playNextSound();
                    scroll_text_reset(&_data.name_scroll_ctx);
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_UP))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                bool sound = false;
                // Fn holding? = to the start
                if (keys_state.fn)
                {
                    // to the start
                    if (_data.selected_index > 0)
                    {
                        _data.selected_index = 0;
                        _data.list_selected_node_id = 0; // Let render capture new node
                        _data.scroll_offset = 0;
                        sound = true;
                    }
                }
                else
                {
                    if (_data.selected_index > 0)
                    {
                        _data.selected_index--;
                        _data.list_selected_node_id = 0; // Let render capture new node
                        if (_data.selected_index < _data.scroll_offset)
                        {
                            _data.scroll_offset--;
                        }
                        sound = true;
                    }
                }
                if (sound)
                {
                    _data.hal->playNextSound();
                    scroll_text_reset(&_data.name_scroll_ctx);
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
        {
            // Page Down
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                int max_index = (int)_data.total_node_count - 1;
                if (_data.selected_index < max_index)
                {
                    _data.hal->playNextSound();
                    _data.selected_index = std::min(_data.selected_index + LIST_MAX_VISIBLE_ITEMS, max_index);
                    _data.list_selected_node_id = 0; // Let render capture new node
                    _data.scroll_offset = std::max(0, _data.selected_index - LIST_MAX_VISIBLE_ITEMS + 1);
                    scroll_text_reset(&_data.name_scroll_ctx);
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
        {
            // Page Up
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                if (_data.selected_index > 0)
                {
                    _data.hal->playNextSound();
                    _data.selected_index = std::max(0, _data.selected_index - LIST_MAX_VISIBLE_ITEMS);
                    _data.list_selected_node_id = 0; // Let render capture new node
                    _data.scroll_offset = std::max(0, _data.selected_index);
                    scroll_text_reset(&_data.name_scroll_ctx);
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ENTER);

            if (_data.total_node_count > 0 && _data.hal->nodedb())
            {
                // Load the selected node from storage
                _data.selected_node_valid = _data.hal->nodedb()->getNodeByIndex(_data.selected_index, _data.selected_node);

                if (_data.selected_node_valid)
                {
                    _data.selected_node_id = _data.selected_node.info.num;
                    _data.detail_scroll = 0;
                    if (keys_state.fn)
                    {
                        _data.view_state = ViewState::NODE_DETAIL;
                        _data.update_list = true;
                    }
                    else
                        _data.view_state = ViewState::DIRECT_MESSAGE;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_F))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_F);

            // Load the currently selected node first
            if (_data.total_node_count > 0 && _data.hal->nodedb())
            {
                Mesh::NodeInfo node;
                if (_data.hal->nodedb()->getNodeByIndex(_data.selected_index, node))
                {
                    // Toggle favorite and save
                    bool new_favorite = !node.info.is_favorite;
                    _data.hal->nodedb()->setFavorite(node.info.num, new_favorite);
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_T))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_T);

            if (_data.total_node_count > 0 && _data.hal->nodedb())
            {
                _data.selected_node_valid = _data.hal->nodedb()->getNodeByIndex(_data.selected_index, _data.selected_node);
                if (_data.selected_node_valid)
                {
                    _data.selected_node_id = _data.selected_node.info.num;
                    _data.tr_total_count = Mesh::MeshDataStore::getInstance().getTraceRouteCount(_data.selected_node_id);
                    _data.tr_selected_index = std::max(0, (int)_data.tr_total_count - 1);
                    _data.tr_scroll_offset = 0;
                    _data.tr_detail_scroll = 0;
                    if (keys_state.fn)
                    {
                        _start_traceroute();
                    }
                    _data.view_state = ViewState::TRACEROUTE_LOG;
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_R))
        {
            if (_data.total_node_count > 0 && _data.hal->nodedb())
            {
                Mesh::NodeInfo node;
                if (_data.hal->nodedb()->getNodeByIndex(_data.selected_index, node) && node.info.hops_away > 0 &&
                    node.relay_node != 0)
                {
                    uint32_t relay_id = _data.hal->nodedb()->findNodeByRelayByte(node.relay_node);
                    if (relay_id != 0)
                    {
                        _data.hal->playNextSound();
                        _data.hal->keyboard()->waitForRelease(KEY_NUM_R);
                        _data.list_selected_node_id = relay_id;
                        scroll_text_reset(&_data.name_scroll_ctx);
                        _data.update_list = true;
                    }
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_N))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_N);

            if (_data.total_node_count > 0 && _data.hal->mesh() && _data.hal->nodedb())
            {
                Mesh::NodeInfo node;
                if (_data.hal->nodedb()->getNodeByIndex(_data.selected_index, node))
                {
                    std::string title = node.info.user.long_name[0]    ? node.info.user.long_name
                                        : node.info.user.short_name[0] ? node.info.user.short_name
                                                                       : std::format("{:04x}", node.info.num & 0xFFFF);
                    if (UTILS::UI::show_confirmation_dialog(_data.hal, title, "Exchange node information?", "Send", "Cancel"))
                    {
                        _data.hal->mesh()->sendNodeInfo(node.info.num, node.info.channel, true);
                    }
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_P))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_P);

            if (_data.total_node_count > 0 && _data.hal->mesh() && _data.hal->nodedb())
            {
                Mesh::NodeInfo node;
                if (_data.hal->nodedb()->getNodeByIndex(_data.selected_index, node))
                {
                    const auto& cfg = _data.hal->mesh()->getConfig();
                    std::string pos_info;
                    if (cfg.position == Mesh::MeshConfig::POSITION_OFF)
                    {
                        UTILS::UI::show_error_dialog(_data.hal,
                                                     "Exchange position",
                                                     "Position sharing is disabled in settings");
                        _data.update_list = true;
                        return;
                    }
                    else if (cfg.position == Mesh::MeshConfig::POSITION_FIXED)
                    {
                        char buf[64];
                        snprintf(buf,
                                 sizeof(buf),
                                 "Fixed position: %.5f, %.5f, %ldm",
                                 cfg.fixed_latitude * 1e-7,
                                 cfg.fixed_longitude * 1e-7,
                                 cfg.fixed_altitude);
                        pos_info = buf;
                    }
                    else // POSITION_GPS
                    {
                        pos_info = "Live GPS position";
                    }

                    std::string title = node.info.user.long_name[0]    ? node.info.user.long_name
                                       : node.info.user.short_name[0] ? node.info.user.short_name
                                                                      : std::format("{:04x}", node.info.num & 0xFFFF);
                    if (UTILS::UI::show_confirmation_dialog(_data.hal, title, pos_info, "Send", "Cancel"))
                    {
                        if (!_data.hal->mesh()->sendPosition(node.info.num, node.info.channel, true))
                        {
                            UTILS::UI::show_error_dialog(_data.hal, "Exchange position", "Failed to send position");
                        }
                    }
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_TAB))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_TAB);

            int current = static_cast<int>(_data.sort_order);
            int result = UTILS::UI::show_select_dialog(_data.hal, "Select sort order", sort_labels, current);
            if (result >= 0 && result < static_cast<int>(sort_labels.size()))
            {
                _apply_sort_order(static_cast<Mesh::SortOrder>(result));
            }
            else
            {
                _data.update_list = true;
            }
        }
        else
        {
            // Quick sort keys: 1-8 map directly to SortOrder enum values 0-7
            static const uint8_t sort_keys[] =
                {KEY_NUM_1, KEY_NUM_2, KEY_NUM_3, KEY_NUM_4, KEY_NUM_5, KEY_NUM_6, KEY_NUM_7, KEY_NUM_8};
            for (int i = 0; i < 8; i++)
            {
                if (_data.hal->keyboard()->isKeyPressing(sort_keys[i]))
                {
                    UTILS::UI::show_progress(_data.hal, "Sorting by", -1, sort_labels[i]);
                    _data.hal->playNextSound();
                    _data.hal->keyboard()->waitForRelease(sort_keys[i]);
                    _apply_sort_order(static_cast<Mesh::SortOrder>(i));
                    break;
                }
            }
        }
    }
    else
    {
        is_repeat = false;
    }
}

void AppNodes::_handle_node_detail_input()
{
    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    if (_data.hal->keyboard()->isPressed())
    {
        if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ESC))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ESC);
            _data.selected_node_valid = false;
            _data.view_state = ViewState::NODE_LIST;
            _data.update_list = true;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ENTER);

            // Open DM view
            _refresh_messages();
            // Auto-scroll to bottom (newest messages visible)
            {
                auto* canvas = _data.hal->canvas();
                int area_bottom = canvas->height() - DM_HEADER_HEIGHT - 1;
                int max_visible = (area_bottom - DM_HEADER_HEIGHT) / DM_ITEM_HEIGHT;
                _data.dm_cur_line = _data.dm_total_lines > max_visible ? _data.dm_total_lines - max_visible : 0;
            }
            _data.view_state = ViewState::DIRECT_MESSAGE;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_T))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_T);
            _data.tr_total_count = Mesh::MeshDataStore::getInstance().getTraceRouteCount(_data.selected_node_id);
            _data.tr_selected_index = std::max(0, (int)_data.tr_total_count - 1);
            _data.tr_scroll_offset = 0;
            _data.tr_detail_scroll = 0;
            _start_traceroute();
            _data.view_state = ViewState::TRACEROUTE_LOG;
            _data.update_list = true;
        }
    }
    else
    {
        is_repeat = false;
    }
}

void AppNodes::_handle_dm_input()
{
    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    auto keys_state = _data.hal->keyboard()->keysState();
    if (_data.dm_ctrl != keys_state.ctrl)
    {
        _data.dm_ctrl = keys_state.ctrl;
        _data.update_list = true;
    }
    if (_data.hal->keyboard()->isPressed())
    {
        uint32_t now = millis();

        if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ESC))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ESC);
            // Keep selected_node_valid as we're going back to detail view
            _data.update_list = true;
            _data.view_state = ViewState::NODE_LIST;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_BACKSPACE);
            // clear all messages
            if (UTILS::UI::show_confirmation_dialog(_data.hal,
                                                    _data.selected_node.info.user.long_name,
                                                    "Clear chat?",
                                                    "Clear",
                                                    "Cancel"))
            {
                Mesh::MeshDataStore::getInstance().clearConversation(_data.selected_node_id, false);
            }
            // have to recover from dialog by redrawing the list
            _data.update_list = true;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ENTER);

            // Open text input dialog (max 200 bytes)
            std::string message_text;
            std::string title = std::format("Message to: {}", _data.selected_node.info.user.long_name);
            if (UTILS::UI::show_edit_string_dialog(_data.hal, title, message_text, false, 200))
            {
                if (!message_text.empty())
                {
                    _send_message(message_text);
                }
            }
            // Redraw after dialog closes
            _data.update_list = true;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_I))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_I);

            // Send channel invitation: show channel selection dialog
            auto* nodedb = _data.hal->nodedb();
            std::vector<std::string> channel_labels;
            std::vector<uint8_t> channel_indices;
            if (nodedb)
            {
                for (uint8_t i = 0; i < 8; i++)
                {
                    auto* ch = nodedb->getChannel(i);
                    if (!ch || ch->role == meshtastic_Channel_Role_DISABLED || !ch->has_settings ||
                        ch->settings.name[0] == '\0')
                        continue;
                    channel_labels.push_back(std::format("[{}] {}", (int)i, ch->settings.name));
                    channel_indices.push_back(i);
                }
            }
            if (channel_labels.empty())
            {
                UTILS::UI::show_error_dialog(_data.hal, "Invite", "No channels to invite");
            }
            else
            {
                std::string title = std::format("Invite {} to channel:", _data.selected_node.info.user.short_name);
                int sel = UTILS::UI::show_select_dialog(_data.hal, title, channel_labels, 0);
                if (sel >= 0 && sel < (int)channel_indices.size())
                {
                    uint8_t ch_idx = channel_indices[(size_t)sel];
                    auto* ch = nodedb->getChannel(ch_idx);
                    if (ch && ch->settings.psk.size > 0 && ch->settings.psk.size <= 32)
                    {
                        size_t b64_len = 0;
                        mbedtls_base64_encode(nullptr, 0, &b64_len, ch->settings.psk.bytes, ch->settings.psk.size);
                        std::string psk_b64(b64_len, '\0');
                        mbedtls_base64_encode((unsigned char*)psk_b64.data(),
                                              psk_b64.size(),
                                              &b64_len,
                                              ch->settings.psk.bytes,
                                              ch->settings.psk.size);
                        std::string invite = std::format("#invite {}={}", ch->settings.name, psk_b64);

                        bool has_pubkey =
                            _data.selected_node.info.has_user && _data.selected_node.info.user.public_key.size == 32;
                        if (!has_pubkey &&
                            !UTILS::UI::show_confirmation_dialog(_data.hal,
                                                                 "Unsecure",
                                                                 "Node has no public key. Data could be leaked. Send anyway?",
                                                                 "Send",
                                                                 "Cancel"))
                        {
                            // User cancelled
                        }
                        else
                        {
                            _send_message(invite);
                        }
                    }
                    else
                    {
                        UTILS::UI::show_error_dialog(_data.hal, "Invite", "Channel has no PSK");
                    }
                }
            }
            _data.update_list = true;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_UP))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                if (_data.dm_cur_line > 0)
                {
                    _data.dm_cur_line--;
                    _data.hal->playNextSound();
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
        {
            int area_bottom = _data.hal->canvas()->height() - DM_HEADER_HEIGHT - 1;
            int max_visible = (area_bottom - DM_HEADER_HEIGHT) / DM_ITEM_HEIGHT;
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                if (_data.dm_cur_line < _data.dm_total_lines - max_visible)
                {
                    _data.dm_cur_line++;
                    _data.hal->playNextSound();
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
        {
            // page up
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                int area_bottom = _data.hal->canvas()->height() - DM_HEADER_HEIGHT - 1;
                int page_size = (area_bottom - DM_HEADER_HEIGHT) / DM_ITEM_HEIGHT;
                if (_data.dm_cur_line > 0)
                {
                    _data.hal->playNextSound();
                    _data.dm_cur_line = std::max(0, _data.dm_cur_line - page_size);
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
        {
            // page down
            int area_bottom = _data.hal->canvas()->height() - DM_HEADER_HEIGHT - 1;
            int page_size = (area_bottom - DM_HEADER_HEIGHT) / DM_ITEM_HEIGHT;
            int max_scroll = _data.dm_total_lines - page_size;
            if (key_repeat_check(is_repeat, next_fire_ts, now) && max_scroll > 0)
            {
                if (_data.dm_cur_line < max_scroll)
                {
                    _data.dm_cur_line = std::min(_data.dm_cur_line + page_size, max_scroll);
                    _data.hal->playNextSound();
                    _data.update_list = true;
                }
            }
        }
    } // not pressed = stop repeating
    else
    {
        is_repeat = false;
    }
}

void AppNodes::_start_traceroute()
{
    if (!_data.hal->mesh() || !_data.selected_node_valid)
        return;

    uint32_t target = _data.selected_node_id;
    uint32_t our_id = _data.hal->mesh()->getNodeId();
    if (target == our_id || target == 0xFFFFFFFF)
        return;

    Mesh::TraceRouteResult result;
    result.target_node_id = target;
    result.timestamp = (uint32_t)time(nullptr);
    result.duration_sec = 0;
    result.status = Mesh::TraceRouteResult::Status::PENDING;
    result.dest_snr = 0.0f;
    result.origin_snr = 0.0f;

    auto& store = Mesh::MeshDataStore::getInstance();
    uint32_t result_index = store.addTraceRoute(target, result);
    _data.tr_total_count = store.getTraceRouteCount(target);

    // UI-only callback — storage is handled by MeshService::handleTraceRoutePacket
    _data.hal->mesh()->setTraceRouteCallback(
        [this, target](uint32_t node_id, uint32_t index)
        {
            if (node_id != target)
                return;
            _data.tr_total_count = Mesh::MeshDataStore::getInstance().getTraceRouteCount(target);
            _data.update_list = true;
        });

    _data.hal->mesh()->sendTraceRoute(target, 0);
    _data.tr_selected_index = (int)result_index;
}

bool AppNodes::_render_traceroute_log()
{
    if (!_data.update_list)
        return false;
    _data.update_list = false;

    auto* canvas = _data.hal->canvas();
    canvas->fillScreen(THEME_COLOR_BG);
    canvas->setFont(FONT_12);

    // Header
    std::string header_name = _data.selected_node_valid ? _data.selected_node.info.user.short_name : "???";
    canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    canvas->drawString("<", 2, 0);
    canvas->drawString("Traceroute", 14, 0);
    uint32_t badge_color = _get_node_color(_data.selected_node_id);
    uint32_t badge_text = _get_node_text_color(_data.selected_node_id);
    int badge_w = COL_SHORT_NAME_WIDTH;
    int badge_x = canvas->width() - badge_w - 2;
    canvas->fillRoundRect(badge_x, 0, badge_w, LIST_ITEM_HEIGHT, 4, badge_color);
    canvas->setTextColor(badge_text, badge_color);
    canvas->drawCenterString(header_name.c_str(), badge_x + badge_w / 2, 0);
    canvas->drawFastHLine(0, 14, canvas->width(), THEME_COLOR_BG_SELECTED);

    auto& store = Mesh::MeshDataStore::getInstance();
    int total = (int)_data.tr_total_count;
    if (total == 0)
    {
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawCenterString("<no attempts>", canvas->width() / 2, canvas->height() / 2 - 6);
        return true;
    }

    // Clamp selection
    if (_data.tr_selected_index >= total)
        _data.tr_selected_index = total - 1;
    if (_data.tr_selected_index < 0)
        _data.tr_selected_index = 0;

    const int item_y_start = 15;
    const int max_visible = (canvas->height() - item_y_start - 9) / (LIST_ITEM_HEIGHT + 1);

    // Adjust scroll
    if (_data.tr_selected_index < _data.tr_scroll_offset)
        _data.tr_scroll_offset = _data.tr_selected_index;
    if (_data.tr_selected_index >= _data.tr_scroll_offset + max_visible)
        _data.tr_scroll_offset = _data.tr_selected_index - max_visible + 1;

    // Load only the visible range from file
    std::vector<Mesh::TraceRouteResult> visible;
    int vis_count = std::min(max_visible, total - _data.tr_scroll_offset);
    store.getTraceRouteRange(_data.selected_node_id, (uint32_t)_data.tr_scroll_offset, (uint32_t)vis_count, visible);

    int y = item_y_start;
    for (int i = 0; i < (int)visible.size(); i++)
    {
        int idx = _data.tr_scroll_offset + i;
        const auto& res = visible[i];
        bool selected = (idx == _data.tr_selected_index);

        uint32_t bg = selected ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG;
        uint32_t fg = selected ? THEME_COLOR_SELECTED : THEME_COLOR_UNSELECTED;

        if (selected)
            canvas->fillRect(2, y, canvas->width() - 4, LIST_ITEM_HEIGHT, THEME_COLOR_BG_SELECTED);

        uint16_t* icon = (uint16_t*)image_data_trace_pending;
        std::string hops_to, hops_back, no_route, pending;
        switch (res.status)
        {
        case Mesh::TraceRouteResult::Status::SUCCESS:
            hops_to = std::format("\u2192{}", (int)res.route_to.size());
            canvas->setTextColor(selected ? THEME_COLOR_SELECTED : TFT_CYAN, bg);
            canvas->drawString(hops_to.c_str(), canvas->width() - 50, y + 1);

            hops_back = std::format("\u2190{}", (int)res.route_back.size());
            canvas->setTextColor(selected ? THEME_COLOR_SELECTED : TFT_MAGENTA, bg);
            canvas->drawString(hops_back.c_str(), canvas->width() - 28, y + 1);

            icon = (uint16_t*)image_data_trace_ok;
            break;
        case Mesh::TraceRouteResult::Status::FAILED:
            // print no route
            no_route = "no route";
            canvas->setTextColor(selected ? THEME_COLOR_SELECTED : THEME_COLOR_SIGNAL_NONE, bg);
            canvas->drawString(no_route.c_str(), canvas->width() - 58, y + 1);
            icon = (uint16_t*)image_data_trace_err;
            break;
        case Mesh::TraceRouteResult::Status::PENDING:
        default:
            pending = "pending";
            canvas->setTextColor(selected ? THEME_COLOR_SELECTED : THEME_COLOR_UNSELECTED, bg);
            canvas->drawString(pending.c_str(), canvas->width() - 58, y + 1);
            icon = (uint16_t*)image_data_trace_pending;
            break;
        }
        // draw status icon
        canvas->pushImage(4, y + 1, 12, 12, icon, TFT_WHITE);
        // draw timestamp column
        std::string time_str = UTILS::TEXT::format_timestamp(res.timestamp);
        canvas->setTextColor(fg, bg);
        canvas->drawString(time_str.c_str(), 18, y + 1);

        if (res.duration_sec > 0)
        {
            std::string dur_str = std::format("{:2d}s", res.duration_sec);
            canvas->setTextColor(selected ? THEME_COLOR_SELECTED : THEME_COLOR_SIGNAL_FAIR, bg);
            canvas->drawString(dur_str.c_str(), canvas->width() - 78, y + 1);
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
                              _data.tr_scroll_offset,
                              SCROLLBAR_MIN_HEIGHT);

    return true;
}

bool AppNodes::_render_traceroute_hint()
{
    return hl_text_render(&_data.hint_hl_ctx,
                          HINT_TR_LOG,
                          0,
                          _data.hal->canvas()->height() - 9,
                          TFT_DARKGREY,
                          TFT_WHITE,
                          THEME_COLOR_BG);
}

bool AppNodes::_render_traceroute_detail_hint()
{
    return hl_text_render(&_data.hint_hl_ctx,
                          HINT_TR_DETAIL,
                          0,
                          _data.hal->canvas()->height() - 9,
                          TFT_DARKGREY,
                          TFT_WHITE,
                          THEME_COLOR_BG);
}

void AppNodes::_handle_traceroute_log_input()
{
    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    if (_data.hal->keyboard()->isPressed())
    {
        uint32_t now = millis();

        if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ESC))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ESC);
            _data.hal->mesh()->setTraceRouteCallback(nullptr);
            _data.view_state = ViewState::NODE_LIST;
            _data.update_list = true;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_T))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_T);
            _start_traceroute();
            _data.update_list = true;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ENTER);
            if (_data.tr_total_count > 0)
            {
                auto& store = Mesh::MeshDataStore::getInstance();
                Mesh::TraceRouteResult res;
                if (store.getTraceRouteByIndex(_data.selected_node_id, (uint32_t)_data.tr_selected_index, res) &&
                    res.status == Mesh::TraceRouteResult::Status::SUCCESS)
                {
                    _data.tr_detail_result = std::move(res);
                    _data.tr_detail_scroll = 0;
                    _data.view_state = ViewState::TRACEROUTE_DETAIL;
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.tr_selected_index < (int)_data.tr_total_count - 1)
            {
                _data.tr_selected_index++;
                _data.hal->playNextSound();
                _data.update_list = true;
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_UP))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.tr_selected_index > 0)
            {
                _data.tr_selected_index--;
                _data.hal->playNextSound();
                _data.update_list = true;
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                int page = (_data.hal->canvas()->height() - 15 - 9) / (LIST_ITEM_HEIGHT + 1);
                int last = (int)_data.tr_total_count - 1;
                if (_data.tr_selected_index < last)
                {
                    _data.tr_selected_index = std::min(_data.tr_selected_index + page, last);
                    _data.hal->playNextSound();
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.tr_selected_index > 0)
            {
                int page = (_data.hal->canvas()->height() - 15 - 9) / (LIST_ITEM_HEIGHT + 1);
                _data.tr_selected_index = std::max(_data.tr_selected_index - page, 0);
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

bool AppNodes::_render_traceroute_detail()
{
    if (!_data.update_list)
        return false;
    _data.update_list = false;

    auto* canvas = _data.hal->canvas();
    canvas->fillScreen(THEME_COLOR_BG);
    canvas->setFont(FONT_12);

    const auto& res = _data.tr_detail_result;

    // Header
    canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    canvas->drawString("<", 2, 0);
    std::string time_str = res.timestamp > 0 ? UTILS::TEXT::format_timestamp(res.timestamp) : "—";
    canvas->drawString(time_str.c_str(), 14, 0);

    int badge_w = COL_SHORT_NAME_WIDTH;
    int badge_gap = 10;

    auto* nodedb = _data.hal->nodedb();

    // Helper: resolve short name from node ID (falls back to mesh config for our own node)
    uint32_t our_id = _data.hal->mesh() ? _data.hal->mesh()->getNodeId() : 0;
    auto* tr_mesh = _data.hal->mesh();
    auto get_short = [nodedb, tr_mesh, our_id](uint32_t nid) -> std::string
    {
        Mesh::NodeInfo ni;
        if (nodedb && nodedb->getNode(nid, ni) && ni.info.user.short_name[0])
            return ni.info.user.short_name;
        if (tr_mesh && nid == our_id && tr_mesh->getConfig().short_name[0])
            return tr_mesh->getConfig().short_name;
        return std::format("{:04x}", nid & 0xFFFF);
    };

    // Our node badge (left)
    std::string our_name = get_short(our_id);
    uint32_t our_badge_color = _get_node_color(our_id);
    uint32_t our_badge_text = _get_node_text_color(our_id);
    int dest_badge_x = canvas->width() - badge_w - badge_gap;
    int our_badge_x = dest_badge_x - badge_w - badge_gap;
    canvas->fillRoundRect(our_badge_x, 0, badge_w, LIST_ITEM_HEIGHT, 4, our_badge_color);
    canvas->setTextColor(our_badge_text, our_badge_color);
    canvas->drawCenterString(our_name.c_str(), our_badge_x + badge_w / 2, 0);
    // arrow direction
    canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas->drawCenterString("\u2192", our_badge_x + badge_w + badge_gap / 2, 0);
    // Destination node badge (right)
    std::string dest_name = _data.selected_node_valid ? _data.selected_node.info.user.short_name : "???";
    uint32_t badge_color = _get_node_color(_data.selected_node_id);
    uint32_t badge_text = _get_node_text_color(_data.selected_node_id);
    canvas->fillRoundRect(dest_badge_x, 0, badge_w, LIST_ITEM_HEIGHT, 4, badge_color);
    canvas->setTextColor(badge_text, badge_color);
    canvas->drawCenterString(dest_name.c_str(), dest_badge_x + badge_w / 2, 0);
    // line
    canvas->drawFastHLine(0, 14, canvas->width(), THEME_COLOR_BG_SELECTED);

    // Build rows: each row is "name  snr_dB"
    // Layout: TO section then BACK section, displayed as a path
    // US -> hop1 -> hop2 -> DEST -> hop3 -> hop4 -> US
    struct PathRow
    {
        std::string name;
        float snr;
        bool is_header;
        uint32_t node_color;
    };
    std::vector<PathRow> rows;

    // "Route TO" header
    rows.push_back({"-- Route to (" + std::to_string(res.route_to.size()) + " hops) --", 0, true, 0});

    // Our node (origin)
    rows.push_back({get_short(our_id), 0, false, our_id});

    for (const auto& hop : res.route_to)
    {
        rows.push_back({get_short(hop.node_id), hop.snr, false, hop.node_id});
    }

    // Destination node (with SNR as received)
    rows.push_back({get_short(res.target_node_id), res.dest_snr, false, res.target_node_id});

    // "Route BACK" header
    rows.push_back({"-- Route back (" + std::to_string(res.route_back.size()) + " hops) --", 0, true, 0});

    // Destination (start of return)
    rows.push_back({get_short(res.target_node_id), 0, false, res.target_node_id});

    for (const auto& hop : res.route_back)
    {
        rows.push_back({get_short(hop.node_id), hop.snr, false, hop.node_id});
    }

    // Us (end, with SNR as received on return)
    rows.push_back({get_short(our_id), res.origin_snr, false, our_id});

    // Render visible rows
    const int row_height = 14;
    const int y_start = 15;
    const int max_visible = (canvas->height() - y_start - 9) / (row_height + 1);

    // Clamp scroll
    int total_rows = (int)rows.size();
    int max_scroll = std::max(0, total_rows - max_visible);
    if (_data.tr_detail_scroll > max_scroll)
        _data.tr_detail_scroll = max_scroll;
    if (_data.tr_detail_scroll < 0)
        _data.tr_detail_scroll = 0;

    int y = y_start;
    for (int i = 0; i < max_visible && (_data.tr_detail_scroll + i) < total_rows; i++)
    {
        const auto& row = rows[_data.tr_detail_scroll + i];

        if (row.is_header)
        {
            canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
            canvas->drawString(row.name.c_str(), 4, y);
        }
        else
        {
            // Node name pill
            uint32_t nc = _get_node_color(row.node_color);
            uint32_t ntc = _get_node_text_color(row.node_color);
            int pill_w = 4 * 6 + 6;
            canvas->fillRoundRect(4, y, pill_w, row_height, 4, nc);
            canvas->setTextColor(ntc, nc);
            canvas->drawCenterString(row.name.c_str(), 4 + pill_w / 2, y + 1);

            // Arrow
            canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
            canvas->drawString("\u2192", pill_w + 8, y + 1);

            // SNR value
            if (row.snr != 0.0f)
            {
                std::string snr_str = std::format("{:5.1f} dB", row.snr);
                uint32_t snr_color = _get_snr_color(row.snr);
                canvas->setTextColor(snr_color, THEME_COLOR_BG);
                canvas->drawString(snr_str.c_str(), pill_w + 20, y + 1);
            }
        }
        y += row_height + 1;
    }

    // Scrollbar
    UTILS::UI::draw_scrollbar(canvas,
                              canvas->width() - SCROLL_BAR_WIDTH - 1,
                              y_start,
                              SCROLL_BAR_WIDTH,
                              max_visible * row_height,
                              total_rows,
                              max_visible,
                              _data.tr_detail_scroll,
                              SCROLLBAR_MIN_HEIGHT);

    return true;
}

void AppNodes::_handle_traceroute_detail_input()
{
    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    if (_data.hal->keyboard()->isPressed())
    {
        uint32_t now = millis();

        if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ESC))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ESC);
            _data.view_state = ViewState::TRACEROUTE_LOG;
            _data.update_list = true;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                const auto& r = _data.tr_detail_result;
                int total_rows = 6 + (int)r.route_to.size() + (int)r.route_back.size();
                int max_visible = (_data.hal->canvas()->height() - 25) / 14;
                int max_scroll = std::max(0, total_rows - max_visible);
                if (_data.tr_detail_scroll < max_scroll)
                {
                    _data.tr_detail_scroll++;
                    _data.hal->playNextSound();
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_UP))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.tr_detail_scroll > 0)
            {
                _data.tr_detail_scroll--;
                _data.hal->playNextSound();
                _data.update_list = true;
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                const auto& r = _data.tr_detail_result;
                int total_rows = 6 + (int)r.route_to.size() + (int)r.route_back.size();
                int max_visible = (_data.hal->canvas()->height() - 25) / 14;
                int max_scroll = std::max(0, total_rows - max_visible);
                if (_data.tr_detail_scroll < max_scroll)
                {
                    _data.tr_detail_scroll = std::min(_data.tr_detail_scroll + max_visible, max_scroll);
                    _data.hal->playNextSound();
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.tr_detail_scroll > 0)
            {
                int max_visible = (_data.hal->canvas()->height() - 25) / 14;
                _data.tr_detail_scroll = std::max(_data.tr_detail_scroll - max_visible, 0);
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

// ========== End Traceroute ==========

void AppNodes::_send_message(const std::string& text)
{
    if (!_data.hal->mesh() || text.empty())
        return;

    uint32_t packet_id = _data.hal->mesh()->sendText(text.c_str(), _data.selected_node_id, 0);

    if (packet_id != 0)
    {
        // Add to local store with the real packet ID (used for ACK matching)
        Mesh::TextMessage msg;
        msg.id = packet_id;
        msg.from = _data.hal->mesh()->getNodeId();
        msg.to = _data.selected_node_id;
        msg.timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
        msg.channel = 0;
        msg.is_direct = true;
        msg.read = true;
        msg.text = text;
        msg.status = Mesh::TextMessage::Status::SENT;

        Mesh::MeshDataStore::getInstance().addMessage(msg);
        _refresh_dm_line_counts();

        // Auto-scroll to bottom after sending
        {
            int area_bottom = _data.hal->canvas()->height() - DM_HEADER_HEIGHT - 1;
            int max_visible = (area_bottom - DM_HEADER_HEIGHT) / DM_ITEM_HEIGHT;
            _data.dm_cur_line = _data.dm_total_lines > max_visible ? _data.dm_total_lines - max_visible : 0;
        }

        ESP_LOGI(TAG,
                 "Sent DM to %08X (pkt=0x%08lX): %s",
                 (unsigned int)_data.selected_node_id,
                 (unsigned long)packet_id,
                 text.c_str());
    }
    else
    {
        ESP_LOGW(TAG, "Failed to send DM");
    }
}

std::string AppNodes::_format_last_seen(uint32_t last_heard)
{
    if (last_heard == 0)
        return "";

    uint32_t now = (uint32_t)time(nullptr);
    // If system time not yet synced or last_heard is from a different time base, show "?"
    if (now < last_heard)
        //   || now < 1700000000)
        return "";

    uint32_t elapsed = now - last_heard;

    if (elapsed < 60)
        return "now";
    else if (elapsed < 3600)
    {
        return std::format("{}m", (int)(elapsed / 60));
    }
    else if (elapsed < 86400)
    {
        return std::format("{}h", (int)(elapsed / 3600));
    }
    else if (elapsed < (uint32_t)(86400 * 30))
    {
        return std::format("{}d", (int)(elapsed / 86400));
    }
    else if (elapsed < (uint32_t)(86400 * 365))
    {
        return std::format("{}M", (int)(elapsed / (86400 * 30)));
    }
    else if (elapsed < (uint32_t)(86400U * 365 * 99))
    {
        return std::format("{}y", (int)(elapsed / (86400 * 365)));
    }
    else
    {
        return "...";
    }
}

std::string AppNodes::_format_node_id(uint32_t id)
{
    char buf[12];
    snprintf(buf, sizeof(buf), "!%08x", (unsigned int)id);
    return buf;
}
