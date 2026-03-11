/**
 * @file app_channels.cpp
 * @author d4rkmen
 * @brief Channels widget implementation
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "app_channels.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "mesh/mesh_service.h"
#include "apps/utils/ui/dialog.h"
#include "apps/app_nodes/assets/key_pke.h"
#include "apps/app_nodes/assets/key_mismatch.h"
#include "apps/app_nodes/assets/key_shared.h"
#include "apps/app_channels/assets/edit.h"
#include "apps/app_channels/assets/sound_off.h"
#include "apps/app_nodes/assets/chat.h"
#include <cstring>
#include <format>
#include <algorithm>
#include <functional>
#include "apps/utils/text/text_utils.h"
#include "apps/utils/ui/draw_helper.h"
#include "apps/utils/ui/key_repeat.h"

static const char* TAG = "APP_CHANNELS";

static std::string base64_encode(const uint8_t* data, size_t len)
{
    if (len == 0)
        return "";
    size_t out_len = 0;
    mbedtls_base64_encode(nullptr, 0, &out_len, data, len);
    std::string out(out_len, '\0');
    mbedtls_base64_encode((unsigned char*)out.data(), out.size(), &out_len, data, len);
    out.resize(out_len);
    return out;
}

static std::vector<uint8_t> base64_decode(const std::string& b64)
{
    if (b64.empty())
        return {};
    size_t out_len = 0;
    mbedtls_base64_decode(nullptr, 0, &out_len, (const unsigned char*)b64.c_str(), b64.size());
    std::vector<uint8_t> out(out_len);
    if (mbedtls_base64_decode(out.data(), out.size(), &out_len, (const unsigned char*)b64.c_str(), b64.size()) != 0)
        return {};
    out.resize(out_len);
    return out;
}

// Hint strings for control hints
static const char* HINT_LIST = "[Fn] [\u2191][\u2193][\u2190][\u2192] [ENTER][DEL] [ESC]";
static const char* HINT_LIST_FN = "[ENTER]EDIT [SPACE]NEW";
static const char* HINT_CHAT = "[^]INFO [\u2191][\u2193][\u2190][\u2192] [ENTER][DEL] [ESC]";
static const char* HINT_CHAT_FN = "[\u2191]HOME [\u2193]END";
static const char* HINT_EDIT = "[\u2191][\u2193] [Fn] [ENTER] [ESC]";
static const char* HINT_EDIT_FN = "[ENTER]CUSTOM TEXT";

// Predefined greetings demonstrating macros: #short #long #id #hops #snr #rssi
static const std::vector<std::string> PREDEF_GREETING_CH = {
    "Off",
    "Hi #short!",
    "Welcome #short! You are #hops hops away from me",
    "Look who is here! #long (#short) I see you with #hops hops, #snr dB / #rssi dBm",
    "Hello #long, welcome! Signal: #snr dB / #rssi dBm",
    "Hey #short! #hops hops, #snr/#rssi",
};
static const std::vector<std::string> PREDEF_GREETING_DM = {
    "Off",
    "Hi #long (#short)!",
    "Welcome #short! You are #hops hops away",
    "Hello #long! Nice to meet you",
    "Hey #long, I see you with #hops hops #snr dB / #rssi dBm",
};
static const std::vector<std::string> PREDEF_PING_REPLY = {
    "Off",
    "#short: pong :)",
    "#short: [#hops] #snr dB / #rssi dBm",
    "#long: signal #snr/#rssi",
    "#long: [#hops] #snr dB / #rssi dBm",
};

// UI Constants - matching app_nodes compact layout
#define SCROLL_BAR_WIDTH 4
#define SCROLLBAR_MIN_HEIGHT 10
#define LIST_HEADER_HEIGHT 0
#define LIST_ITEM_HEIGHT 14
#define LIST_ITEM_LEFT_PADDING 4
#define LIST_MAX_VISIBLE_ITEMS 7
#define CHAT_HEADER_HEIGHT 14
#define CHAT_FOOTER_HEIGHT 9
#define CHAT_ITEM_HEIGHT 12

static void _draw_channel_header(LGFX_Sprite* canvas,
                                 const meshtastic_Channel& ch,
                                 const char* title,
                                 const uint16_t* right_icon,
                                 const char* right_text = nullptr)
{
    const int y = 0;
    bool is_primary = (ch.role == meshtastic_Channel_Role_PRIMARY);
    uint32_t ch_color = is_primary ? TFT_ORANGE : TFT_CYAN;
    int badge_width = 14;

    canvas->fillRoundRect(LIST_ITEM_LEFT_PADDING, y, badge_width, LIST_ITEM_HEIGHT, 4, ch_color);
    canvas->setTextColor(TFT_BLACK, ch_color);
    canvas->drawCenterString(std::format("{}", ch.index).c_str(), LIST_ITEM_LEFT_PADDING + badge_width / 2 - 1, y + 1);

    int name_x = LIST_ITEM_LEFT_PADDING + badge_width + 4;
    uint16_t* key_img = (uint16_t*)image_data_key_shared;
    if (ch.settings.psk.size > 0)
    {
        if (ch.settings.psk.size > 1)
            key_img = (uint16_t*)image_data_key_pke;
        else if (ch.settings.has_module_settings && ch.settings.module_settings.position_precision > 0)
            key_img = (uint16_t*)image_data_key_mismatch;
    }
    canvas->pushImage(name_x, y + 1, 12, 12, key_img, TFT_WHITE);
    name_x += 14;

    canvas->setTextColor(title ? TFT_ORANGE : THEME_COLOR_UNSELECTED, THEME_COLOR_BG);
    canvas->drawString(title ? title : ch.settings.name, name_x, y + 1);

    if (right_icon)
        canvas->pushImage(canvas->width() - 1 - 12, y, 12, 12, right_icon, TFT_WHITE);

    if (right_text)
    {
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawRightString(right_text, canvas->width() - 2 - 12, y);
    }

    canvas->drawFastHLine(0, CHAT_HEADER_HEIGHT, canvas->width() - 1, THEME_COLOR_BG_SELECTED);
}

static bool is_repeat = false;
static uint32_t next_fire_ts = 0xFFFFFFFF;

using UTILS::TEXT::count_wrapped_lines;
using UTILS::TEXT::utf8_advance;
using UTILS::TEXT::utf8_char_len;
using UTILS::TEXT::utf8_count;
using UTILS::TEXT::wrap_text;

using namespace MOONCAKE::APPS;
using namespace UTILS::HL_TEXT;

void AppChannels::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
    _data.view_state = ViewState::CHANNEL_LIST;
    _data.selected_index = 0;
    _data.scroll_offset = 0;
    _data.update_list = true;
    _data.last_msgstore_change = 0;
    _data.chat_msg_count = 0;
    _data.chat_cur_line = 0;
    _data.chat_total_lines = 0;
    _data.chat_chars_per_line = 20;
    _data.chat_ctrl = false;

    hl_text_init(&_data.hint_hl_ctx, _data.hal->canvas(), 20, 1500);
    UTILS::SCROLL_TEXT::scroll_text_init_ex(&_data.desc_scroll_ctx,
                                            _data.hal->canvas(),
                                            _data.hal->canvas()->width(),
                                            14,
                                            20,
                                            2000,
                                            FONT_12);
}

void AppChannels::onResume()
{
    ANIM_APP_OPEN();
    _data.hal->canvas()->fillScreen(THEME_COLOR_BG);
    _data.hal->canvas()->setFont(FONT_12);
    _data.hal->canvas()->setTextSize(1);
    _data.hal->canvas_update();

    _data.view_state = ViewState::CHANNEL_LIST;
    _data.selected_index = 0;
    _data.scroll_offset = 0;
    _data.update_list = true;
    _data.last_msgstore_change = 0;
    _data.chat_msg_count = 0;
    _data.chat_cur_line = 0;
    _data.chat_total_lines = 0;
    _data.chat_chars_per_line = 20;
    _data.chat_ctrl = false;
    _refresh_channels();
}

void AppChannels::onRunning()
{
    static ViewState last_view_state;
    bool msgstore_changed = false;
    bool view_state_changed = last_view_state != _data.view_state;
    last_view_state = _data.view_state;

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
    case ViewState::CHANNEL_LIST:
        if (msgstore_changed || view_state_changed)
        {
            _refresh_channels();
        }
        updated |= _render_channel_list();
        updated |= _render_list_hint();
        if (updated)
        {
            _data.hal->canvas_update();
        }
        _handle_channel_list_input();
        break;

    case ViewState::CHANNEL_CHAT:
    {
        auto* canvas = _data.hal->canvas();
        int max_visible = (canvas->height() - CHAT_HEADER_HEIGHT - CHAT_FOOTER_HEIGHT) / (CHAT_ITEM_HEIGHT + 1);
        int old_total = _data.chat_total_lines;
        bool was_at_bottom = (old_total <= max_visible) || (_data.chat_cur_line >= old_total - max_visible);
        if (msgstore_changed || view_state_changed)
        {
            _refresh_messages();
        }
        if (was_at_bottom && _data.chat_total_lines != old_total)
        {
            _data.chat_cur_line = _data.chat_total_lines > max_visible ? _data.chat_total_lines - max_visible : 0;
        }
        updated |= _render_channel_chat();
        updated |= _render_chat_hint();
        if (updated)
        {
            _data.hal->canvas_update();
        }
        _handle_channel_chat_input();
        break;
    }

    case ViewState::CHANNEL_EDIT:
        updated |= _render_channel_edit();
        updated |= _render_edit_desc();
        updated |= _render_edit_hint();
        if (updated)
        {
            _data.hal->canvas_update();
        }
        _handle_channel_edit_input();
        break;
    }
}

void AppChannels::onDestroy() { UTILS::SCROLL_TEXT::scroll_text_free(&_data.desc_scroll_ctx); }

// ─── Helpers ────────────────────────────────────────────────────────────────────

uint32_t AppChannels::_get_node_color(uint32_t node_id) { return UTILS::UI::node_color(node_id); }

uint32_t AppChannels::_get_node_text_color(uint32_t node_id) { return UTILS::UI::node_text_color(node_id); }

std::string AppChannels::_get_sender_name(uint32_t node_id)
{
    if (_data.hal->nodedb())
    {

        Mesh::NodeInfo node;
        if (_data.hal->nodedb()->getNode(node_id, node))
        {
            return Mesh::NodeDB::getLabel(node);
        }
    }
    return std::format("{:04x}", node_id & 0xFFFF);
}

void AppChannels::_refresh_channels()
{
    _data.channels.clear();

    if (!_data.hal->nodedb())
        return;

    auto* nodedb = _data.hal->nodedb();
    auto& store = Mesh::MeshDataStore::getInstance();

    for (uint8_t i = 0; i < 8; i++)
    {
        auto* ch = nodedb->getChannel(i);
        if (ch && ch->has_settings && ch->settings.name[0] != '\0')
        {
            _data.channels.push_back({*ch, store.getUnreadChannelCount(i)});
        }
    }

    if (_data.channels.empty())
    {
        meshtastic_Channel def = meshtastic_Channel_init_default;
        def.index = 0;
        def.has_settings = true;
        def.role = meshtastic_Channel_Role_PRIMARY;
        strncpy(def.settings.name, "Default", sizeof(def.settings.name) - 1);
        _data.channels.push_back({def, store.getUnreadChannelCount(0)});
    }

    _data.update_list = true;
}

void AppChannels::_refresh_messages()
{
    auto& store = Mesh::MeshDataStore::getInstance();
    store.markMessagesRead(_data.selected_channel, true);
    _refresh_chat_line_counts();
    _data.update_list = true;
}

void AppChannels::_refresh_chat_line_counts()
{
    auto* canvas = _data.hal->canvas();
    auto& store = Mesh::MeshDataStore::getInstance();

    const int name_col_width = 4 * 6 + 6;
    const int text_start_x = name_col_width + 2;
    const int max_text_width = canvas->width() - text_start_x - SCROLL_BAR_WIDTH - 2;
    _data.chat_chars_per_line = max_text_width / 6; // FONT_12 is 6px per char

    _data.chat_line_counts.clear();
    _data.chat_total_lines = 0;
    _data.chat_msg_count = 0;

    int cpl = _data.chat_chars_per_line;
    _data.chat_msg_count = store.forEachChannelMessage(_data.selected_channel,
                                                       [this, cpl](uint32_t /*index*/, const Mesh::TextMessage& msg) -> bool
                                                       {
                                                           uint16_t lc = count_wrapped_lines(msg.text, cpl);
                                                           _data.chat_line_counts.push_back(lc);
                                                           _data.chat_total_lines += lc;
                                                           return true;
                                                       });
}

