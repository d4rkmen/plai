/**
 * @file settings.cpp
 * @brief Settings management system implementation
 */

#include "settings.h"
#include "hal/hal.h"
#include "apps/utils/ui/dialog.h"
#include "mesh/mesh_service.h"
#include "mesh/node_db.h"
#include "mbedtls/base64.h"
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <map>
#include <algorithm>

static const char* TAG = "SETTINGS";
static const std::string SETTINGS_FILE_NAME = "/sdcard/settings.txt";

namespace SETTINGS
{
    // partition names in order of priority
    const char* const Settings::NVS_PARTITIONS[] = {"nvs", "apps_nvs", nullptr};

    Settings::Settings() : _initialized(false)
    {
        // back item
        SettingItem_t back_item = {"", "[..]", TYPE_NONE, "back", "back", "", "", "Return back to the parent menu"};

        SettingGroup_t sys_group;
        sys_group.name = "System settings";
        sys_group.nvs_namespace = "system";
        sys_group.items = {
            back_item,
            {"brightness", "Brightness", TYPE_NUMBER, "100", "100", "10", "255", "Screen brightness level (10-255)"},
            {"volume", "Volume", TYPE_NUMBER, "64", "64", "0", "255", "System sound volume level (0-255)"},
            {"dim_time", "Dim seconds", TYPE_NUMBER, "30", "30", "0", "3600", "Screen dimming time in seconds (0-3600)"},
            {"boot_sound", "Boot sound", TYPE_BOOL, "true", "true", "", "", "Play boot sound on startup"},
            {"show_bat_volt", "Battery voltage", TYPE_BOOL, "true", "true", "", "", "Show battery voltage on the system bar"},
            {"show_time", "Show time", TYPE_BOOL, "true", "true", "", "", "Show time on the system bar"},
            {"timezone",
             "Timezone",
             TYPE_STRING,
             "GMT+2",
             "GMT+2",
             "GMT-12;GMT-11;GMT-10;GMT-9:30;GMT-9;GMT-8;GMT-7;GMT-6;GMT-5;GMT-4;GMT-3:30;GMT-3;GMT-2;GMT-1;"
             "GMT+0;GMT+1;GMT+2;GMT+3;GMT+3:30;GMT+4;GMT+4:30;GMT+5;GMT+5:30;GMT+5:45;"
             "GMT+6;GMT+6:30;GMT+7;GMT+8;GMT+8:45;GMT+9;GMT+9:30;GMT+10;GMT+10:30;GMT+11;GMT+12;GMT+13;GMT+14",
             "",
             "Timezone offset from GMT"},
        };

        auto mesh_apply_cb = [this](SettingItem_t& item) { applyMeshConfig(item); };
        auto nodeinfo_apply_cb = [this](SettingItem_t& item)
        {
            applyMeshConfig(item);
            if (_hal && _hal->mesh())
                _hal->mesh()->forceNodeInfoBroadcast();
        };
        // LoRa settings
        SettingGroup_t lora_group;
        lora_group.name = "LoRa config";
        lora_group.nvs_namespace = "lora";
        lora_group.items = {
            back_item,
            {"region",
             "Region",
             TYPE_STRING,
             "EU_433",
             "EU_433",
             "UNSET;US;EU_433;EU_868;CN;JP;ANZ;KR;TW;RU;IN;NZ_865;TH;LORA_24;UA_433;UA_868;MY_919;SG_923;BR_902",
             "",
             "LoRa region code",
             mesh_apply_cb},
            {"modem_preset",
             "Modem preset",
             TYPE_STRING,
             "LongFast",
             "LongFast",
             "LongFast;LongSlow;VeryLongSlow;MediumSlow;MediumFast;ShortFast;ShortSlow;LongModerate;ShortTurbo;LongTurbo",
             "",
             "LoRa modem preset",
             mesh_apply_cb},
            {"freq_slot",
             "Frequency slot",
             TYPE_NUMBER,
             "0",
             "0",
             "0",
             "255",
             "Frequency slot number (0 = auto)",
             mesh_apply_cb},
            {"freq_ovr",
             "Freq. override",
             TYPE_NUMBER,
             "0",
             "0",
             "0",
             "999999",
             "Override frequency in kHz (0 = disabled)",
             mesh_apply_cb},
            {"hop_limit",
             "Number of hops",
             TYPE_NUMBER,
             "3",
             "3",
             "1",
             "7",
             "Maximum number of hops for mesh routing (1-7)",
             mesh_apply_cb},
            {"duty_ovr",
             "Duty cycle override",
             TYPE_BOOL,
             "false",
             "false",
             "",
             "",
             "Override duty cycle limit",
             mesh_apply_cb},
            {"rx_boost", "RX boost", TYPE_BOOL, "false", "false", "", "", "Enable SX126x RX boosted gain", mesh_apply_cb},
            {"tx_power", "TX power", TYPE_NUMBER, "22", "22", "-9", "22", "Transmit power in dBm (-9 to 22)", mesh_apply_cb},
        };

        // Security settings
        SettingGroup_t security_group;
        security_group.name = "Security";
        security_group.nvs_namespace = "security";
        security_group.items = {
            back_item,
            {"public_key",
             "Public key",
             TYPE_STRING,
             "",
             "",
             "",
             "",
             "X25519 public key (base64). Auto-generated if empty",
             mesh_apply_cb},
            {"private_key",
             "Private key",
             TYPE_STRING,
             "",
             "",
             "",
             "",
             "X25519 private key (base64). Auto-generated if empty",
             mesh_apply_cb},
            {"regen_keys",
             "Regenerate keys...",
             TYPE_CALLBACK,
             "",
             "",
             "",
             "",
             "Generate new X25519 key pair",
             [this](SettingItem_t& item)
             {
                 if (_hal)
                 {
                     bool confirm = UTILS::UI::show_confirmation_dialog(_hal, "Confirm", "Regenerate the keys?", "Yes", "No");
                     if (!confirm)
                         return;
                 }
                 uint8_t priv_key[32], pub_key[32];
                 bool ok = Mesh::MeshService::generateKeypair(priv_key, pub_key);
                 if (ok)
                 {
                     unsigned char b64[48] = {};
                     size_t b64_len = 0;
                     if (mbedtls_base64_encode(b64, sizeof(b64), &b64_len, priv_key, 32) == 0)
                         setString("security", "private_key", std::string((char*)b64, b64_len));
                     b64_len = 0;
                     if (mbedtls_base64_encode(b64, sizeof(b64), &b64_len, pub_key, 32) == 0)
                         setString("security", "public_key", std::string((char*)b64, b64_len));
                     // apply the changes to the mesh
                     applyMeshConfig(item);
                 }
                 else
                 {
                     if (_hal)
                     {
                         UTILS::UI::show_error_dialog(_hal, "Error", "Key generation failed", "OK");
                     }
                 }
             }},
            {"invitations",
             "Invitations",
             TYPE_BOOL,
             "true",
             "true",
             "",
             "",
             "Allow auto add chennels by invitations in DM. Format: #invite LongFast=AQ==",
             mesh_apply_cb},
            {"clear_nodes",
             "Clear all nodes...",
             TYPE_CALLBACK,
             "",
             "",
             "",
             "",
             "Delete all nodes, DMs and traceroute logs",
             [this](SettingItem_t& item)
             {
                 if (!_hal || !_hal->nodedb())
                     return;
                 bool confirm =
                     UTILS::UI::show_confirmation_dialog(_hal, "Confirm", "Delete all nodes and messages?", "Yes", "No");
                 if (!confirm)
                     return;
                 _hal->nodedb()->clearNodes();
                 _hal->nodedb()->save();
                 UTILS::UI::show_error_dialog(_hal, "Done", "All nodes cleared", "OK");
             }},
        };

        // Node info settings
        SettingGroup_t nodeinfo_group;
        nodeinfo_group.name = "Node info";
        nodeinfo_group.nvs_namespace = "nodeinfo";
        nodeinfo_group.items = {
            back_item,
            {"long_name", "Long name", TYPE_STRING, "", "", "", "40", "Long name for this node", nodeinfo_apply_cb},
            {"short_name",
             "Short name",
             TYPE_STRING,
             "",
             "",
             "",
             "4",
             "Short name for this node (max 4 characters)",
             nodeinfo_apply_cb},
            {"unmessagable",
             "Unmessagable",
             TYPE_BOOL,
             "false",
             "false",
             "",
             "",
             "Node does not accept messages",
             nodeinfo_apply_cb},
            {"ham_licensed",
             "HAM licensed",
             TYPE_BOOL,
             "false",
             "false",
             "",
             "",
             "HAM radio licensed operator",
             nodeinfo_apply_cb},
            {"role",
             "Role",
             TYPE_STRING,
             "Client",
             "Client",
             "Client;Client Mute;Client Hidden;Tracker;Lost+Found;Sensor;TAK;TAK Tracker;Repeater;Router",
             "",
             "Device role (affects routing behavior)",
             nodeinfo_apply_cb},
            {"rebroadcast",
             "Rebroadcast",
             TYPE_STRING,
             "All",
             "All",
             "All;All skip decode;Local only;Known only;None",
             "",
             "Rebroadcast mode for received packets",
             nodeinfo_apply_cb},
            {"bcast_int",
             "Broadcast interval",
             TYPE_STRING,
             "1h",
             "1h",
             "off;15m;30m;1h;2h;4h;8h;12h;24h",
             "",
             "Node info broadcast interval",
             nodeinfo_apply_cb},
        };

        // Position info settings
        SettingGroup_t position_group;
        position_group.name = "Position info";
        position_group.nvs_namespace = "position";
        position_group.items = {
            back_item,
            {"location",
             "Location",
             TYPE_STRING,
             "fixed",
             "fixed",
             "fixed;gps",
             "",
             "Position source (fixed coordinates or live GPS)",
             mesh_apply_cb},
            {"latitude",
             "Latitude fix",
             TYPE_NUMBER,
             "0",
             "0",
             "-900000000",
             "900000000",
             "Fixed latitude (degrees * 1e7, e.g. 504233000 = 50.4233N)",
             mesh_apply_cb},
            {"longitude",
             "Longitude fix",
             TYPE_NUMBER,
             "0",
             "0",
             "-1800000000",
             "1800000000",
             "Fixed longitude (degrees * 1e7, e.g. 304167000 = 30.4167E)",
             mesh_apply_cb},
            {"altitude",
             "Altitude fix",
             TYPE_NUMBER,
             "0",
             "0",
             "-1000",
             "100000",
             "Fixed altitude above sea level (meters)",
             mesh_apply_cb},
            {"pos_alt", "Altitude", TYPE_BOOL, "true", "true", "", "", "Include altitude in position broadcast", mesh_apply_cb},
            {"pos_sats",
             "Satellites",
             TYPE_BOOL,
             "true",
             "true",
             "",
             "",
             "Include satellite count in position broadcast",
             mesh_apply_cb},
            {"pos_seq",
             "Sequence",
             TYPE_BOOL,
             "false",
             "false",
             "",
             "",
             "Include sequence number in position broadcast",
             mesh_apply_cb},
            {"pos_time",
             "Timestamp",
             TYPE_BOOL,
             "true",
             "true",
             "",
             "",
             "Include timestamp in position broadcast",
             mesh_apply_cb},
            {"pos_heading",
             "Heading",
             TYPE_BOOL,
             "false",
             "false",
             "",
             "",
             "Include heading in position broadcast",
             mesh_apply_cb},
            {"pos_speed", "Speed", TYPE_BOOL, "false", "false", "", "", "Include speed in position broadcast", mesh_apply_cb},
            {"bcast_int",
             "Broadcast interval",
             TYPE_STRING,
             "1h",
             "1h",
             "off;15m;30m;1h;2h;4h;8h;12h;24h",
             "",
             "Position broadcast interval",
             mesh_apply_cb},
        };

        // Device metrics settings
        SettingGroup_t devmetrics_group;
        devmetrics_group.name = "Device metrics";
        devmetrics_group.nvs_namespace = "devmetrics";
        devmetrics_group.items = {
            back_item,
            {"bat_level",
             "Battery level",
             TYPE_BOOL,
             "true",
             "true",
             "",
             "",
             "Include battery level in telemetry",
             mesh_apply_cb},
            {"voltage", "Voltage", TYPE_BOOL, "true", "true", "", "", "Include battery voltage in telemetry", mesh_apply_cb},
            {"ch_util",
             "Channel utilization",
             TYPE_BOOL,
             "true",
             "true",
             "",
             "",
             "Include channel utilization in telemetry",
             mesh_apply_cb},
            {"air_util",
             "Air utilization",
             TYPE_BOOL,
             "true",
             "true",
             "",
             "",
             "Include air utilization TX in telemetry",
             mesh_apply_cb},
            {"uptime", "Uptime seconds", TYPE_BOOL, "true", "true", "", "", "Include uptime in telemetry", mesh_apply_cb},
            {"bcast_int",
             "Broadcast interval",
             TYPE_STRING,
             "1h",
             "1h",
             "off;15m;30m;1h;2h;4h;8h;12h;24h",
             "",
             "Device metrics broadcast interval",
             mesh_apply_cb},
        };

        SettingGroup_t export_group;
        export_group.name = "Export (SD card)";
        export_group.items = {};
        export_group.callback = [this](SETTINGS::SettingGroup_t& group)
        {
            bool sdcard_mounted = _hal->sdcard()->is_mounted();
            if (!sdcard_mounted)
                _hal->sdcard()->mount(false);
            if (_hal->sdcard()->is_mounted())
            {
                exportToFile(SETTINGS_FILE_NAME);
                if (!sdcard_mounted)
                    _hal->sdcard()->eject();
                UTILS::UI::show_message_dialog(_hal, "Success", "Settings saved to: " + SETTINGS_FILE_NAME, 0);
            }
            else
            {
                UTILS::UI::show_message_dialog(_hal, "Error", "Failed to mount SD card", 0);
            }
        };

        SettingGroup_t import_group;
        import_group.name = "Import (SD card)";
        import_group.items = {};
        import_group.callback = [this](SETTINGS::SettingGroup_t& group)
        {
            bool sdcard_mounted = _hal->sdcard()->is_mounted();
            if (!sdcard_mounted)
                _hal->sdcard()->mount(false);
            if (_hal->sdcard()->is_mounted())
            {
                importFromFile(SETTINGS_FILE_NAME);
                if (!sdcard_mounted)
                    _hal->sdcard()->eject();
#if HAL_USE_WIFI
                UTILS::UI::show_progress(_hal, "WiFi", -1, "Stopping...");
                delay(500);
                _hal->wifi()->init();
                if (getBool("wifi", "enabled"))
                {
                    UTILS::UI::show_progress(_hal, "WiFi", -1, "Starting...");
                    delay(500);
                    _hal->wifi()->connect();
                }
#endif
                UTILS::UI::show_message_dialog(_hal, "Success", "Loaded from: " + SETTINGS_FILE_NAME, 0);
            }
            else
            {
                UTILS::UI::show_error_dialog(_hal, "Error", "Failed to mount SD card", "OK");
            }
        };

        _metadata = {sys_group,
                     lora_group,
                     security_group,
                     nodeinfo_group,
                     position_group,
                     devmetrics_group,
                     export_group,
                     import_group};
    }

