/**
 * @file hal_cardputer.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-09-22
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "hal_cardputer.h"
#include "hal_config.h"
#include "common_define.h"
#if HAL_USE_DISPLAY
#include "display/display.hpp"
#endif
#if HAL_USE_RADIO
#include "radio/sx1262.h"
#endif
#include "mesh/mesh_service.h"
#include "mesh/node_db.h"
#include "mesh/mesh_data.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/base64.h"

static const char* TAG = "HAL";
static const char* DEFAULT_CHANNEL_PSK_B64 = "AQ==";

#if HAL_USE_SPEAKER
extern const uint8_t morse_wav_start[] asm("_binary_morse_wav_start");
extern const uint8_t morse_wav_end[] asm("_binary_morse_wav_end");
extern const uint8_t seagull_wav_start[] asm("_binary_seagull_wav_start");
extern const uint8_t seagull_wav_end[] asm("_binary_seagull_wav_end");
extern const uint8_t pum_pam_wav_start[] asm("_binary_pum_pam_wav_start");
extern const uint8_t pum_pam_wav_end[] asm("_binary_pum_pam_wav_end");
extern const uint8_t tum_tum_wav_start[] asm("_binary_tum_tum_wav_start");
extern const uint8_t tum_tum_wav_end[] asm("_binary_tum_tum_wav_end");
extern const uint8_t eight_bit_wav_start[] asm("_binary_eight_bit_wav_start");
extern const uint8_t eight_bit_wav_end[] asm("_binary_eight_bit_wav_end");
extern const uint8_t knock_wav_start[] asm("_binary_knock_wav_start");
extern const uint8_t knock_wav_end[] asm("_binary_knock_wav_end");
extern const uint8_t gps_wav_start[] asm("_binary_gps_wav_start");
extern const uint8_t gps_wav_end[] asm("_binary_gps_wav_end");

static const std::vector<const uint8_t*> NOTIFICATION_SOUNDS = {
    nullptr,
    nullptr,
    morse_wav_start,
    seagull_wav_start,
    tum_tum_wav_start,
    pum_pam_wav_start,
    eight_bit_wav_start,
    knock_wav_start,
    gps_wav_start,
};
static const std::vector<int32_t> NOTIFICATION_SOUNDS_LENGTH = {
    0,
    0,
    morse_wav_end - morse_wav_start,
    seagull_wav_end - seagull_wav_start,
    tum_tum_wav_end - tum_tum_wav_start,
    pum_pam_wav_end - pum_pam_wav_start,
    eight_bit_wav_end - eight_bit_wav_start,
    knock_wav_end - knock_wav_start,
    gps_wav_end - gps_wav_start,
};
#endif

static bool decodeBase64(const char* input, uint8_t* output, size_t output_size, size_t* out_len)
{
    if (!input || !output || !out_len)
    {
        return false;
    }

    size_t decoded_len = 0;
    int res =
        mbedtls_base64_decode(output, output_size, &decoded_len, reinterpret_cast<const unsigned char*>(input), strlen(input));
    if (res != 0)
    {
        return false;
    }

    *out_len = decoded_len;
    return true;
}

using namespace HAL;

#if HAL_USE_SPEAKER
void HalCardputer::playNotificationSound(uint32_t index)
{
    if (index < NOTIFICATION_SOUNDS.size() && NOTIFICATION_SOUNDS[index])
        speaker()->playWav(NOTIFICATION_SOUNDS[index], NOTIFICATION_SOUNDS_LENGTH[index]);
    else
        playMessageSound();
}
#endif

#if HAL_USE_I2C
void HalCardputer::_init_i2c()
{
    ESP_LOGI(TAG, "init i2c");
    _i2c = new I2CMaster();
}
#endif

#if HAL_USE_DISPLAY
void HalCardputer::_init_display()
{
    ESP_LOGI(TAG, "init display");

    _display = new LGFX;
    _canvas_system_bar = new LGFX_Sprite(_display);
    _canvas_system_bar->createSprite(_display->width(), 21);
    _canvas = new LGFX_Sprite(_display);
    _canvas->createSprite(_display->width(), _display->height() - _canvas_system_bar->height());
}
#endif

#if HAL_USE_KEYBOARD
void HalCardputer::_init_keyboard()
{
    ESP_LOGI(TAG, "init keyboard");
    _keyboard = new KEYBOARD::Keyboard(this);
    _board_type = _keyboard->boardType();
}
#endif

#if HAL_USE_SPEAKER
void HalCardputer::_init_speaker()
{
    ESP_LOGI(TAG, "init speaker");
    _speaker = new Speaker(this);
}
#endif

#if HAL_USE_BUTTON
void HalCardputer::_init_button()
{
    ESP_LOGI(TAG, "init button");
    _homeButton = new Button(0);
}
#endif

#if HAL_USE_BAT
void HalCardputer::_init_bat()
{
    ESP_LOGI(TAG, "init battery");
    _battery = new Battery();
}
#endif

#if HAL_USE_SDCARD
void HalCardputer::_init_sdcard()
{
    ESP_LOGI(TAG, "init sdcard");
    _sdcard = new SDCard;
}
#endif

#if HAL_USE_USB
void HalCardputer::_init_usb()
{
    ESP_LOGI(TAG, "init usb");
    _usb = new USB(this);
}
#endif

#if HAL_USE_WIFI
void HalCardputer::_init_wifi()
{
    ESP_LOGI(TAG, "init wifi");
    _wifi = new WiFi(_settings);
    _wifi->set_status_callback(
        [this](wifi_status_t status)
        {
            if (!_settings->getBool("system", "use_led"))
            {
                return;
            }
#if HAL_USE_LED
            LED* led = this->led();
            if (led == nullptr)
            {
                return;
            }
            switch (status)
            {
            case WIFI_STATUS_IDLE:
                led->off();
                break;
            case WIFI_STATUS_DISCONNECTED:
                led->blink_periodic({127, 0, 0}, 50, 2000);
                break;
            case WIFI_STATUS_CONNECTING:
                led->blink_periodic({127, 0, 0}, 50, 1000);
                break;
            case WIFI_STATUS_CONNECTED_WEAK:
                led->blink_periodic_double({255 / 2, 106 / 2, 0}, 50, 50, 2000);
                break;
            case WIFI_STATUS_CONNECTED_GOOD:
                led->blink_periodic_double({120 / 2, 255 / 2, 32 / 2}, 50, 50, 2000);
                break;
            case WIFI_STATUS_CONNECTED_STRONG:
                led->blink_periodic_double({0, 38 / 2, 255 / 2}, 50, 50, 2000);
                break;
            };
#endif
        });
}
#endif

#if HAL_USE_LED
void HalCardputer::_init_led()
{
    ESP_LOGI(TAG, "init led");
    _led = new LED(RGB_LED_GPIO);
    // turn off led
    _led->off();
}
#endif

void HalCardputer::init()
{
    ESP_LOGI(TAG, "HAL init");

#if HAL_USE_I2C
    _init_i2c();
#endif
#if HAL_USE_DISPLAY
    _init_display();
#endif
#if HAL_USE_KEYBOARD
    _init_keyboard();
#endif
#if HAL_USE_SPEAKER
    _init_speaker();
#endif
#if HAL_USE_BUTTON
    _init_button();
#endif
#if HAL_USE_BAT
    _init_bat();
#endif
#if HAL_USE_SDCARD
    _init_sdcard();
#endif
#if HAL_USE_USB
    _init_usb();
#endif
#if HAL_USE_LED
    _init_led();
#endif
#if HAL_USE_WIFI
    _init_wifi();
#endif
#if HAL_USE_RADIO
    _init_radio();
#endif
#if HAL_USE_GPS
    _init_gps();
#endif
    _init_mesh();
}

#if HAL_USE_RADIO
void HalCardputer::_init_radio()
{
    ESP_LOGI(TAG, "init radio");

    SX1262Pins pins = {
        .spi_host = LoRa::SPI_HOST,
        .sck = LoRa::PIN_SCK,
        .mosi = LoRa::PIN_MOSI,
        .miso = LoRa::PIN_MISO,
        .cs = LoRa::PIN_CS,
        .rst = LoRa::PIN_RST,
        .busy = LoRa::PIN_BUSY,
        .dio1 = LoRa::PIN_DIO1,
        .rxen = LoRa::PIN_RXEN,
        .txen = LoRa::PIN_TXEN,
    };

    _radio = new SX1262(pins);
    if (!_radio->init())
    {
        ESP_LOGE(TAG, "Failed to initialize radio");
        delete _radio;
        _radio = nullptr;
    }
}
#endif

#if HAL_USE_GPS
void HalCardputer::_init_gps()
{
    ESP_LOGI(TAG, "init gps");

    _gps = new GPS(Gps::PIN_RX, Gps::PIN_TX, Gps::UART_NUM, Gps::BAUD_RATE);
    if (!_gps->init())
    {
        ESP_LOGE(TAG, "Failed to initialize GPS");
        delete _gps;
        _gps = nullptr;
    }
}
#endif

void HalCardputer::_init_mesh()
{
    ESP_LOGI(TAG, "init mesh");

    // Create node database
    _nodedb = new Mesh::NodeDB();

    // Create mesh service
    _mesh = new Mesh::MeshService(this);
}

HalCardputer::~HalCardputer()
{
    if (_mesh)
    {
        delete _mesh;
        _mesh = nullptr;
    }
    if (_nodedb)
    {
        delete _nodedb;
        _nodedb = nullptr;
    }
#if HAL_USE_GPS
    if (_gps)
    {
        delete _gps;
        _gps = nullptr;
    }
#endif
#if HAL_USE_RADIO
    if (_radio)
    {
        delete _radio;
        _radio = nullptr;
    }
#endif
}

#if 0
void Hal::loadMeshConfigFromSettings(Mesh::MeshConfig& cfg)
{
    // Node info
    std::string short_name = _settings->getString("nodeinfo", "short_name");
    std::string long_name = _settings->getString("nodeinfo", "long_name");
    if (!short_name.empty())
    {
        strncpy(cfg.short_name, short_name.c_str(), 4);
        cfg.short_name[4] = '\0';
    }
    if (!long_name.empty())
    {
        strncpy(cfg.long_name, long_name.c_str(), sizeof(cfg.long_name) - 1);
        cfg.long_name[sizeof(cfg.long_name) - 1] = '\0';
    }
    cfg.is_unmessagable = _settings->getBool("nodeinfo", "unmessagable");
    cfg.role = roleFromName(_settings->getString("nodeinfo", "role"));
    cfg.rebroadcast_mode = rebroadcastModeFromName(_settings->getString("nodeinfo", "rebroadcast"));

    // LoRa
    cfg.lora_config.region = Mesh::MeshService::regionCodeFromName(_settings->getString("lora", "region"));
    cfg.lora_config.tx_power = _settings->getNumber("lora", "tx_power");
    cfg.lora_config.hop_limit = _settings->getNumber("lora", "hop_limit");
    cfg.lora_config.channel_num = _settings->getNumber("lora", "freq_slot");
    int32_t freq_ovr_khz = _settings->getNumber("lora", "freq_ovr");
    cfg.lora_config.override_frequency = (freq_ovr_khz > 0) ? (float)freq_ovr_khz / 1000.0f : 0.0f;

    // Security keys (decode from base64, leave unchanged if missing/invalid)
    std::string priv_b64 = _settings->getString("security", "private_key");
    std::string pub_b64 = _settings->getString("security", "public_key");
    if (!priv_b64.empty() && !pub_b64.empty())
    {
        size_t priv_len = 0, pub_len = 0;
        mbedtls_base64_decode(cfg.private_key, 32, &priv_len, (const unsigned char*)priv_b64.c_str(), priv_b64.size());
        mbedtls_base64_decode(cfg.public_key, 32, &pub_len, (const unsigned char*)pub_b64.c_str(), pub_b64.size());
        cfg.public_key_len = (priv_len == 32 && pub_len == 32) ? 32 : 0;
    }

    // Position
    cfg.fixed_position = (_settings->getString("position", "location") == "fixed");
    cfg.fixed_latitude = _settings->getNumber("position", "latitude");
    cfg.fixed_longitude = _settings->getNumber("position", "longitude");
    cfg.fixed_altitude = _settings->getNumber("position", "altitude");

    // Position flags
    uint32_t pf = 0;
    if (_settings->getBool("position", "pos_alt"))
        pf |= meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE;
    if (_settings->getBool("position", "pos_sats"))
        pf |= meshtastic_Config_PositionConfig_PositionFlags_SATINVIEW;
    if (_settings->getBool("position", "pos_seq"))
        pf |= meshtastic_Config_PositionConfig_PositionFlags_SEQ_NO;
    if (_settings->getBool("position", "pos_time"))
        pf |= meshtastic_Config_PositionConfig_PositionFlags_TIMESTAMP;
    if (_settings->getBool("position", "pos_heading"))
        pf |= meshtastic_Config_PositionConfig_PositionFlags_HEADING;
    if (_settings->getBool("position", "pos_speed"))
        pf |= meshtastic_Config_PositionConfig_PositionFlags_SPEED;
    cfg.position_flags = pf;

    // Device telemetry field flags
    cfg.telemetry_bat_level = _settings->getBool("devmetrics", "bat_level");
    cfg.telemetry_voltage = _settings->getBool("devmetrics", "voltage");
    cfg.telemetry_ch_util = _settings->getBool("devmetrics", "ch_util");
    cfg.telemetry_air_util = _settings->getBool("devmetrics", "air_util");
    cfg.telemetry_uptime = _settings->getBool("devmetrics", "uptime");

    // Broadcast intervals
    cfg.nodeinfo_broadcast_interval_ms = parseIntervalToMs(_settings->getString("nodeinfo", "bcast_int"));
    cfg.position_broadcast_interval_ms = parseIntervalToMs(_settings->getString("position", "bcast_int"));
    cfg.telemetry_broadcast_interval_ms = parseIntervalToMs(_settings->getString("devmetrics", "bcast_int"));
}
#endif

bool HalCardputer::startMesh()
{
#if HAL_USE_RADIO
    if (!_radio)
    {
        ESP_LOGE(TAG, "Radio not initialized");
        return false;
    }
#endif

    if (!_mesh || !_nodedb)
    {
        ESP_LOGE(TAG, "Mesh service not initialized");
        return false;
    }

    Mesh::MeshConfig mesh_config = {};
    _mesh->loadConfigFromSettings(mesh_config);

    _nodedb->loadChannels();
    bool found_primary = false;
    for (uint8_t i = 0; i < 8; i++)
    {
        auto* ch = _nodedb->getChannel(i);
        if (ch && ch->role == meshtastic_Channel_Role_PRIMARY && ch->has_settings)
        {
            mesh_config.primary_channel = *ch;
            found_primary = true;
            break;
        }
    }
    if (!found_primary)
    {
        ESP_LOGI(TAG, "No primary channel in storage, applying defaults");
        mesh_config.primary_channel.index = 0;
        mesh_config.primary_channel.role = meshtastic_Channel_Role_PRIMARY;
        mesh_config.primary_channel.has_settings = true;
        strcpy(mesh_config.primary_channel.settings.name, "LongFast");
        size_t psk_len = 0;
        if (decodeBase64(DEFAULT_CHANNEL_PSK_B64,
                         mesh_config.primary_channel.settings.psk.bytes,
                         sizeof(mesh_config.primary_channel.settings.psk.bytes),
                         &psk_len))
        {
            mesh_config.primary_channel.settings.psk.size = psk_len;
        }
        _nodedb->setChannel(0, mesh_config.primary_channel);
        _nodedb->saveChannels();
    }

#if HAL_USE_RADIO
    if (!_mesh->init(_radio, _nodedb, mesh_config))
#else
    if (!_mesh->init(nullptr, _nodedb, mesh_config))
#endif
    {
        ESP_LOGE(TAG, "Failed to initialize mesh service");
        return false;
    }

#if HAL_USE_GPS
    _mesh->setGps(_gps);
#else
    _mesh->setGps(nullptr);
#endif

#if HAL_USE_BAT
    _mesh->setBatteryCallback(
        [this]() -> Mesh::BatteryInfo
        {
            Mesh::BatteryInfo info = {};
            if (_battery)
            {
                info.voltage = _battery->get_voltage();
                info.level = _battery->get_level(info.voltage);
                if (info.voltage > 4.2f)
                {
                    info.level = 101;
                }
            }
            return info;
        });
#endif

    _nodedb->init(_mesh->getNodeId());

    Mesh::MeshDataStore::getInstance().init();

    if (!_mesh->start())
    {
        ESP_LOGE(TAG, "Failed to start mesh service");
        return false;
    }

#if HAL_USE_SPEAKER
    _mesh->setConnectionCallback(
        [this](bool connected)
        {
            if (connected)
            {
                playDeviceConnectedSound();
            }
            else
            {
                playDeviceDisconnectedSound();
            }
        });

    _mesh->setMessageCallback(
        [this](const meshtastic_MeshPacket& packet)
        {
            uint32_t notif = 1;
            if (_nodedb)
            {
                auto* ch = _nodedb->getChannel(packet.channel);
                if (ch && ch->has_settings)
                    notif = ch->settings.channel_num;
            }
            if (notif != 0)
                playNotificationSound(notif);
        });
#endif

    ESP_LOGI(TAG, "Mesh service started successfully");
    return true;
}

void HalCardputer::updateMesh()
{
    if (_mesh)
    {
        _mesh->update();
    }
    if (_nodedb)
    {
        _nodedb->checkSave();
    }
}

#if HAL_USE_BAT
uint8_t HalCardputer::getBatLevel(float voltage)
{
    uint8_t result = 0;
    if (voltage >= 4.12)
        result = 100;
    else if (voltage >= 3.88)
        result = 75;
    else if (voltage >= 3.61)
        result = 50;
    else if (voltage >= 3.40)
        result = 25;
    else
        result = 0;
    return result;
}

float HalCardputer::getBatVoltage() { return static_cast<float>(_battery->get_voltage()); }
#endif

void HalCardputer::reboot()
{
    ESP_LOGW(TAG, "Rebooting...");
#if HAL_USE_WIFI
    wifi()->set_status_callback(nullptr);
#endif
    delay(100);
#if HAL_USE_LED
    led()->off();
#endif
    esp_restart();
}