void AppChannels::_send_message(const std::string& text)
{
    if (!_data.hal->mesh() || text.empty())
        return;

    uint32_t packet_id = _data.hal->mesh()->sendText(text.c_str(), 0xFFFFFFFF, _data.selected_channel);

    if (packet_id != 0)
    {
        Mesh::TextMessage msg;
        msg.id = packet_id;
        msg.from = _data.hal->mesh()->getNodeId();
        msg.to = 0xFFFFFFFF;
        msg.timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
        msg.channel = _data.selected_channel;
        msg.is_direct = false;
        msg.read = true;
        msg.text = text;
        msg.status = Mesh::TextMessage::Status::SENT;

        Mesh::MeshDataStore::getInstance().addMessage(msg);
        _refresh_messages();

        ESP_LOGI(TAG, "Sent to channel %d: %s", _data.selected_channel, text.c_str());
    }
}

// ─── Channel edit helpers ────────────────────────────────────────────────────────

static const std::vector<std::string> KEY_TYPE_OPTIONS = {"None", "Default", "8 bit", "128 bit", "256 bit"};

static const std::vector<std::string> NOTIFICATION_OPTIONS = {
    "Off", "Default", "Morse", "Seagull", "Tum-tum", "Pum-pam", "8 bit"};

static const char* notification_label(uint32_t index)
{

    if (index >= NOTIFICATION_OPTIONS.size())
        index = 0;
    return NOTIFICATION_OPTIONS[index].c_str();
}

