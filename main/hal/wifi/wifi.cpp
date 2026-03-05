/**
 * @file wifi.cpp
 * @brief WiFi module implementation for M5Cardputer
 * @version 0.1
 * @date 2023-07-01
 */

#include "wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include <cstdlib>

static const char* TAG = "WIFI";

namespace HAL
{

    // Static instance pointer for event handler
    static WiFi* s_wifi_instance = nullptr;

    WiFi::WiFi(SETTINGS::Settings* settings)
        : _settings(settings), _status(WIFI_STATUS_IDLE), _initialized(false), _rssi(0), _last_status_check(0)
    {
        s_wifi_instance = this;
        // memset(_sta_mac, 0, sizeof(_sta_mac));
    }

    WiFi::~WiFi()
    {
        deinit();
        s_wifi_instance = nullptr;
    }

    void WiFi::_wifi_promiscuous_rx_cb(void* buf, wifi_promiscuous_pkt_type_t type)
    {
        if (s_wifi_instance == nullptr)
            return;

        // Only process data frames
        if (type == WIFI_PKT_MGMT)
        {
            wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
            if (pkt != nullptr)
            {
                int8_t rssi = pkt->rx_ctrl.rssi;
                // Update RSSI if it changed significantly (to avoid too frequent callbacks)
                if (abs(s_wifi_instance->_rssi - rssi) >= 5 || s_wifi_instance->_rssi == 0)
                {
                    s_wifi_instance->_rssi = rssi;
                    s_wifi_instance->_update_status_from_rssi();
                }
            }
        }
    }

