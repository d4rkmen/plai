#pragma once

#define LGFX_USE_V1

#include <LovyanGFX.hpp>
#include "Panel_ST7789V2.hpp"
#include "Bus_IDFSPI.hpp"

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789V2 _panel_instance;
    lgfx::Bus_IDFSPI _bus_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_CST816S _touch_instance;

public:
    LGFX(void)
    {
        {
            auto cfg = _bus_instance.config();
            // SPI2 used by SD card, using free SPI3 for display
            cfg.spi_host = SPI3_HOST;
            cfg.freq_write = 80 * 1000 * 1000;
            cfg.freq_read = 4 * 1000 * 1000;
            // * lock the bus for concurrent access
            // cfg.use_lock = true;
            cfg.pin_dc = 34;
            cfg.pin_cs = 37;
            cfg.pin_sclk = 36;
            cfg.pin_io0 = 35;
            cfg.spi_3wire = true;
            cfg.pin_io1 = -1;
            cfg.pin_io2 = -1;
            cfg.pin_io3 = -1;
            cfg.spi_mode = 0;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();

            // * use pin_busy as TE pin for tearing effect
            cfg.pin_rst = 33;
            cfg.panel_height = 240;
            cfg.panel_width = 135;
            cfg.offset_x = 53;
            cfg.offset_y = 40;
            cfg.bus_shared = false;

            _panel_instance.config(cfg);
        }
        {
            auto cfg = _light_instance.config();

            cfg.pin_bl = 38;
            cfg.invert = false;
            cfg.freq = 256;
            cfg.offset = 16;
            cfg.pwm_channel = 7;

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
        // initialize the display
        init();
        setBrightness(0);
        setRotation(1);
    }
};