static void psk_from_key_type(const std::string& type, meshtastic_ChannelSettings_psk_t& psk)
{
    memset(&psk, 0, sizeof(psk));
    if (type == "None")
    {
        psk.size = 0;
    }
    else if (type == "Default") // 1 byte key AQ==
    {
        psk.size = 1;
        psk.bytes[0] = 0x01;
    }
    else if (type == "8 bit")
    {
        psk.size = 1;
        psk.bytes[0] = 2;
    }
    else if (type == "128 bit")
    {
        psk.size = 16;
        for (int i = 0; i < 16; i++)
            psk.bytes[i] = (uint8_t)(esp_random() & 0xFF);
    }
    else if (type == "256 bit")
    {
        psk.size = 32;
        for (int i = 0; i < 32; i++)
            psk.bytes[i] = (uint8_t)(esp_random() & 0xFF);
    }
}

static std::string key_type_from_psk(const meshtastic_ChannelSettings_psk_t& psk)
{
    if (psk.size == 0)
        return "None";
    if (psk.size == 1)
    {
        if (psk.bytes[0] == 0x01)
            return "Default";
        else
            return "8 bit";
    }
    if (psk.size == 16)
        return "128 bit";
    if (psk.size == 32)
        return "256 bit";
    return std::format("Custom ({}B)", psk.size * 8);
}

struct EditItem
{
    const char* label;
    const char* description;
    int label_color;
    std::function<std::string(AppChannels*, const meshtastic_Channel&)> get_value;
    std::function<void(AppChannels*, meshtastic_Channel&)> on_enter;
};

static const std::vector<EditItem> EDIT_ITEMS = {
    {"Name",
     "Something for end users to call the channel. If this is the empty string it is assumed that this channel is the special "
     "(minimally secure) \"Default\" channel.",
     TFT_CYAN,
     [](AppChannels*, const meshtastic_Channel& ch) { return std::string(ch.settings.name); },
     [](AppChannels* app, meshtastic_Channel& ch)
     {
         std::string name(ch.settings.name);
         if (UTILS::UI::show_edit_string_dialog(app->get_hal(), "Channel name", name, false, 11))
         {
             memset(ch.settings.name, 0, sizeof(ch.settings.name));
             strncpy(ch.settings.name, name.c_str(), sizeof(ch.settings.name) - 1);
             ch.has_settings = true;
         }
     }},
    {"Role",
     "Primary, Secondary or Disabled channel role. Only one primary channel is allowed",
     TFT_CYAN,
     [](AppChannels*, const meshtastic_Channel& ch)
     {
         return ch.role == meshtastic_Channel_Role_PRIMARY     ? "Primary"
                : ch.role == meshtastic_Channel_Role_SECONDARY ? "Secondary"
                                                               : "Disabled";
     },
     [](AppChannels* app, meshtastic_Channel& ch)
     {
         static const std::vector<std::string> opts = {"Primary", "Secondary", "Disabled"};
         int cur = ch.role == meshtastic_Channel_Role_PRIMARY ? 0 : ch.role == meshtastic_Channel_Role_SECONDARY ? 1 : 2;
         int res = UTILS::UI::show_select_dialog(app->get_hal(), "Role", opts, cur);
         if (res == 0)
             ch.role = meshtastic_Channel_Role_PRIMARY;
         else if (res == 1)
             ch.role = meshtastic_Channel_Role_SECONDARY;
         else if (res == 2)
             ch.role = meshtastic_Channel_Role_DISABLED;
     }},
    {"Notification",
     "Notification sound for incoming messages: Off - no sound",
     TFT_CYAN,
     [](AppChannels*, const meshtastic_Channel& ch) { return std::string(notification_label(ch.settings.channel_num)); },
     [](AppChannels* app, meshtastic_Channel& ch)
     {
         // use deprecated channel_num field for notification
         uint32_t cur = ch.settings.channel_num;
         if (cur >= (int)NOTIFICATION_OPTIONS.size())
             cur = 0;
         int res = UTILS::UI::show_select_dialog(app->get_hal(), "Notification", NOTIFICATION_OPTIONS, (int)cur);
         if (res >= 0 && res < (int)NOTIFICATION_OPTIONS.size())
             ch.settings.channel_num = (uint32_t)res;
         if (res > 0)
             app->get_hal()->playNotificationSound(res);
     }},
    {"Key type",
     "Encryption key type: None, Default, 8 bit, 128 bit, or 256 bit. Automatically generated new key on change",
     TFT_CYAN,
     [](AppChannels*, const meshtastic_Channel& ch) { return key_type_from_psk(ch.settings.psk); },
     [](AppChannels* app, meshtastic_Channel& ch)
     {
         std::string cur = key_type_from_psk(ch.settings.psk);
         int cur_idx = 0;
         for (int i = 0; i < (int)KEY_TYPE_OPTIONS.size(); i++)
             if (KEY_TYPE_OPTIONS[i] == cur)
             {
                 cur_idx = i;
                 break;
             }
         int res = UTILS::UI::show_select_dialog(app->get_hal(), "Key type", KEY_TYPE_OPTIONS, cur_idx);
         if (res >= 0 && res < (int)KEY_TYPE_OPTIONS.size())
             psk_from_key_type(KEY_TYPE_OPTIONS[res], ch.settings.psk);
     }},
    {"Key",
     "Pre-shared encryption key in base64 format",
     TFT_CYAN,
     [](AppChannels*, const meshtastic_Channel& ch) { return base64_encode(ch.settings.psk.bytes, ch.settings.psk.size); },
     [](AppChannels* app, meshtastic_Channel& ch)
     {
         std::string psk_str = base64_encode(ch.settings.psk.bytes, ch.settings.psk.size);
         if (UTILS::UI::show_edit_string_dialog(app->get_hal(), "PSK (base64)", psk_str, false, 50))
         {
             auto decoded = base64_decode(psk_str);
             if (decoded.size() <= 32)
             {
                 ch.settings.psk.size = decoded.size();
                 memcpy(ch.settings.psk.bytes, decoded.data(), decoded.size());
             }
         }
     }},
// no wifi = no mqtt = no uplink/downlink
#if 0
    {"Uplink enabled",
     "If true, messages on the mesh will be sent to the \"public\" internet by any gateway node",
     TFT_CYAN,
     [](const meshtastic_Channel& ch) { return ch.settings.uplink_enabled ? "true" : "false"; },
     [](AppChannels*, meshtastic_Channel& ch) { ch.settings.uplink_enabled = !ch.settings.uplink_enabled; }},
    {"Downlink enabled",
     "If true, messages seen on the internet will be forwarded to the local mesh",
     TFT_CYAN,
     [](const meshtastic_Channel& ch) { return ch.settings.downlink_enabled ? "true" : "false"; },
     [](AppChannels*, meshtastic_Channel& ch) { ch.settings.downlink_enabled = !ch.settings.downlink_enabled; }},
#endif
    {"Position precision",
     "Bits of precision for the location sent in position packets. 0 = no location, 1 = 1 bit, 32 = 32 bits",
     TFT_CYAN,
     [](AppChannels*, const meshtastic_Channel& ch) { return std::to_string(ch.settings.module_settings.position_precision); },
     [](AppChannels* app, meshtastic_Channel& ch)
     {
         int val = (int)ch.settings.module_settings.position_precision;
         if (UTILS::UI::show_edit_number_dialog(app->get_hal(), "Position precision", val, 0, 32))
         {
             ch.settings.has_module_settings = true;
             ch.settings.module_settings.position_precision = (uint32_t)val;
         }
     }},
    {"Greeting (ch)",
     "Broadcast greeting on this channel when new node appears. "
     "Macros: #short #long #id #hops #rssi #snr. Hold [Fn] for custom text. Max 200 chars",
     TFT_CYAN,
     [](AppChannels* app, const meshtastic_Channel&) -> std::string
     {
         auto& g = app->getEditGreeting();
         return g.channel_text[0] ? std::string(g.channel_text) : "Off";
     },
     [](AppChannels* app, meshtastic_Channel&)
     {
         auto& g = app->getEditGreeting();
         std::string text(g.channel_text);
         app->get_hal()->keyboard()->updateKeyList();
         app->get_hal()->keyboard()->updateKeysState();
         if (app->get_hal()->keyboard()->keysState().fn)
         {
             if (UTILS::UI::show_edit_string_dialog(app->get_hal(), "Greeting on channel:", text, false, 200))
             {
                 memset(g.channel_text, 0, sizeof(g.channel_text));
                 strncpy(g.channel_text, text.c_str(), sizeof(g.channel_text) - 1);
             }
         }
         else
         {
             int cur = 0;
             for (size_t i = 0; i < PREDEF_GREETING_CH.size(); i++)
             {
                 if (PREDEF_GREETING_CH[i] == text || (PREDEF_GREETING_CH[i] == "Off" && text.empty()))
                 {
                     cur = (int)i;
                     break;
                 }
             }
             int sel = UTILS::UI::show_select_dialog(app->get_hal(), "Greeting on channel:", PREDEF_GREETING_CH, cur);
             if (sel >= 0)
             {
                 memset(g.channel_text, 0, sizeof(g.channel_text));
                 strncpy(g.channel_text,
                         (PREDEF_GREETING_CH[sel] == "Off" ? "" : PREDEF_GREETING_CH[sel].c_str()),
                         sizeof(g.channel_text) - 1);
             }
         }
     }},
    {"Greeting (DM)",
     "Direct message sent to a new node when it appears on this channel. "
     "Macros: #short #long #id #hops #rssi #snr. Hold [Fn] for custom text. Max 200 chars",
     TFT_CYAN,
     [](AppChannels* app, const meshtastic_Channel&) -> std::string
     {
         auto& g = app->getEditGreeting();
         return g.dm_text[0] ? std::string(g.dm_text) : "Off";
     },
     [](AppChannels* app, meshtastic_Channel&)
     {
         auto& g = app->getEditGreeting();
         std::string text(g.dm_text);
         app->get_hal()->keyboard()->updateKeyList();
         app->get_hal()->keyboard()->updateKeysState();
         if (app->get_hal()->keyboard()->keysState().fn)
         {
             if (UTILS::UI::show_edit_string_dialog(app->get_hal(), "Greeting on DM:", text, false, 200))
             {
                 memset(g.dm_text, 0, sizeof(g.dm_text));
                 strncpy(g.dm_text, text.c_str(), sizeof(g.dm_text) - 1);
             }
         }
         else
         {
             int cur = 0;
             for (size_t i = 0; i < PREDEF_GREETING_DM.size(); i++)
             {
                 if (PREDEF_GREETING_DM[i] == text || (PREDEF_GREETING_DM[i] == "Off" && text.empty()))
                 {
                     cur = (int)i;
                     break;
                 }
             }
             int sel = UTILS::UI::show_select_dialog(app->get_hal(), "Greeting on DM:", PREDEF_GREETING_DM, cur);
             if (sel >= 0)
             {
                 memset(g.dm_text, 0, sizeof(g.dm_text));
                 strncpy(g.dm_text,
                         (PREDEF_GREETING_DM[sel] == "Off" ? "" : PREDEF_GREETING_DM[sel].c_str()),
                         sizeof(g.dm_text) - 1);
             }
         }
     }},
    {"Ping reply",
     "Auto-reply when #ping is found in a channel message. "
     "Macros: #short #long #id #hops #rssi #snr. [Fn]=custom. Max 200 chars",
     TFT_CYAN,
     [](AppChannels* app, const meshtastic_Channel&) -> std::string
     {
         auto& g = app->getEditGreeting();
         return g.ping_text[0] ? std::string(g.ping_text) : "Off";
     },
     [](AppChannels* app, meshtastic_Channel&)
     {
         auto& g = app->getEditGreeting();
         std::string text(g.ping_text);
         app->get_hal()->keyboard()->updateKeyList();
         app->get_hal()->keyboard()->updateKeysState();
         if (app->get_hal()->keyboard()->keysState().fn)
         {
             if (UTILS::UI::show_edit_string_dialog(app->get_hal(), "Ping reply:", text, false, 199))
             {
                 memset(g.ping_text, 0, sizeof(g.ping_text));
                 strncpy(g.ping_text, text.c_str(), sizeof(g.ping_text) - 1);
             }
         }
         else
         {
             int cur = 0;
             for (size_t i = 0; i < PREDEF_PING_REPLY.size(); i++)
             {
                 if (PREDEF_PING_REPLY[i] == text || (PREDEF_PING_REPLY[i] == "Off" && text.empty()))
                 {
                     cur = (int)i;
                     break;
                 }
             }
             int sel = UTILS::UI::show_select_dialog(app->get_hal(), "Ping reply:", PREDEF_PING_REPLY, cur);
             if (sel >= 0)
             {
                 memset(g.ping_text, 0, sizeof(g.ping_text));
                 strncpy(g.ping_text,
                         (PREDEF_PING_REPLY[sel] == "Off" ? "" : PREDEF_PING_REPLY[sel].c_str()),
                         sizeof(g.ping_text) - 1);
             }
         }
     }},
    {"Save", "Save channel settings to device", TFT_ORANGE, nullptr, nullptr},
};

