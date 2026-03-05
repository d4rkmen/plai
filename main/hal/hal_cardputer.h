/**
 * @file hal_cardputer.h
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-09-22
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "hal.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define delay(ms) vTaskDelay(pdMS_TO_TICKS(ms))

#define RGB_LED_GPIO 21

#if HAL_USE_SPEAKER
extern const uint8_t usb_connected_wav_start[] asm("_binary_usb_connected_wav_start");
extern const uint8_t usb_connected_wav_end[] asm("_binary_usb_connected_wav_end");
extern const uint8_t usb_disconnected_wav_start[] asm("_binary_usb_disconnected_wav_start");
extern const uint8_t usb_disconnected_wav_end[] asm("_binary_usb_disconnected_wav_end");
extern const uint8_t error_wav_start[] asm("_binary_error_wav_start");
extern const uint8_t error_wav_end[] asm("_binary_error_wav_end");
#endif

namespace HAL
{
    class HalCardputer : public Hal
    {
    private:
#if HAL_USE_I2C
        void _init_i2c();
#endif
#if HAL_USE_DISPLAY
        void _init_display();
#endif
#if HAL_USE_KEYBOARD
        void _init_keyboard();
#endif
#if HAL_USE_SPEAKER
        void _init_speaker();
#endif
#if HAL_USE_BUTTON
        void _init_button();
#endif
#if HAL_USE_BAT
        void _init_bat();
#endif
#if HAL_USE_SDCARD
        void _init_sdcard();
#endif
#if HAL_USE_USB
        void _init_usb();
#endif
#if HAL_USE_WIFI
        void _init_wifi();
#endif
#if HAL_USE_LED
        void _init_led();
#endif
#if HAL_USE_RADIO
        void _init_radio();
#endif
#if HAL_USE_GPS
        void _init_gps();
#endif
        void _init_mesh();

    public:
        HalCardputer(SETTINGS::Settings* settings) : Hal(settings) {}
        ~HalCardputer();

        std::string type() override
        {
            switch (_board_type)
            {
            case HAL::BoardType::CARDPUTER:
                return "v1.x";
            case HAL::BoardType::CARDPUTER_ADV:
                return "ADV";
            default:
                return "unknown";
            }
        }
        void init() override;

        /**
         * @brief Start the mesh service (call after init)
         * @return true on success
         */
        bool startMesh();

        /**
         * @brief Update mesh service (call from main loop)
         */
        void updateMesh() override;

#if HAL_USE_SPEAKER
        void playErrorSound() override { _speaker->playWav(error_wav_start, error_wav_end - error_wav_start); }
        void playKeyboardSound() override { _speaker->tone(5000, 20); }
        void playLastSound() override { _speaker->tone(6000, 20); }
        void playNextSound() override { _speaker->tone(7000, 20); }
        void playMessageSound() override
        {
            _speaker->tone(1633, 60);
            delay(50);
            _speaker->tone(1209, 60);
        }

        void playNotificationSound(uint32_t index) override;

        void playMessageSentSound() override
        {
            _speaker->tone(616, 60);
            delay(60);
            _speaker->tone(616, 60);
        }

        void playDeviceConnectedSound() override
        {
            _speaker->playWav(usb_connected_wav_start, usb_connected_wav_end - usb_connected_wav_start);
        }
        void playDeviceDisconnectedSound() override
        {
            _speaker->playWav(usb_disconnected_wav_start, usb_disconnected_wav_end - usb_disconnected_wav_start);
        }
#endif
#if HAL_USE_BAT
        uint8_t getBatLevel(float voltage) override;
        float getBatVoltage() override;
#endif
        void reboot() override;
    };
} // namespace HAL