    void Settings::setHal(HAL::Hal* hal) { _hal = hal; }

    Settings::~Settings()
    {
        if (_initialized)
        {
            _deinitNvs();
        }
    }

    bool Settings::init()
    {
        if (_initialized)
        {
            return true;
        }
        ESP_LOGW(TAG, "Settings init");

        if (!_initNvs())
        {
            return false;
        }

        _loadSettings();
        _initialized = true;
        return true;
    }

    std::vector<SettingGroup_t> Settings::getMetadata() const { return _metadata; }

    bool Settings::_initNvs()
    {
        for (const char* const* p = NVS_PARTITIONS; *p; ++p)
        {
            esp_err_t err = nvs_flash_init_partition(*p);
            if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
            {
                ESP_LOGW(TAG, "NVS partition '%s' truncated or new version, erasing...", *p);
                err = nvs_flash_erase_partition(*p);
                if (err == ESP_OK)
                    err = nvs_flash_init_partition(*p);
            }
            if (err == ESP_OK)
            {
                _active_partition = *p;
                ESP_LOGI(TAG, "NVS initialized on partition '%s'", *p);
                return true;
            }
            ESP_LOGW(TAG, "Partition '%s' failed: %s", *p, esp_err_to_name(err));
        }

        ESP_LOGE(TAG, "Failed to initialize NVS on any partition");
        return false;
    }