void AppChannels::_enter_channel_edit(uint8_t channel_index, bool is_new)
{
    if (!_data.hal->nodedb())
        return;

    if (is_new)
    {
        _data.edit_channel = meshtastic_Channel_init_default;
        _data.edit_channel.index = channel_index;
        _data.edit_channel.has_settings = true;
        _data.edit_channel.role = meshtastic_Channel_Role_SECONDARY;
        psk_from_key_type("Default", _data.edit_channel.settings.psk);
    }
    else
    {
        auto* ch = _data.hal->nodedb()->getChannel(channel_index);
        if (!ch || ch->role == meshtastic_Channel_Role_DISABLED)
            return;
        _data.edit_channel = *ch;
    }

    _data.edit_channel_index = channel_index;
    _data.edit_is_new = is_new;
    _data.edit_greeting = is_new ? Mesh::ChannelGreeting{} : _data.hal->nodedb()->getGreeting(channel_index);
    _data.edit_selected = 0;
    _data.edit_scroll = 0;
    _data.view_state = ViewState::CHANNEL_EDIT;
    _data.update_list = true;
    UTILS::SCROLL_TEXT::scroll_text_reset(&_data.desc_scroll_ctx);
}

void AppChannels::_save_channel_edit()
{
    auto& channel = _data.edit_channel;
    uint8_t channel_index = _data.edit_channel_index;

    if (channel.settings.name[0] == '\0')
    {
        UTILS::UI::show_error_dialog(_data.hal, "Error", "Channel name is required");
        _data.update_list = true;
        return;
    }

    _data.hal->nodedb()->setChannel(channel_index, channel);
    // if the channel is primary, set all other primary channels to secondary
    if (channel.role == meshtastic_Channel_Role_PRIMARY)
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            if (i == channel_index)
                continue;
            auto* ch = _data.hal->nodedb()->getChannel(i);
            if (ch && ch->role == meshtastic_Channel_Role_PRIMARY)
            {
                ch->role = meshtastic_Channel_Role_SECONDARY;
                _data.hal->nodedb()->setChannel(i, *ch);
            }
        }
    }
    else
    {
        // if the channel is secondary, make sure atleast one primary channel is set
        bool has_primary = false;
        for (uint8_t i = 0; i < 8; i++)
        {
            if (i == channel_index)
                continue;
            auto* ch = _data.hal->nodedb()->getChannel(i);
            if (ch && ch->role == meshtastic_Channel_Role_PRIMARY)
            {
                has_primary = true;
                break;
            }
        }
        if (!has_primary)
        {
            // set ch.0 as primary
            auto* ch = _data.hal->nodedb()->getChannel(0);
            if (ch)
            {
                ch->role = meshtastic_Channel_Role_PRIMARY;
                _data.hal->nodedb()->setChannel(0, *ch);
            }
        }
    }

    _data.hal->nodedb()->saveChannels();

    _data.hal->nodedb()->setGreeting(channel_index, _data.edit_greeting);
    _data.hal->nodedb()->saveGreetings();

    if (channel.role == meshtastic_Channel_Role_PRIMARY && _data.hal->mesh())
    {
        //
        auto cfg = _data.hal->mesh()->getConfig();
        cfg.primary_channel = channel;
        _data.hal->mesh()->setConfig(cfg);
    }

    _data.view_state = ViewState::CHANNEL_LIST;
    _refresh_channels();
    _data.update_list = true;
}

