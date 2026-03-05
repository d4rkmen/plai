/**
 * @file hal.h
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-09-18
 *
 * @copyright Copyright (c) 2023
 *
 */
#pragma once
#include "hal_config.h"
#include "board.h"
#include "LovyanGFX.h"
#include "settings/settings.h"
#include <iostream>
#include <string>

#if HAL_USE_I2C
#include "i2c/i2c_master.h"
#endif
#if HAL_USE_KEYBOARD
#include "keyboard/keyboard.h"
#endif
#if HAL_USE_BAT
#include "bat/battery.h"
#endif
#if HAL_USE_SDCARD
#include "sdcard/sdcard.h"
#endif
#if HAL_USE_BUTTON
#include "button/button.h"
#endif
#if HAL_USE_SPEAKER
#include "speaker/speaker.h"
#endif
#if HAL_USE_USB
#include "usb/usb.h"
#endif
#if HAL_USE_WIFI
#include "wifi/wifi.h"
#endif
#if HAL_USE_LED
#include "led/led.h"
#endif
#if HAL_USE_RADIO
#include "radio/radio_interface.h"
#endif
#if HAL_USE_GPS
#include "gps/gps.h"
#endif

// Forward declarations
namespace Mesh
{
    struct MeshConfig;
    class MeshService;
    class NodeDB;
} // namespace Mesh

namespace HAL
{
    /**
     * @brief Hal base class
     *
     */
    class Hal
    {
    protected:
        SETTINGS::Settings* _settings;
        Mesh::MeshService* _mesh;
        Mesh::NodeDB* _nodedb;
        BoardType _board_type;

#if HAL_USE_DISPLAY
        LGFX_Device* _display;
        LGFX_Sprite* _canvas;
        LGFX_Sprite* _canvas_system_bar;
#endif
#if HAL_USE_KEYBOARD
        KEYBOARD::Keyboard* _keyboard;
#endif
#if HAL_USE_I2C
        I2CMaster* _i2c;
#endif
#if HAL_USE_BAT
        Battery* _battery;
#endif
#if HAL_USE_SPEAKER
        Speaker* _speaker;
#endif
#if HAL_USE_BUTTON
        Button* _homeButton;
#endif
#if HAL_USE_SDCARD
        SDCard* _sdcard;
#endif
#if HAL_USE_USB
        USB* _usb;
#endif
#if HAL_USE_WIFI
        WiFi* _wifi;
#endif
#if HAL_USE_LED
        LED* _led;
#endif
#if HAL_USE_RADIO
        RadioInterface* _radio;
#endif
#if HAL_USE_GPS
        GPS* _gps;
        bool _gps_adjusted;
#endif

    public:
        Hal(SETTINGS::Settings* settings)
            : _settings(settings), _mesh(nullptr), _nodedb(nullptr), _board_type(BoardType::AUTO_DETECT)
#if HAL_USE_KEYBOARD
              ,
              _keyboard(nullptr)
#endif
#if HAL_USE_I2C
              ,
              _i2c(nullptr)
#endif
#if HAL_USE_BAT
              ,
              _battery(nullptr)
#endif
#if HAL_USE_SPEAKER
              ,
              _speaker(nullptr)
#endif
#if HAL_USE_BUTTON
              ,
              _homeButton(nullptr)
#endif
#if HAL_USE_SDCARD
              ,
              _sdcard(nullptr)
#endif
#if HAL_USE_USB
              ,
              _usb(nullptr)
#endif
#if HAL_USE_WIFI
              ,
              _wifi(nullptr)
#endif
#if HAL_USE_LED
              ,
              _led(nullptr)
#endif
#if HAL_USE_RADIO
              ,
              _radio(nullptr)
#endif
#if HAL_USE_GPS
              ,
              _gps(nullptr), _gps_adjusted(false)
#endif
        {
        }

        // Getters
        inline SETTINGS::Settings* settings() { return _settings; }
        inline Mesh::MeshService* mesh() { return _mesh; }
        inline Mesh::NodeDB* nodedb() { return _nodedb; }
        inline BoardType board_type() const { return _board_type; }

#if HAL_USE_DISPLAY
        inline LGFX_Device* display() { return _display; }
        inline LGFX_Sprite* canvas() { return _canvas; }
        inline LGFX_Sprite* canvas_system_bar() { return _canvas_system_bar; }
        inline void canvas_system_bar_update() { _canvas_system_bar->pushSprite(0, 0); }
        inline void canvas_update() { _canvas->pushSprite(0, _canvas_system_bar->height()); }
#endif
#if HAL_USE_KEYBOARD
        inline KEYBOARD::Keyboard* keyboard() { return _keyboard; }
#endif
#if HAL_USE_I2C
        inline I2CMaster* i2c() { return _i2c; }
#endif
#if HAL_USE_BAT
        inline Battery* bat() { return _battery; }
#endif
#if HAL_USE_SPEAKER
        inline Speaker* speaker() { return _speaker; }
#endif
#if HAL_USE_BUTTON
        inline Button* home_button() { return _homeButton; }
#endif
#if HAL_USE_SDCARD
        inline SDCard* sdcard() { return _sdcard; }
#endif
#if HAL_USE_USB
        inline USB* usb() { return _usb; }
#endif
#if HAL_USE_WIFI
        inline WiFi* wifi() { return _wifi; }
#endif
#if HAL_USE_LED
        inline LED* led() { return _led; }
#endif
#if HAL_USE_RADIO
        inline RadioInterface* radio() { return _radio; }
#endif
#if HAL_USE_GPS
        inline GPS* gps() { return _gps; }
        inline void setGPSAdjusted(bool isAdjusted) { _gps_adjusted = isAdjusted; }
        inline bool isGPSAdjusted(void) { return _gps_adjusted; }
#endif

        // Override
        virtual std::string type() { return "null"; }
        virtual void init() {}

#if HAL_USE_SPEAKER
        enum class NotificationSound : uint32_t
        {
            NONE = 0,    // silent
            DEFAULT = 1, // default beep
            MORSE = 2,
            SEAGULL = 3,
            TUM_TUM = 4,
            PUM_PAM = 5,
            EIGHT_BIT = 6,
            KNOCK = 7,
            GPS = 8,
        };

        virtual void playLastSound() {}
        virtual void playNextSound() {}
        virtual void playKeyboardSound() {}
        virtual void playErrorSound() {}
        virtual void playDeviceConnectedSound() {}
        virtual void playDeviceDisconnectedSound() {}
        virtual void playMessageSound() {}
        virtual void playNotificationSound(uint32_t index) { playMessageSound(); }
        virtual void playNotificationSound(NotificationSound sound) { playNotificationSound(static_cast<uint32_t>(sound)); }
        virtual void playMessageSentSound() {}
#endif

#if HAL_USE_BAT
        virtual uint8_t getBatLevel(float voltage) { return 100; }
        virtual float getBatVoltage() { return 4.15; }
#endif
        virtual void reboot() {}
        virtual void updateMesh() {}
    };
} // namespace HAL