    void Settings::_deinitNvs()
    {
        if (!_active_partition)
            return;
        esp_err_t err = nvs_flash_deinit_partition(_active_partition);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to deinitialize NVS partition '%s': %s", _active_partition, esp_err_to_name(err));
        }
        _active_partition = nullptr;
    }

    std::string Settings::_makeKey(const std::string& ns, const std::string& key) const
    {
        return std::format("{}-{}", ns, key);
    }

    const SettingItem_t* Settings::_findItem(const std::string& ns, const std::string& key) const
    {
        for (const auto& group : _metadata)
        {
            if (group.nvs_namespace == ns)
            {
                for (const auto& item : group.items)
                {
                    if (item.key == key)
                    {
                        return &item;
                    }
                }
            }
        }
        return nullptr;
    }

    void Settings::_loadSettings()
    {
        for (const auto& group : _metadata)
        {
            nvs_handle_t nvs_handle;
            esp_err_t err = nvs_open_from_partition(_active_partition, group.nvs_namespace.c_str(), NVS_READONLY, &nvs_handle);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Error opening NVS namespace %s", group.nvs_namespace.c_str());
                continue;
            }

            for (const auto& item : group.items)
            {
                if (item.type == TYPE_NONE)
                    continue;

                std::string cache_key = _makeKey(group.nvs_namespace, item.key);
                CachedValue cached_value;
                cached_value.type = item.type;

                switch (item.type)
                {
                case TYPE_BOOL:
                {
                    uint8_t value;
                    if (nvs_get_u8(nvs_handle, item.key.c_str(), &value) != ESP_OK)
                    {
                        value = item.default_val == "true" ? 1 : 0;
                    }
                    cached_value.bool_val = value == 1;
                    ESP_LOGI(TAG, "Loaded bool %s = %d", cache_key.c_str(), cached_value.bool_val);
                    break;
                }
                case TYPE_NUMBER:
                {
                    int32_t value;
                    if (nvs_get_i32(nvs_handle, item.key.c_str(), &value) != ESP_OK)
                    {
                        value = std::stoi(item.default_val);
                    }
                    cached_value.num_val = value;
                    ESP_LOGI(TAG, "Loaded number %s = %ld", cache_key.c_str(), cached_value.num_val);
                    break;
                }
                case TYPE_STRING:
                {
                    size_t required_size = 0;
                    if (nvs_get_str(nvs_handle, item.key.c_str(), nullptr, &required_size) == ESP_OK)
                    {
                        std::vector<char> value(required_size);
                        if (nvs_get_str(nvs_handle, item.key.c_str(), value.data(), &required_size) == ESP_OK)
                        {
                            cached_value.str_val = std::string(value.data());
                        }
                    }
                    if (cached_value.str_val.empty())
                    {
                        cached_value.str_val = item.default_val;
                    }
                    ESP_LOGI(TAG, "Loaded string %s = %s", cache_key.c_str(), cached_value.str_val.c_str());
                    break;
                }
                default:
                    break;
                }

                _cache[cache_key] = cached_value;
            }
            nvs_close(nvs_handle);
        }
    }

    bool Settings::getBool(const std::string& ns, const std::string& key)
    {
        std::string cache_key = _makeKey(ns, key);
        auto it = _cache.find(cache_key);
        if (it != _cache.end() && it->second.type == TYPE_BOOL)
        {
            return it->second.bool_val;
        }

        const SettingItem_t* item = _findItem(ns, key);
        return item ? (item->default_val == "true") : false;
    }

    int32_t Settings::getNumber(const std::string& ns, const std::string& key)
    {
        std::string cache_key = _makeKey(ns, key);
        auto it = _cache.find(cache_key);
        if (it != _cache.end() && it->second.type == TYPE_NUMBER)
        {
            return it->second.num_val;
        }

        const SettingItem_t* item = _findItem(ns, key);
        return item ? std::stoi(item->default_val) : 0;
    }

    std::string Settings::getString(const std::string& ns, const std::string& key)
    {
        std::string cache_key = _makeKey(ns, key);
        auto it = _cache.find(cache_key);
        if (it != _cache.end() && it->second.type == TYPE_STRING)
        {
            return it->second.str_val;
        }

        const SettingItem_t* item = _findItem(ns, key);
        return item ? item->default_val : "";
    }

    bool Settings::setBool(const std::string& ns, const std::string& key, bool value)
    {
        const SettingItem_t* item = _findItem(ns, key);
        if (!item || item->type != TYPE_BOOL)
        {
            return false;
        }

        std::string cache_key = _makeKey(ns, key);
        CachedValue cached_value;
        cached_value.type = TYPE_BOOL;
        cached_value.bool_val = value;
        _cache[cache_key] = cached_value;

        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open_from_partition(_active_partition, ns.c_str(), NVS_READWRITE, &nvs_handle);
        if (err != ESP_OK)
        {
            return false;
        }

        err = nvs_set_u8(nvs_handle, key.c_str(), value ? 1 : 0);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);

        return err == ESP_OK;
    }

    bool Settings::setNumber(const std::string& ns, const std::string& key, int32_t value)
    {
        const SettingItem_t* item = _findItem(ns, key);
        if (!item || item->type != TYPE_NUMBER)
        {
            return false;
        }

        std::string cache_key = _makeKey(ns, key);
        CachedValue cached_value;
        cached_value.type = TYPE_NUMBER;
        cached_value.num_val = value;
        _cache[cache_key] = cached_value;

        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open_from_partition(_active_partition, ns.c_str(), NVS_READWRITE, &nvs_handle);
        if (err != ESP_OK)
        {
            return false;
        }

        err = nvs_set_i32(nvs_handle, key.c_str(), value);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);

        return err == ESP_OK;
    }

    bool Settings::setString(const std::string& ns, const std::string& key, const std::string& value)
    {
        const SettingItem_t* item = _findItem(ns, key);
        if (!item || item->type != TYPE_STRING)
        {
            return false;
        }

        std::string cache_key = _makeKey(ns, key);
        CachedValue cached_value;
        cached_value.type = TYPE_STRING;
        cached_value.str_val = value;
        _cache[cache_key] = cached_value;

        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open_from_partition(_active_partition, ns.c_str(), NVS_READWRITE, &nvs_handle);
        if (err != ESP_OK)
        {
            return false;
        }

        err = nvs_set_str(nvs_handle, key.c_str(), value.c_str());
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);

        return err == ESP_OK;
    }

    bool Settings::saveAll()
    {
        bool success = true;
        for (const auto& group : _metadata)
        {
            nvs_handle_t nvs_handle;
            esp_err_t err = nvs_open_from_partition(_active_partition, group.nvs_namespace.c_str(), NVS_READWRITE, &nvs_handle);
            if (err != ESP_OK)
            {
                success = false;
                continue;
            }

            for (const auto& item : group.items)
            {
                if (item.type == TYPE_NONE)
                    continue;

                std::string cache_key = _makeKey(group.nvs_namespace, item.key);
                auto it = _cache.find(cache_key);
                if (it == _cache.end())
                    continue;

                switch (item.type)
                {
                case TYPE_BOOL:
                    err = nvs_set_u8(nvs_handle, item.key.c_str(), it->second.bool_val ? 1 : 0);
                    break;
                case TYPE_NUMBER:
                    err = nvs_set_i32(nvs_handle, item.key.c_str(), it->second.num_val);
                    break;
                case TYPE_STRING:
                    err = nvs_set_str(nvs_handle, item.key.c_str(), it->second.str_val.c_str());
                    break;
                default:
                    break;
                }

                if (err != ESP_OK)
                {
                    success = false;
                }
            }

            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
        }

        return success;
    }

    bool Settings::exportToFile(const std::string& filename) const
    {
        ESP_LOGI(TAG, "Exporting settings to %s", filename.c_str());

        std::map<std::string, std::string> existing_settings;

        std::ifstream infile(filename);
        if (infile.is_open())
        {
            std::string line;
            while (std::getline(infile, line))
            {
                line.erase(0, line.find_first_not_of(" \t\n\r"));
                line.erase(line.find_last_not_of(" \t\n\r") + 1);

                if (line.empty() || line[0] == '#')
                {
                    continue;
                }

                size_t equals_pos = line.find('=');
                if (equals_pos == std::string::npos)
                {
                    ESP_LOGW(TAG, "Skipping invalid line during export pre-read: %s", line.c_str());
                    continue;
                }

                std::string cache_key = line.substr(0, equals_pos);
                std::string value_str = line.substr(equals_pos + 1);

                size_t separator_pos = cache_key.find('-');
                if (separator_pos == std::string::npos)
                {
                    ESP_LOGW(TAG,
                             "Invalid key format (missing namespace separator '-') in existing file: %s",
                             cache_key.c_str());
                    continue;
                }
                existing_settings[cache_key] = value_str;
            }
            infile.close();
            ESP_LOGI(TAG, "Read file %s. Found %d settings", filename.c_str(), existing_settings.size());
        }
        else
        {
            ESP_LOGI(TAG, "File %s does not exist, creating new", filename.c_str());
        }
        // replacing settings in map with current values
        for (const auto& group : _metadata)
        {
            for (const auto& item : group.items)
            {
                if (item.type == TYPE_NONE)
                    continue;

                std::string cache_key = _makeKey(group.nvs_namespace, item.key);
                auto it = _cache.find(cache_key);
                if (it == _cache.end())
                {
                    ESP_LOGW(TAG, "Setting %s not found in cache during export, skipping", cache_key.c_str());
                    continue;
                }

                // outfile << cache_key << "=";
                std::string str_val;
                switch (item.type)
                {
                case TYPE_BOOL:
                    str_val = (it->second.bool_val ? "true" : "false");
                    break;
                case TYPE_NUMBER:
                    str_val = std::to_string(it->second.num_val);
                    break;
                case TYPE_STRING:
                {
                    std::string escaped_str;
                    for (char c : it->second.str_val)
                    {
                        if (c == '\n')
                        {
                            escaped_str += "\\n";
                        }
                        else
                        {
                            escaped_str += c;
                        }
                    }
                    str_val = escaped_str;
                }
                break;
                default:
                    break;
                }
                // rewriting if exists
                existing_settings[cache_key] = str_val;
            }
        }
        // saving to file
        std::ofstream outfile(filename);
        if (!outfile.is_open())
        {
            ESP_LOGE(TAG, "Failed to open file %s for writing", filename.c_str());
            return false;
        }
        for (const auto& [key, value] : existing_settings)
        {
            outfile << key << "=" << value << std::endl;
        }
        outfile.close();
        ESP_LOGI(TAG, "Settings successfully exported to %s", filename.c_str());
        return true;
    }

    bool Settings::importFromFile(const std::string& filename)
    {
        ESP_LOGI(TAG, "Importing settings from %s", filename.c_str());
        std::ifstream infile(filename);
        if (!infile.is_open())
        {
            ESP_LOGE(TAG, "Failed to open file %s for reading", filename.c_str());
            return false;
        }

        std::string line;
        bool success = false;
        int line_num = 0;

        while (std::getline(infile, line))
        {
            line_num++;
            line.erase(0, line.find_first_not_of(" \t\n\r"));
            line.erase(line.find_last_not_of(" \t\n\r") + 1);

            if (line.empty() || line[0] == '#')
                continue;

            size_t equals_pos = line.find('=');
            if (equals_pos == std::string::npos)
            {
                ESP_LOGW(TAG, "Invalid format on line %d in %s: %s", line_num, filename.c_str(), line.c_str());
                continue;
            }

            std::string cache_key = line.substr(0, equals_pos);
            std::string value_str = line.substr(equals_pos + 1);

            size_t separator_pos = cache_key.find('-');
            if (separator_pos == std::string::npos)
            {
                ESP_LOGW(TAG,
                         "Invalid key format on line %d (missing namespace separator '-'): %s",
                         line_num,
                         cache_key.c_str());
                continue;
            }
            std::string ns = cache_key.substr(0, separator_pos);
            std::string key = cache_key.substr(separator_pos + 1);

            const SettingItem_t* item = _findItem(ns, key);
            if (!item)
            {
                ESP_LOGW(TAG,
                         "Setting %s (ns=%s, key=%s) not found in metadata, skipping line %d",
                         cache_key.c_str(),
                         ns.c_str(),
                         key.c_str(),
                         line_num);
                continue;
            }

            bool import_ok = false;
            switch (item->type)
            {
            case TYPE_BOOL:
            {
                bool val = (value_str == "true");
                if (value_str != "true" && value_str != "false")
                {
                    ESP_LOGW(TAG,
                             "Invalid boolean value '%s' for %s on line %d, using default",
                             value_str.c_str(),
                             cache_key.c_str(),
                             line_num);
                    val = (item->default_val == "true");
                }
                import_ok = setBool(ns, key, val);
                break;
            }
            case TYPE_NUMBER:
            {
                int32_t val = std::stoi(value_str);
                import_ok = setNumber(ns, key, val);
                break;
            }
            case TYPE_STRING:
            {
                std::string unescaped_str;
                for (size_t i = 0; i < value_str.length(); ++i)
                {
                    if (value_str[i] == '\\' && i + 1 < value_str.length() && value_str[i + 1] == 'n')
                    {
                        unescaped_str += '\n';
                        i++; // Skip the 'n'
                    }
                    else
                    {
                        unescaped_str += value_str[i];
                    }
                }
                import_ok = setString(ns, key, unescaped_str);
                break;
            }
            case TYPE_NONE:
            default:
                break;
            }

            if (import_ok)
            {
                ESP_LOGI(TAG, "Imported setting: %s = %s", cache_key.c_str(), value_str.c_str());
                success = true;
            }
            else
            {
                ESP_LOGW(TAG, "Failed to import setting %s on line %d", cache_key.c_str(), line_num);
            }
        }

        infile.close();

        if (success)
        {
            ESP_LOGI(TAG, "Settings successfully imported from %s", filename.c_str());
            // Re-apply LoRa / mesh config so the radio immediately uses the imported values
            if (_hal && _hal->mesh())
            {
                Mesh::MeshConfig cfg = _hal->mesh()->getConfig();
                _hal->mesh()->loadConfigFromSettings(cfg);
                _hal->mesh()->setConfig(cfg);
                // force node info broadcast
                _hal->mesh()->forceNodeInfoBroadcast();
                ESP_LOGI(TAG, "Mesh/LoRa config re-applied after import");
            }
        }
        else
        {
            ESP_LOGW(TAG, "No settings were imported from %s", filename.c_str());
        }

        return success;
    }

    void Settings::applyMeshConfig(SettingItem_t& item)
    {
        ESP_LOGI(TAG, "Applying mesh config from setting: %s", item.key.c_str());
        if (!_hal || !_hal->mesh())
            return;

        // Get current config as base (preserves node_id, channel, etc.)
        Mesh::MeshConfig cfg = _hal->mesh()->getConfig();
        _hal->mesh()->loadConfigFromSettings(cfg);
        _hal->mesh()->setConfig(cfg);

        ESP_LOGI(TAG, "Mesh config applied from settings");
    }

} // namespace SETTINGS