// ─── Rendering ──────────────────────────────────────────────────────────────────

bool AppChannels::_render_channel_list()
{
    if (!_data.update_list)
        return false;
    _data.update_list = false;

    auto* canvas = _data.hal->canvas();
    int panel_x = 0;
    int panel_width = canvas->width();
    canvas->fillRect(panel_x, 0, panel_width, canvas->height(), THEME_COLOR_BG);
    canvas->setFont(FONT_12);

    if (_data.channels.empty())
    {
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawCenterString("<no channels>",
                                 panel_x + panel_width / 2,
                                 LIST_HEADER_HEIGHT + (LIST_MAX_VISIBLE_ITEMS / 2) * (LIST_ITEM_HEIGHT + 1));
        return true;
    }

    int y_offset = LIST_HEADER_HEIGHT;
    int items_drawn = 0;
    int total_channels = (int)_data.channels.size();

    for (int i = _data.scroll_offset; i < total_channels && items_drawn < LIST_MAX_VISIBLE_ITEMS; i++, items_drawn++)
    {
        const auto& ch = _data.channels[i];
        bool is_selected = (i == _data.selected_index);

        // Selection highlight
        if (is_selected)
        {
            canvas->fillRect(panel_x + 2,
                             y_offset + 1,
                             panel_width - 2 - SCROLL_BAR_WIDTH - 1,
                             LIST_ITEM_HEIGHT,
                             THEME_COLOR_BG_SELECTED);
        }

        const auto& mc = ch.channel;
        bool is_primary = (mc.role == meshtastic_Channel_Role_PRIMARY);

        // Channel index badge with color
        uint32_t ch_color = is_primary ? TFT_ORANGE : TFT_CYAN;
        int badge_width = 14;
        canvas->fillRoundRect(LIST_ITEM_LEFT_PADDING, y_offset, badge_width, LIST_ITEM_HEIGHT, 4, ch_color);
        canvas->setTextColor(TFT_BLACK, ch_color);
        canvas->drawCenterString(std::format("{}", mc.index).c_str(),
                                 LIST_ITEM_LEFT_PADDING + badge_width / 2 - 1,
                                 y_offset + 1);

        // Lock icon for PSK channels
        int name_x = LIST_ITEM_LEFT_PADDING + badge_width + 4;
        uint16_t* key_image_data = (uint16_t*)image_data_key_shared;
        if (mc.settings.psk.size > 0)
        {
            if (mc.settings.psk.size > 1)
                key_image_data = (uint16_t*)image_data_key_pke;
            else if (mc.settings.has_module_settings && mc.settings.module_settings.position_precision > 0)
                key_image_data = (uint16_t*)image_data_key_mismatch;
        }
        canvas->pushImage(name_x, y_offset + 1, 12, 12, key_image_data, TFT_WHITE);
        name_x += 14;

        // Channel name
        canvas->setTextColor(is_selected ? THEME_COLOR_SELECTED : THEME_COLOR_UNSELECTED,
                             is_selected ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG);
        canvas->drawString(mc.settings.name, name_x, y_offset + 1);

        // Channel hash
        if (_data.hal->mesh())
        {
            uint8_t ch_hash = _data.hal->mesh()->getChannelHash(mc.settings);
            canvas->setTextColor(is_selected ? THEME_COLOR_SELECTED : THEME_COLOR_CHANNEL_HASH,
                                 is_selected ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG);
            int name_width = canvas->textWidth(mc.settings.name);
            canvas->drawString(std::format("#{:02X}", ch_hash).c_str(), name_x + name_width + 4, y_offset + 1);
        }

        // Right-side indicators area (before scrollbar)
        int right_x = panel_x + panel_width - SCROLL_BAR_WIDTH - 2;

        // Unread count badge
        if (ch.unread_count > 0)
        {
            char ticker_str[8];
            snprintf(ticker_str, sizeof(ticker_str), "+%d", ch.unread_count > 99 ? 99 : (int)ch.unread_count);
            int ticker_width = strlen(ticker_str) * 6 + 6;
            right_x -= ticker_width + 1;
            canvas->fillRoundRect(right_x, y_offset + 1, ticker_width, LIST_ITEM_HEIGHT, 3, TFT_RED);
            canvas->setTextColor(is_selected ? THEME_COLOR_SELECTED : THEME_COLOR_UNSELECTED, TFT_RED);
            canvas->drawRightString(ticker_str, right_x + ticker_width - 4, y_offset + 1);
        }

        // Notification off indicator
        if (mc.settings.channel_num == 0)
        {
            right_x -= 12;
            canvas->pushImage(right_x, y_offset + 1, 12, 12, image_data_sound_off, TFT_WHITE);
        }

        y_offset += LIST_ITEM_HEIGHT + 1;
    }

    UTILS::UI::draw_scrollbar(canvas,
                              panel_x + panel_width - SCROLL_BAR_WIDTH - 1,
                              LIST_HEADER_HEIGHT,
                              SCROLL_BAR_WIDTH,
                              (LIST_ITEM_HEIGHT + 1) * LIST_MAX_VISIBLE_ITEMS,
                              total_channels,
                              LIST_MAX_VISIBLE_ITEMS,
                              _data.scroll_offset,
                              SCROLLBAR_MIN_HEIGHT);

    return true;
}