    void WiFi::_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
    {
        if (s_wifi_instance == nullptr)
            return;

        if (event_base == WIFI_EVENT)
        {
            bool status_changed = false;
            switch (event_id)
            {
            case WIFI_EVENT_STA_START:
                ESP_LOGD(TAG, "WiFi station started, connecting");
                esp_wifi_connect();
                status_changed = s_wifi_instance->_status != WIFI_STATUS_CONNECTING;
                s_wifi_instance->_status = WIFI_STATUS_CONNECTING;
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi connected");
                status_changed = s_wifi_instance->_status != WIFI_STATUS_CONNECTED_WEAK;
                s_wifi_instance->_status = WIFI_STATUS_CONNECTED_WEAK;

                // // Get connected AP info
                // wifi_ap_record_t ap_info;
                // if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
                // {
                //     // Store AP BSSID for packet filtering
                //     memcpy(s_wifi_instance->_ap_bssid, ap_info.bssid, 6);
                // }

                // Get our station MAC address
                // esp_wifi_get_mac(WIFI_IF_STA, s_wifi_instance->_sta_mac);

                // Status will be updated based on RSSI in promiscuous callback
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "WiFi disconnected");
                // Disable promiscuous mode
                esp_wifi_set_promiscuous(false);
                esp_wifi_set_promiscuous_rx_cb(nullptr);
                ESP_LOGD(TAG, "Promiscuous mode disabled");
                status_changed = s_wifi_instance->_status != WIFI_STATUS_DISCONNECTED;
                s_wifi_instance->_status = WIFI_STATUS_DISCONNECTED;
                s_wifi_instance->_rssi = 0;
                // Clear AP BSSID
                // memset(s_wifi_instance->_ap_bssid, 0, sizeof(s_wifi_instance->_ap_bssid));
                // Try to reconnect if enabled
                if (s_wifi_instance->_settings->getBool("wifi", "enabled"))
                {
                    ESP_LOGI(TAG, "WiFi reconnecting...");
                    esp_wifi_connect();
                    status_changed = s_wifi_instance->_status != WIFI_STATUS_CONNECTING;
                    s_wifi_instance->_status = WIFI_STATUS_CONNECTING;
                }
                break;
            }
            if (status_changed && s_wifi_instance->_status_callback)
            {
                s_wifi_instance->_status_callback(s_wifi_instance->_status);
            }
        }
        else if (event_base == IP_EVENT)
        {
            if (event_id == IP_EVENT_STA_GOT_IP)
            {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
                ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
                // set IP, mask and gateway to settings
                s_wifi_instance->_settings->setString("wifi", "ip", ip4addr_ntoa((ip4_addr_t*)&event->ip_info.ip));
                s_wifi_instance->_settings->setString("wifi", "mask", ip4addr_ntoa((ip4_addr_t*)&event->ip_info.netmask));
                s_wifi_instance->_settings->setString("wifi", "gateway", ip4addr_ntoa((ip4_addr_t*)&event->ip_info.gw));
            }
        }
    }

    bool WiFi::init()
    {
        if (_initialized)
        {
            deinit();
        }
        if (!_settings->getBool("wifi", "enabled"))
        {
            _status = WIFI_STATUS_IDLE;
            ESP_LOGD(TAG, "WiFi is disabled by settings");
            return true;
        }
        _wifi_settings.ssid = _settings->getString("wifi", "ssid");
        _wifi_settings.password = _settings->getString("wifi", "pass");
        _wifi_settings.static_ip = _settings->getBool("wifi", "static_ip");
        _wifi_settings.ip = _settings->getString("wifi", "ip");
        _wifi_settings.mask = _settings->getString("wifi", "mask");
        _wifi_settings.gateway = _settings->getString("wifi", "gateway");
        _wifi_settings.dns = _settings->getString("wifi", "dns");

        ESP_LOGI(TAG,
                 "Initializing WiFi with SSID: %s, password: %s",
                 _wifi_settings.ssid.c_str(),
                 _wifi_settings.password.c_str());

        // Initialize TCP/IP adapter
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize TCP/IP adapter: %s", esp_err_to_name(err));
            return false;
        }

        // Enable promiscuous mode for RSSI updates
        esp_wifi_set_promiscuous_rx_cb(&WiFi::_wifi_promiscuous_rx_cb);
        esp_wifi_set_promiscuous(true);
        ESP_LOGD(TAG, "Promiscuous mode enabled for RSSI updates");

        // Create default event loop if not already created
        err = esp_event_loop_create_default();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
            return false;
        }

        // Create default netif instance
        _sta_netif = esp_netif_create_default_wifi_sta();

        // Configure static IP if enabled
        if (_wifi_settings.static_ip)
        {
            ESP_LOGI(TAG, "Configuring static IP: %s", _wifi_settings.ip.c_str());

            esp_netif_dhcpc_stop(_sta_netif);

            esp_netif_ip_info_t ip_info;
            memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));

            ip4addr_aton(_wifi_settings.ip.c_str(), (ip4_addr_t*)&ip_info.ip);
            ip4addr_aton(_wifi_settings.mask.c_str(), (ip4_addr_t*)&ip_info.netmask);
            ip4addr_aton(_wifi_settings.gateway.c_str(), (ip4_addr_t*)&ip_info.gw);

            err = esp_netif_set_ip_info(_sta_netif, &ip_info);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set static IP: %s", esp_err_to_name(err));
                return false;
            }

            // Set DNS server
            ip_addr_t dns_server;
            ip4addr_aton(_wifi_settings.dns.c_str(), (ip4_addr_t*)&dns_server);
            dns_setserver(0, &dns_server);
        }

        // Initialize WiFi
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
            return false;
        }

        // Register event handlers
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &_wifi_event_handler, NULL);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(err));
            return false;
        }
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &_wifi_event_handler, NULL);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(err));
            return false;
        }

        // Configure WiFi station
        wifi_config_t wifi_config = {};
        memset(&wifi_config, 0, sizeof(wifi_config_t));

        strncpy((char*)wifi_config.sta.ssid, _wifi_settings.ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, _wifi_settings.password.c_str(), sizeof(wifi_config.sta.password) - 1);

        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(err));
            return false;
        }
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(err));
            return false;
        }

        _initialized = true;
        _status = WIFI_STATUS_DISCONNECTED;

        return true;
    }

    void WiFi::deinit()
    {
        if (!_initialized)
            return;

        disconnect();
        // Disable promiscuous mode if enabled
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(nullptr);

        esp_err_t err = esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &_wifi_event_handler);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to unregister IP event handler: %s", esp_err_to_name(err));
        }
        err = esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &_wifi_event_handler);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to unregister WiFi event handler: %s", esp_err_to_name(err));
        }
        esp_netif_destroy_default_wifi(_sta_netif);
        _sta_netif = nullptr;
        err = esp_event_loop_delete_default();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to delete default event loop: %s", esp_err_to_name(err));
        }
        err = esp_wifi_deinit();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to deinitialize WiFi: %s", esp_err_to_name(err));
        }
        esp_netif_deinit();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to deinitialize TCP/IP adapter: %s", esp_err_to_name(err));
        }
        _initialized = false;
        _status = WIFI_STATUS_IDLE;

        if (_status_callback)
        {
            _status_callback(_status);
        }
    }

    bool WiFi::connect()
    {
        if (!_initialized)
        {
            ESP_LOGE(TAG, "WiFi not initialized");
            return false;
        }

        if (_status == WIFI_STATUS_CONNECTING || is_connected())
        {
            ESP_LOGI(TAG, "WiFi already connecting or connected");
            return true;
        }

        esp_err_t err = esp_wifi_start();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
            return false;
        }

        _status = WIFI_STATUS_CONNECTING;

        if (_status_callback)
        {
            _status_callback(_status);
        }

        return true;
    }

    void WiFi::disconnect()
    {
        if (!_initialized)
            return;

        ESP_LOGD(TAG, "Disconnecting WiFi");

        esp_wifi_disconnect();
        esp_wifi_stop();

        _status = WIFI_STATUS_DISCONNECTED;
        _rssi = 0;

        if (_status_callback)
        {
            _status_callback(_status);
        }
    }

    wifi_status_t WiFi::get_status() const { return _status; }

    int8_t WiFi::get_rssi() const { return _rssi; }

    bool WiFi::is_connected() const
    {
        return _status == WIFI_STATUS_CONNECTED_WEAK || _status == WIFI_STATUS_CONNECTED_GOOD ||
               _status == WIFI_STATUS_CONNECTED_STRONG;
    }

    void WiFi::set_status_callback(std::function<void(wifi_status_t)> callback) { _status_callback = callback; }

    // void WiFi::update()
    // {
    //     if (!_initialized || !is_connected())
    //         return;

    //     // Check RSSI periodically (every 5 seconds)
    //     uint32_t now = esp_log_timestamp();
    //     if (now - _last_status_check > 5000)
    //     {
    //         _last_status_check = now;

    //         wifi_ap_record_t ap_info;
    //         if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    //         {
    //             _rssi = ap_info.rssi;
    //             ESP_LOGD(TAG, "RSSI: %d", _rssi);
    //             _update_status_from_rssi();
    //         }
    //         else
    //         {
    //             ESP_LOGE(TAG, "Failed to get AP info");
    //         }
    //     }
    // }

    void WiFi::_update_status_from_rssi()
    {
        // update only signal level, not connection status
        if (_initialized && is_connected())
        {
            // Update status based on RSSI
            if (_rssi == 0)
            {
                _status = WIFI_STATUS_CONNECTED_WEAK;
            }
            else if (_rssi < -80)
            {
                _status = WIFI_STATUS_CONNECTED_WEAK;
            }
            else if (_rssi < -67)
            {
                _status = WIFI_STATUS_CONNECTED_GOOD;
            }
            else
            {
                _status = WIFI_STATUS_CONNECTED_STRONG;
            }
        }
        // Notify if status changed
        if (_status_callback)
        {
            _status_callback(_status);
        }
    }

    std::vector<std::string> WiFi::scan()
    {
        std::vector<std::string> networks;
        bool temp_init = false;
        esp_netif_t* temp_sta_netif = nullptr;
        uint16_t ap_count = 0;
        uint16_t num_aps = 0;
        wifi_ap_record_t* ap_records = nullptr;

        // If WiFi is not initialized, perform temporary initialization
        if (!_initialized)
        {
            ESP_LOGI(TAG, "Performing temporary WiFi initialization for scanning");

            // Initialize TCP/IP adapter
            esp_err_t err = esp_netif_init();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to initialize TCP/IP adapter: %s", esp_err_to_name(err));
                return networks;
            }

            // Create default event loop
            err = esp_event_loop_create_default();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
                return networks;
            }

            // Create default netif instance
            temp_sta_netif = esp_netif_create_default_wifi_sta();
            if (!temp_sta_netif)
            {
                ESP_LOGE(TAG, "Failed to create default WiFi STA netif");
                return networks;
            }

            // Initialize WiFi with default config
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            err = esp_wifi_init(&cfg);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
                esp_netif_destroy_default_wifi(temp_sta_netif);
                return networks;
            }

            // Set WiFi mode and start it
            err = esp_wifi_set_mode(WIFI_MODE_STA);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(err));
                esp_wifi_deinit();
                esp_netif_destroy_default_wifi(temp_sta_netif);
                return networks;
            }

            err = esp_wifi_start();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
                esp_wifi_deinit();
                esp_netif_destroy_default_wifi(temp_sta_netif);
                return networks;
            }

            temp_init = true;
        }

        // Configure scan parameters
        wifi_scan_config_t scan_config = {.ssid = NULL,
                                          .bssid = NULL,
                                          .channel = 0,
                                          .show_hidden = false,
                                          .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                                          .scan_time = {.active = {.min = 0, .max = 0}}};

        // Start scan
        esp_err_t err = esp_wifi_scan_start(&scan_config, true); // true = block until scan completes
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start WiFi scan: %s", esp_err_to_name(err));
            goto cleanup;
        }

        // Get scan results
        esp_wifi_scan_get_ap_num(&ap_count);

        if (ap_count == 0)
        {
            ESP_LOGI(TAG, "No networks found");
            goto cleanup;
        }

        // Cap the number of networks to display
        if (ap_count > 20)
            ap_count = 20;

        // Allocate memory for AP records
        ap_records = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * ap_count);
        if (!ap_records)
        {
            ESP_LOGE(TAG, "Failed to allocate memory for AP records");
            goto cleanup;
        }

        // Get AP records
        num_aps = ap_count;
        err = esp_wifi_scan_get_ap_records(&num_aps, ap_records);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(err));
            free(ap_records);
            goto cleanup;
        }

        // Sort APs by signal strength (RSSI)
        for (int i = 0; i < num_aps - 1; i++)
        {
            for (int j = i + 1; j < num_aps; j++)
            {
                if (ap_records[j].rssi > ap_records[i].rssi)
                {
                    wifi_ap_record_t temp = ap_records[i];
                    ap_records[i] = ap_records[j];
                    ap_records[j] = temp;
                }
            }
        }

        // Create list of networks with signal strength indicators
        for (int i = 0; i < num_aps; i++)
        {
            std::string ssid((char*)ap_records[i].ssid);
            // add only unique networks
            if (!ssid.empty() && std::find(networks.begin(), networks.end(), ssid) == networks.end())
            {
                networks.push_back(ssid);
            }
        }

        free(ap_records);
    cleanup:
        // Clean up temporary initialization if needed
        if (temp_init)
        {
            esp_wifi_stop();
            esp_wifi_deinit();
            esp_netif_destroy_default_wifi(temp_sta_netif);
            esp_event_loop_delete_default();
            esp_netif_deinit();
        }

        return networks;
    }

} // namespace HAL