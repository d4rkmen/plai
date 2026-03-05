/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)

Author:
 [lovyan03](https://twitter.com/lovyan03)

Contributors:
 [ciniml](https://github.com/ciniml)
 [mongonta0716](https://github.com/mongonta0716)
 [tobozo](https://github.com/tobozo)
 [d4rkmen](https://github.com/d4rkmen)
/----------------------------------------------------------------------------*/
#pragma once

#include "lgfx/v1/panel/Panel_Device.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>

namespace lgfx
{
    inline namespace v1
    {
        struct Panel_ST7789V2 : public Panel_Device
        {
            Panel_ST7789V2(void)
            {
                _cfg.panel_width = _cfg.memory_width = 240;
                _cfg.panel_height = _cfg.memory_height = 320;
                _cfg.invert = true;
                _cfg.rgb_order = false;

                _cfg.dummy_read_bits = 8;

                // init command list for 16 bit color depth
                _write_depth = color_depth_t::rgb565_2Byte;
                // reading is always 18 bit color depth
                _read_depth = color_depth_t::rgb666_3Byte;
            }

        public:
            bool init(bool use_reset) override;
            void beginTransaction(void) override;
            void endTransaction(void) override;

            color_depth_t setColorDepth(color_depth_t depth) override;
            void setRotation(uint_fast8_t r) override;
            void setInvert(bool invert) override;
            void setSleep(bool flg) override;
            void setPowerSave(bool flg) override;

            void waitDisplay(void) override { waitForTE(); }
            bool displayBusy(void) override { return false; }

            bool isTEEnabled(void) const { return _te_enabled; }

            void writePixels(pixelcopy_t* param, uint32_t len, bool use_dma) override;
            void writeBlock(uint32_t rawcolor, uint32_t len) override;

            void setWindow(uint_fast16_t xs, uint_fast16_t ys, uint_fast16_t xe, uint_fast16_t ye) override;
            void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y, uint32_t rawcolor) override;
            void writeFillRectPreclipped(
                uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, uint32_t rawcolor) override;
            void writeImage(
                uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, pixelcopy_t* param, bool use_dma) override;

            uint32_t readCommand(uint_fast16_t cmd, uint_fast8_t index, uint_fast8_t len) override;
            uint32_t readData(uint_fast8_t index, uint_fast8_t len) override;
            void readRect(
                uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, void* dst, pixelcopy_t* param) override;

            int32_t getScanLine(void) override;

            void command_list(const uint8_t* addr);

        protected:
            uint16_t _colstart = 0;
            uint16_t _rowstart = 0;
            bool _te_enabled = false;
            SemaphoreHandle_t _te_semaphore = nullptr;

            // TE interrupt handling
            static void IRAM_ATTR te_isr_handler(void* arg);
            void setupTEInterrupt(void);
            void cleanupTEInterrupt(void);
            bool waitForTE(uint32_t timeout_ms = 50);

            enum mad_t
            {
                MAD_MY = 0x80,    // MY Row Address Order (MY) ‘1’ = Decrement, (Bottom to Top, when MADCTL (36h) D7=’1’)
                                  // ‘0’ = Increment, (Top to Bottom, when MADCTL(36h) D7 =’0’)
                MAD_MX = 0x40,    // MX Column Address Order (MX) ‘1’ = Decrement, (Right to Left, when MADCTL (36h) D6=’1’)
                                  // ‘0’ = Increment, (Left to Right, when MADCTL (36h) D6=’ 0 ’)
                MAD_MV = 0x20,    // MV Row/Column Exchange (MV) ‘1’ = Row/column exchange, (when MADCTL (36h) D5=’1’)
                                  // ‘0’ = Normal, (when MADCTL (36h) D5=’0’)
                MAD_ML = 0x10,    // ML Scan Address Order (ML) ‘0’ =Decrement, (LCD refresh Top to Bottom, when MADCTL(36h) D4
                                  // =’0’) ‘1’ = Increment, (LCD refresh Bottom to Top, when MADCTL(36h) D4=’1’)
                MAD_BGR = 0x08,   // RGB RGB / BGR Order(RGB) ‘1’ = BGR, (When MADCTL(36h) D3 =’1’)
                                  // ‘0’ = RGB, (When MADCTL(36h) D3 =’0’)
                MAD_MH = 0x04,    // MH Horizontal Order ‘0’ =Decrement, (LCD refresh Left to Right, when MADCTL(36h) D2 =’0’)
                                  // ‘1’ = Increment, (LCD refresh Right to Left, when MADCTL(36h) D2 =’1’)
                MAD_HSD = 0x02,   // Horizontal Scroll Address Order, used in HSCRDEF (43h): Horizontal Scrolling Definition
                MAD_DUMMY = 0x01, // 0
                MAD_RGB = 0x00    // Use default RGB order
            };

            enum colmod_t
            {
                RGB565_2BYTE = 0x55,
                RGB666_3BYTE = 0x66,
            };

            virtual uint8_t getMadCtl(uint8_t r) const
            {
                static constexpr uint8_t madctl_table[] = {
                    0,
                    MAD_MV | MAD_MX | MAD_MH,
                    MAD_MX | MAD_MH | MAD_MY | MAD_ML,
                    MAD_MV | MAD_MY | MAD_ML,
                    MAD_MY | MAD_ML,
                    MAD_MV,
                    MAD_MX | MAD_MH,
                    MAD_MV | MAD_MX | MAD_MY | MAD_MH | MAD_ML,
                };
                return madctl_table[r];
            }
            // System Commands
            static constexpr uint8_t CMD_NOP = 0x00;       // No Operation
            static constexpr uint8_t CMD_SWRESET = 0x01;   // Software Reset
            static constexpr uint8_t CMD_RDDID = 0x04;     // Read Display ID
            static constexpr uint8_t CMD_RDDST = 0x09;     // Read Display Status
            static constexpr uint8_t CMD_RDDPM = 0x0A;     // Read Display Power Mode
            static constexpr uint8_t CMD_RDDMADCTL = 0x0B; // Read Display MADCTL
            static constexpr uint8_t CMD_RDDCOLMOD = 0x0C; // Read Display Pixel Format
            static constexpr uint8_t CMD_RDDIM = 0x0D;     // Read Display Image Mode
            static constexpr uint8_t CMD_RDDSM = 0x0E;     // Read Display Signal Mode
            static constexpr uint8_t CMD_RDDSDR = 0x0F;    // Read display self - diagnostic result

            // Sleep Commands
            static constexpr uint8_t CMD_SLPIN = 0x10;  // Sleep In
            static constexpr uint8_t CMD_SLPOUT = 0x11; // Sleep Out
            static constexpr uint8_t CMD_PTLON = 0x12;  // Partial mode On
            static constexpr uint8_t CMD_NORON = 0x13;  // Normal On (Partial Off)

            // Display Inversion Commands
            static constexpr uint8_t CMD_INVOFF = 0x20; // Display Inversion Off
            static constexpr uint8_t CMD_INVON = 0x21;  // Display Inversion On

            // Gamma Commands
            static constexpr uint8_t CMD_GAMSET = 0x21; // Gamma Set
            // Display On/Off Commands
            static constexpr uint8_t CMD_DISPOFF = 0x28; // Display Off
            static constexpr uint8_t CMD_DISPON = 0x29;  // Display On

            // Address Commands
            static constexpr uint8_t CMD_CASET = 0x2A; // Column Address Set
            static constexpr uint8_t CMD_RASET = 0x2B; // Row Address Set

            // Memory Commands
            static constexpr uint8_t CMD_RAMWR = 0x2C; // Memory Write
            static constexpr uint8_t CMD_RAMRD = 0x2E; // Memory Read

            // Partial mode Commands
            static constexpr uint8_t CMD_PTLAR = 0x30; // Partial start/end address set
            // Scrolling Commands
            static constexpr uint8_t CMD_VSCRDEF = 0x33;  // Vertical Scrolling Definition
            static constexpr uint8_t CMD_VSCRSADD = 0x37; // Vertical scrolling start address

            // Tearing Effect Commands
            static constexpr uint8_t CMD_TEOFF = 0x34; // Tearing Effect Line OFF
            static constexpr uint8_t CMD_TEON = 0x35;  // Tearing Effect Line On

            // Control Commands
            static constexpr uint8_t CMD_MADCTL = 0x36; // Memory Data Access Control
            static constexpr uint8_t CMD_IDMOFF = 0x38; // Idle Mode Off
            static constexpr uint8_t CMD_IDMON = 0x39;  // Idle Mode On
            static constexpr uint8_t CMD_COLMOD = 0x3A; // Interface Pixel Format

            // Memory Continue Commands
            static constexpr uint8_t CMD_WRMEMC = 0x3C; // Write Memory Continue
            static constexpr uint8_t CMD_RDMEMC = 0x3E; // Read Memory Continue

            // Horizontal Scrolling Commands
            static constexpr uint8_t CMD_HSCRDEF = 0x43; // Horizontal Scrolling Definition
            static constexpr uint8_t CMD_HSCRSAD = 0x47; // Horizontal Scroll Start Address

            // Display Control Commands
            static constexpr uint8_t CMD_TESCAN = 0x44;   // Write Tear Scan Line
            static constexpr uint8_t CMD_RDTESCAN = 0x45; // Read Tear Scan Line

            // Brightness/CTRL Commands
            static constexpr uint8_t CMD_WRDISBV = 0x51; // Write Display Brightness
            static constexpr uint8_t CMD_RDDISBV = 0x52; // Read Display Brightness
            static constexpr uint8_t CMD_WRCTRLD = 0x53; // Write CTRL Display
            static constexpr uint8_t CMD_RDCTRLD = 0x54; // Read CTRL Display
            static constexpr uint8_t CMD_WRCACE = 0x55;  // Write content adaptive brightness control and Color enhancemnet
            // Compress Commands
            static constexpr uint8_t CMD_RDCABC = 0x56;   // Read content adaptive brightness control and Color enhancemnet
            static constexpr uint8_t CMD_WRCABCMB = 0x5E; // Write CABC minimum brightness
            static constexpr uint8_t CMD_RDCABCMB = 0x5F; // Read CABC minimum brightness
            static constexpr uint8_t CMD_RDABCSDR = 0x68; // Read Automatic Brightness Control Self - Diagnostic Result

            // ID Read Commands
            static constexpr uint8_t CMD_RDID1 = 0xDA; // Read ID1
            static constexpr uint8_t CMD_RDID2 = 0xDB; // Read ID2
            static constexpr uint8_t CMD_RDID3 = 0xDC; // Read ID3

            // Command table 2

            static constexpr uint8_t CMD_RAMCTRL = 0xB0;    // RAM Control
            static constexpr uint8_t CMD_RGBCTRL = 0xB1;    // RGB Control
            static constexpr uint8_t CMD_PORCTRL = 0xB2;    // Porch control
            static constexpr uint8_t CMD_FRCTRL1 = 0xB3;    // Frame rate control 1
            static constexpr uint8_t CMD_PARCTRL = 0xB5;    // Partial control
            static constexpr uint8_t CMD_GCTRL = 0xB7;      // Gate control
            static constexpr uint8_t CMD_GTADJ = 0xB8;      // Gate on timing adjustment
            static constexpr uint8_t CMD_DGMEN = 0xBA;      // Digital Gamma Enable
            static constexpr uint8_t CMD_VCOMS = 0xBB;      // VCOM setting
            static constexpr uint8_t CMD_POWSAVE = 0xBC;    // Power saving mode
            static constexpr uint8_t CMD_DLPOFFSAVE = 0xBD; // Display off power save
            static constexpr uint8_t CMD_LCMCTRL = 0xC0;    // LCM control
            static constexpr uint8_t CMD_IDSET = 0xC1;      // ID setting
            static constexpr uint8_t CMD_VDVVRHEN = 0xC2;   // VDV and VRH Command Enable
            static constexpr uint8_t CMD_VRHS = 0xC3;       // VRH Set
            static constexpr uint8_t CMD_VDVSET = 0xC4;     // VDV Setting
            static constexpr uint8_t CMD_VCMOFSET = 0xC5;   // VCOM Offset Set
            static constexpr uint8_t CMD_FRCTR2 = 0xC6;     // Frame rate control 2
            static constexpr uint8_t CMD_CABCCTRL = 0xC7;   // CABC control
            static constexpr uint8_t CMD_REGSEL1 = 0xC8;    // Register select 1
            static constexpr uint8_t CMD_REGSEL2 = 0xCA;    // Register select 2
            static constexpr uint8_t CMD_PWMFRSEL = 0xCC;   // PWM Frequency Selection
            static constexpr uint8_t CMD_PWCTRL1 = 0xD0;    // Power control 1
            static constexpr uint8_t CMD_VAPVANEN = 0xD2;   // Enable VAP / VAN signal output
            static constexpr uint8_t CMD_CMD2EN = 0xDF;     // Command 2 enable (param 5A 69 02)
            static constexpr uint8_t CMD_PVGAMCTRL = 0xE0;  // Positive Voltage Gamma Control
            static constexpr uint8_t CMD_NVGAMCTRL = 0xE1;  // Negative Voltage Gamma Control
            static constexpr uint8_t CMD_DGMLUTR = 0xE2;    // Digital Gamma LUT Red
            static constexpr uint8_t CMD_DGMLUTB = 0xE3;    // Digital Gamma LUT Blue
            static constexpr uint8_t CMD_GATECTRL = 0xE4;   // Gate control
            static constexpr uint8_t CMD_SPI2EN = 0xE7;     // SPI2 enable
            static constexpr uint8_t CMD_PWCTRL2 = 0xE8;    // Power control 2
            static constexpr uint8_t CMD_EQCTRL = 0xE9;     // Equalize time control
            static constexpr uint8_t CMD_PROMCTRL = 0xEC;   // Program control
            static constexpr uint8_t CMD_PROMEN = 0xFA;     // Program mode enable
            static constexpr uint8_t CMD_NVMSET = 0xFC;     // NVM setting
            static constexpr uint8_t CMD_PROMACT = 0xFE;    // Program action

            uint32_t read_bits(uint_fast8_t bit_index, uint_fast8_t bit_len);

            void writeCommand(uint8_t data);
            void writeBytes(const uint8_t* data, uint32_t len, bool use_dma);
            virtual void update_madctl(void);
            virtual uint8_t getColMod(uint8_t bpp) const { return (bpp > 16) ? RGB666_3BYTE : RGB565_2BYTE; }

            virtual void setColorDepth_impl(color_depth_t depth)
            {
                _write_depth = ((int)depth & color_depth_t::bit_mask) > 16 ? rgb666_3Byte : rgb565_2Byte;
                _read_depth = rgb888_3Byte;
            }
            const uint8_t* getInitCommands(uint8_t listno) const override
            {
                static constexpr uint8_t list0[] = {
                    CMD_PORCTRL,
                    5,
                    0x0c,
                    0x0c,
                    0x00,
                    0x33,
                    0x33,
                    CMD_GCTRL,
                    1,
                    0x35,
                    CMD_VCOMS,
                    1,
                    0x28, // JLX240 display datasheet
                    CMD_LCMCTRL,
                    1,
                    0x0C,
                    CMD_VDVVRHEN,
                    2,
                    0x01,
                    0xFF,
                    CMD_VRHS,
                    1,
                    0x10, // voltage VRHS
                    CMD_VDVSET,
                    1,
                    0x20,
                    CMD_FRCTR2,
                    1,
                    0x0f, // 0x0f=60Hz
                    CMD_PWCTRL1,
                    2,
                    0xa4,
                    0xa1,
                    CMD_RAMCTRL,
                    2,
                    0x00,
                    0xC0,
                    //--------------------------------ST7789V gamma setting---------------------------------------//
                    CMD_PVGAMCTRL,
                    14,
                    0xd0,
                    0x00,
                    0x02,
                    0x07,
                    0x0a,
                    0x28,
                    0x32,
                    0x44,
                    0x42,
                    0x06,
                    0x0e,
                    0x12,
                    0x14,
                    0x17,
                    CMD_NVGAMCTRL,
                    14,
                    0xd0,
                    0x00,
                    0x02,
                    0x07,
                    0x0a,
                    0x28,
                    0x31,
                    0x54,
                    0x47,
                    0x0e,
                    0x1c,
                    0x17,
                    0x1b,
                    0x1e,
                    CMD_SLPOUT,
                    0 + CMD_INIT_DELAY,
                    130, // Exit sleep mode
                    CMD_IDMOFF,
                    0,
                    CMD_DISPON,
                    0,
                    0xFF, // end
                    0xFF,
                };
                switch (listno)
                {
                case 0:
                    return list0;
                default:
                    return nullptr;
                }
            };
        };
    }; // namespace v1
    //----------------------------------------------------------------------------
} // namespace lgfx