bool AppChannels::_render_channel_chat()
{
    if (!_data.update_list)
        return false;
    _data.update_list = false;

    auto* canvas = _data.hal->canvas();
    canvas->fillScreen(THEME_COLOR_BG);
    canvas->setFont(FONT_12);

    const auto& mc = _data.channels[_data.selected_index].channel;
    _draw_channel_header(canvas,
                         mc,
                         nullptr,
                         (const uint16_t*)image_data_chat,
                         std::format("{}", _data.chat_msg_count).c_str());

    // Messages area
    const int messages_area_top = CHAT_HEADER_HEIGHT;
    const int messages_area_height = canvas->height() - CHAT_HEADER_HEIGHT - CHAT_FOOTER_HEIGHT;
    const int max_visible = messages_area_height / (CHAT_ITEM_HEIGHT + 1);

    const int name_col_width = 4 * 6 + 6;
    const int text_start_x = name_col_width + 2;

    if (_data.chat_msg_count == 0 || _data.chat_line_counts.empty())
    {
        canvas->setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas->drawCenterString("<no messages yet>", canvas->width() / 2, canvas->height() / 2);
    }
    else
    {
        // Determine which messages overlap the visible line window
        int line_acc = 0;
        uint32_t first_msg = 0;

        for (uint32_t i = 0; i < _data.chat_msg_count; i++)
        {
            int msg_lines = _data.chat_line_counts[i];
            if (line_acc + msg_lines > _data.chat_cur_line)
            {
                first_msg = i;
                break;
            }
            line_acc += msg_lines;
        }

        int visible_end = _data.chat_cur_line + max_visible;
        uint32_t last_msg = first_msg;
        int running = line_acc;
        for (uint32_t i = first_msg; i < _data.chat_msg_count; i++)
        {
            last_msg = i;
            running += _data.chat_line_counts[i];
            if (running >= visible_end)
                break;
        }

        // Load only the needed messages from file
        uint32_t range_count = last_msg - first_msg + 1;
        std::vector<Mesh::TextMessage> visible_msgs;
        visible_msgs.reserve(range_count);
        auto& store = Mesh::MeshDataStore::getInstance();
        store.getChannelMessageRange(_data.selected_channel, first_msg, range_count, visible_msgs);

        uint32_t our_id = _data.hal->mesh() ? _data.hal->mesh()->getNodeId() : 0;
        int y = messages_area_top;
        int current_line = line_acc;

        for (uint32_t mi = 0; mi < visible_msgs.size(); mi++)
        {
            const auto& msg = visible_msgs[mi];
            auto wrapped = wrap_text(msg.text, _data.chat_chars_per_line);

            bool is_ours = (msg.from == our_id);
            uint32_t sender_bg = _get_node_color(msg.from);
            uint32_t sender_fg = _get_node_text_color(msg.from);
            std::string sender_name = is_ours ? "Me" : _get_sender_name(msg.from);

            for (size_t line_idx = 0; line_idx < wrapped.size(); line_idx++)
            {
                if (current_line < _data.chat_cur_line)
                {
                    current_line++;
                    continue;
                }
                if (current_line >= _data.chat_cur_line + max_visible)
                    break;
                // Draw message text line
                canvas->setTextColor(TFT_WHITE, THEME_COLOR_BG);
                canvas->drawString(wrapped[line_idx].c_str(), text_start_x, y + 1);

                // Draw sender name tag only on first line of message
                if (line_idx == 0)
                {
                    canvas->setTextColor(sender_fg, sender_bg);
                    if (_data.chat_ctrl && msg.timestamp > 0)
                    {
                        std::string dt = sender_name + " \u2192 " + UTILS::TEXT::format_timestamp(msg.timestamp);
                        canvas->fillRoundRect(2, y + 1, canvas->textWidth(dt.c_str()) + 6, CHAT_ITEM_HEIGHT, 3, sender_bg);
                        canvas->drawString(dt.c_str(), 2 + 3, y + 1);
                    }
                    else
                    {
                        canvas->fillRoundRect(2, y + 1, name_col_width, CHAT_ITEM_HEIGHT, 3, sender_bg);
                        if (is_ours)
                        {
                            canvas->drawString(sender_name.c_str(), 2 + 3, y + 1);
                        }
                        else
                        {
                            canvas->drawCenterString(sender_name.c_str(), 2 + name_col_width / 2, y + 1);
                        }

                        // Delivery status indicator for our messages
                        if (is_ours)
                        {
                            auto si = UTILS::UI::message_status_info((int)msg.status);
                            canvas->setFont(FONT_6);
                            canvas->setTextColor(si.color, sender_bg);
                            canvas->drawRightString(si.icon, name_col_width + 3, y + 2);
                            // canvas->fillRect(name_col_width + 1, y + 1, 8, CHAT_ITEM_HEIGHT, THEME_COLOR_BG);
                            canvas->setFont(FONT_12);
                        }
                    }
                }

                y += CHAT_ITEM_HEIGHT + 1;
                current_line++;
            }
        }

        // Draw scroll bar if needed
        UTILS::UI::draw_scrollbar(canvas,
                                  canvas->width() - SCROLL_BAR_WIDTH - 1,
                                  messages_area_top,
                                  SCROLL_BAR_WIDTH,
                                  messages_area_height,
                                  _data.chat_total_lines,
                                  max_visible,
                                  _data.chat_cur_line,
                                  SCROLLBAR_MIN_HEIGHT);
    }
    return true;
}

bool AppChannels::_render_list_hint()
{
    static bool last_fn = false;
    auto c = _data.hal->canvas();
    auto keys_state = _data.hal->keyboard()->keysState();
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

bool AppChannels::_render_chat_hint()
{
    static bool last_fn = false;
    auto c = _data.hal->canvas();
    auto keys_state = _data.hal->keyboard()->keysState();
    if (last_fn != keys_state.fn)
    {
        last_fn = keys_state.fn;
        c->fillRect(0, c->height() - 9, c->width(), 10, THEME_COLOR_BG);
    }
    return hl_text_render(&_data.hint_hl_ctx,
                          last_fn ? HINT_CHAT_FN : HINT_CHAT,
                          0,
                          _data.hal->canvas()->height() - 9,
                          TFT_DARKGREY,
                          TFT_WHITE,
                          THEME_COLOR_BG);
}

bool AppChannels::_render_channel_edit()
{
    if (!_data.update_list)
        return false;
    _data.update_list = false;

    auto* canvas = _data.hal->canvas();
    auto& ch = _data.edit_channel;
    const auto& items = EDIT_ITEMS;
    const int item_count = (int)items.size();

    const int HEADER_H = CHAT_HEADER_HEIGHT;
    const int FOOTER_H = 9;
    const int ITEM_H = 14;
    const int max_visible = (canvas->height() - HEADER_H - FOOTER_H) / (ITEM_H + 1);

    canvas->fillScreen(THEME_COLOR_BG);
    canvas->setFont(FONT_12);

    const char* title = _data.edit_is_new ? "New channel" : ch.settings.name;
    _draw_channel_header(canvas, ch, title, (const uint16_t*)image_data_edit);

    int y = HEADER_H;
    int max_width = canvas->width() - 4;

    for (int i = _data.edit_scroll; i < item_count && (i - _data.edit_scroll) < max_visible; i++)
    {
        const auto& item = items[i];
        bool is_sel = (i == _data.edit_selected);

        if (is_sel)
            canvas->fillRect(2, y + 1, max_width - 2, ITEM_H, THEME_COLOR_BG_SELECTED);

        canvas->setTextColor(is_sel ? THEME_COLOR_SELECTED : lgfx::v1::convert_to_rgb888(item.label_color),
                             is_sel ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG);
        canvas->drawString(item.label, 4, y + 1);

        if (item.get_value)
        {
            std::string val = item.get_value(this, ch);
            if (!val.empty())
            {
                int label_w = canvas->textWidth(item.label) + 10;
                int avail_w = canvas->width() - 6 - label_w;
                int val_w = canvas->textWidth(val.c_str());
                if (val_w > avail_w && avail_w > 0)
                {
                    int char_w = canvas->textWidth("0");
                    if (char_w > 0)
                    {
                        int max_chars = avail_w / char_w;
                        if (max_chars > 1 && (int)val.length() > max_chars)
                            val = val.substr(0, max_chars - 1) + ">";
                    }
                }
                canvas->setTextColor(is_sel ? THEME_COLOR_SELECTED : lgfx::v1::convert_to_rgb888(TFT_WHITE),
                                     is_sel ? THEME_COLOR_BG_SELECTED : THEME_COLOR_BG);
                canvas->drawRightString(val.c_str(), canvas->width() - 6, y + 1);
            }
        }

        y += ITEM_H + 1;
    }

    // Scrollbar
    if (item_count > max_visible)
    {
        int sb_x = canvas->width() - 4;
        int sb_h = (ITEM_H + 1) * max_visible;
        int thumb_h = std::max(8, sb_h * max_visible / item_count);
        int thumb_y = HEADER_H + (sb_h - thumb_h) * _data.edit_scroll / std::max(1, item_count - max_visible);
        canvas->drawRect(sb_x, HEADER_H, 3, sb_h, TFT_DARKGREY);
        canvas->fillRect(sb_x, thumb_y, 3, thumb_h, TFT_ORANGE);
    }

    return true;
}

bool AppChannels::_render_edit_hint()
{
    static bool last_fn = false;
    auto c = _data.hal->canvas();
    auto keys_state = _data.hal->keyboard()->keysState();
    if (last_fn != keys_state.fn)
    {
        last_fn = keys_state.fn;
        c->fillRect(0, c->height() - 9, c->width(), 10, THEME_COLOR_BG);
    }
    return hl_text_render(&_data.hint_hl_ctx,
                          last_fn ? HINT_EDIT_FN : HINT_EDIT,
                          0,
                          _data.hal->canvas()->height() - 9,
                          TFT_DARKGREY,
                          TFT_WHITE,
                          THEME_COLOR_BG);
}

bool AppChannels::_render_edit_desc()
{
    if (millis() - _data.hal->keyboard()->lastPressedTime() < 3000)
        return false;

    const auto& items = EDIT_ITEMS;
    if (_data.edit_selected < 0 || _data.edit_selected >= (int)items.size())
        return false;

    const char* desc = items[_data.edit_selected].description;
    if (!desc || desc[0] == '\0')
        return false;

    return UTILS::SCROLL_TEXT::scroll_text_render(&_data.desc_scroll_ctx,
                                                  desc,
                                                  0,
                                                  0,
                                                  lgfx::v1::convert_to_rgb888(TFT_DARKGREY),
                                                  THEME_COLOR_BG);
}

// ─── Input handling ─────────────────────────────────────────────────────────────

void AppChannels::_handle_channel_list_input()
{
    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    if (_data.hal->keyboard()->isPressed())
    {
        uint32_t now = millis();
        auto keys_state = _data.hal->keyboard()->keysState();

        if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ESC) || _data.hal->home_button()->is_pressed())
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ESC);
            destroyApp();
            return;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                if (_data.selected_index < (int)_data.channels.size() - 1)
                {
                    _data.selected_index++;
                    if (_data.selected_index >= _data.scroll_offset + LIST_MAX_VISIBLE_ITEMS)
                    {
                        _data.scroll_offset++;
                    }
                    _data.hal->playNextSound();
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_UP))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                if (_data.selected_index > 0)
                {
                    _data.selected_index--;
                    if (_data.selected_index < _data.scroll_offset)
                    {
                        _data.scroll_offset--;
                    }
                    _data.hal->playNextSound();
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                int max_index = (int)_data.channels.size() - 1;
                if (max_index > 0 && _data.selected_index < max_index)
                {
                    _data.hal->playNextSound();
                    _data.selected_index = std::min(_data.selected_index + LIST_MAX_VISIBLE_ITEMS, max_index);
                    _data.scroll_offset = std::max(0, _data.selected_index - LIST_MAX_VISIBLE_ITEMS + 1);
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                if (_data.selected_index > 0)
                {
                    _data.hal->playNextSound();
                    _data.selected_index = std::max(0, _data.selected_index - LIST_MAX_VISIBLE_ITEMS);
                    _data.scroll_offset = std::max(0, _data.selected_index);
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ENTER);

            if (!_data.channels.empty())
            {
                // Fn pressed
                if (keys_state.fn)
                {
                    _enter_channel_edit(_data.channels[_data.selected_index].channel.index, false);
                }
                else
                {
                    _data.selected_channel = _data.channels[_data.selected_index].channel.index;
                    _data.selected_channel_name = _data.channels[_data.selected_index].channel.settings.name;
                    _data.chat_cur_line = 0;
                    _refresh_messages();
                    // Auto-scroll to bottom
                    auto* canvas = _data.hal->canvas();
                    int max_visible = (canvas->height() - CHAT_HEADER_HEIGHT - CHAT_FOOTER_HEIGHT) / (CHAT_ITEM_HEIGHT + 1);
                    _data.chat_cur_line = _data.chat_total_lines > max_visible ? _data.chat_total_lines - max_visible : 0;
                    _data.view_state = ViewState::CHANNEL_CHAT;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_SPACE))
        {
            if (keys_state.fn)
            {
                _data.hal->playNextSound();
                _data.hal->keyboard()->waitForRelease(KEY_NUM_SPACE);
                if (_data.hal->nodedb())
                {
                    uint8_t free_slot = 0xFF;
                    for (uint8_t i = 0; i < 8; i++)
                    {
                        auto* ch = _data.hal->nodedb()->getChannel(i);
                        if (!ch || ch->role == meshtastic_Channel_Role_DISABLED)
                        {
                            free_slot = i;
                            break;
                        }
                    }
                    if (free_slot != 0xFF)
                    {
                        _enter_channel_edit(free_slot, true);
                    }
                    else
                    {
                        UTILS::UI::show_error_dialog(_data.hal, "Error", "All 8 channels in use");
                        _data.update_list = true;
                    }
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_BACKSPACE);

            if (!_data.channels.empty() && _data.hal->nodedb())
            {
                auto& sel = _data.channels[_data.selected_index];
                if (sel.channel.role == meshtastic_Channel_Role_PRIMARY)
                {
                    UTILS::UI::show_error_dialog(_data.hal, "Error", "Cannot delete primary channel");
                }
                else if (UTILS::UI::show_confirmation_dialog(_data.hal,
                                                             sel.channel.settings.name,
                                                             "Delete channel and all messages?",
                                                             "Delete",
                                                             "Cancel"))
                {
                    uint8_t ch_idx = sel.channel.index;
                    _data.hal->nodedb()->deleteChannel(ch_idx);
                    Mesh::MeshDataStore::getInstance().clearConversation(ch_idx, true);
                    _data.hal->nodedb()->saveChannels();
                    _data.hal->nodedb()->saveGreetings();
                    _refresh_channels();
                    if (_data.selected_index >= (int)_data.channels.size())
                        _data.selected_index = _data.channels.empty() ? 0 : (int)_data.channels.size() - 1;
                    if (_data.selected_index < _data.scroll_offset)
                        _data.scroll_offset = _data.selected_index;
                }
            }
            _data.update_list = true;
        }
    }
    else
    {
        is_repeat = false;
    }
}

void AppChannels::_handle_channel_chat_input()
{
    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    auto keys_state = _data.hal->keyboard()->keysState();
    if (_data.chat_ctrl != keys_state.ctrl)
    {
        _data.chat_ctrl = keys_state.ctrl;
        _data.update_list = true;
    }

    if (_data.hal->keyboard()->isPressed())
    {
        uint32_t now = millis();

        if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ESC))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ESC);
            _data.update_list = true;
            _data.view_state = ViewState::CHANNEL_LIST;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_BACKSPACE);
            // Clear chat confirmation
            if (UTILS::UI::show_confirmation_dialog(_data.hal, _data.selected_channel_name, "Clear chat?", "Clear", "Cancel"))
            {
                Mesh::MeshDataStore::getInstance().clearConversation(_data.selected_channel, true);
            }
            _data.update_list = true;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ENTER);

            std::string message_text;
            std::string title = std::format("Message to: #{}", _data.selected_channel_name);
            if (UTILS::UI::show_edit_string_dialog(_data.hal, title, message_text, false, 200))
            {
                if (!message_text.empty())
                {
                    _send_message(message_text);
                    // Scroll to bottom to show the just-sent message
                    auto* canvas = _data.hal->canvas();
                    int max_visible = (canvas->height() - CHAT_HEADER_HEIGHT - CHAT_FOOTER_HEIGHT) / (CHAT_ITEM_HEIGHT + 1);
                    _data.chat_cur_line = _data.chat_total_lines > max_visible ? _data.chat_total_lines - max_visible : 0;
                }
            }
            _data.update_list = true;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_UP))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                if (_data.chat_cur_line > 0)
                {
                    if (keys_state.fn)
                        _data.chat_cur_line = 0;
                    else
                        _data.chat_cur_line--;
                    _data.hal->playNextSound();
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                int area_bottom = _data.hal->canvas()->height() - CHAT_HEADER_HEIGHT - 1;
                int max_visible = (area_bottom - CHAT_HEADER_HEIGHT) / CHAT_ITEM_HEIGHT;
                int max_line = _data.chat_total_lines - max_visible;
                if (_data.chat_cur_line < max_line)
                {
                    if (keys_state.fn)
                        _data.chat_cur_line = max_line;
                    else
                        _data.chat_cur_line++;
                    _data.hal->playNextSound();
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                int area_bottom = _data.hal->canvas()->height() - CHAT_HEADER_HEIGHT - 1;
                int page_size = (area_bottom - CHAT_HEADER_HEIGHT) / CHAT_ITEM_HEIGHT;
                if (_data.chat_cur_line > 0)
                {
                    _data.hal->playNextSound();
                    _data.chat_cur_line = std::max(0, _data.chat_cur_line - page_size);
                    _data.update_list = true;
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                int area_bottom = _data.hal->canvas()->height() - CHAT_HEADER_HEIGHT - 1;
                int page_size = (area_bottom - CHAT_HEADER_HEIGHT) / CHAT_ITEM_HEIGHT;
                int max_scroll = _data.chat_total_lines - page_size;
                if (max_scroll > 0 && _data.chat_cur_line < max_scroll)
                {
                    _data.chat_cur_line = std::min(_data.chat_cur_line + page_size, max_scroll);
                    _data.hal->playNextSound();
                    _data.update_list = true;
                }
            }
        }
    }
    else
    {
        is_repeat = false;
    }
}

void AppChannels::_handle_channel_edit_input()
{
    const auto& items = EDIT_ITEMS;
    const int item_count = (int)items.size();

    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    if (_data.hal->keyboard()->isPressed())
    {
        uint32_t now = millis();
        auto keys_state = _data.hal->keyboard()->keysState();

        if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ESC) || _data.hal->keyboard()->isKeyPressing(KEY_NUM_BACKSPACE))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(_data.hal->keyboard()->isKeyPressing(KEY_NUM_ESC) ? KEY_NUM_ESC
                                                                                                    : KEY_NUM_BACKSPACE);
            _data.view_state = ViewState::CHANNEL_LIST;
            _data.update_list = true;
            return;
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_DOWN))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.edit_selected < item_count - 1)
            {
                _data.edit_selected++;
                auto* canvas = _data.hal->canvas();
                int max_visible = (canvas->height() - 14 - 9) / (14 + 1);
                if (_data.edit_selected >= _data.edit_scroll + max_visible)
                    _data.edit_scroll = _data.edit_selected - max_visible + 1;
                _data.hal->playNextSound();
                _data.update_list = true;
                UTILS::SCROLL_TEXT::scroll_text_reset(&_data.desc_scroll_ctx);
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_UP))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now) && _data.edit_selected > 0)
            {
                _data.edit_selected--;
                if (_data.edit_selected < _data.edit_scroll)
                    _data.edit_scroll = _data.edit_selected;
                _data.hal->playNextSound();
                _data.update_list = true;
                UTILS::SCROLL_TEXT::scroll_text_reset(&_data.desc_scroll_ctx);
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_LEFT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                int prev = _data.edit_selected;
                auto* canvas = _data.hal->canvas();
                int max_visible = (canvas->height() - 14 - 9) / (14 + 1);
                _data.edit_selected -= max_visible;
                if (_data.edit_selected < 0)
                    _data.edit_selected = 0;
                if (_data.edit_selected != prev)
                {
                    if (_data.edit_selected < _data.edit_scroll)
                        _data.edit_scroll = _data.edit_selected;
                    _data.hal->playNextSound();
                    _data.update_list = true;
                    UTILS::SCROLL_TEXT::scroll_text_reset(&_data.desc_scroll_ctx);
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_RIGHT))
        {
            if (key_repeat_check(is_repeat, next_fire_ts, now))
            {
                int prev = _data.edit_selected;
                auto* canvas = _data.hal->canvas();
                int max_visible = (canvas->height() - 14 - 9) / (14 + 1);
                _data.edit_selected += max_visible;
                if (_data.edit_selected >= item_count)
                    _data.edit_selected = item_count - 1;
                if (_data.edit_selected != prev)
                {
                    if (_data.edit_selected >= _data.edit_scroll + max_visible)
                        _data.edit_scroll = _data.edit_selected - max_visible + 1;
                    _data.hal->playNextSound();
                    _data.update_list = true;
                    UTILS::SCROLL_TEXT::scroll_text_reset(&_data.desc_scroll_ctx);
                }
            }
        }
        else if (_data.hal->keyboard()->isKeyPressing(KEY_NUM_ENTER))
        {
            _data.hal->playNextSound();
            _data.hal->keyboard()->waitForRelease(KEY_NUM_ENTER);

            const auto& item = items[_data.edit_selected];
            if (item.on_enter)
            {
                item.on_enter(this, _data.edit_channel);
                _data.update_list = true;
                UTILS::SCROLL_TEXT::scroll_text_reset(&_data.desc_scroll_ctx);
            }
            else
            {
                _save_channel_edit();
            }
        }
    }
    else
    {
        is_repeat = false;
    }
}
