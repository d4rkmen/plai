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
/----------------------------------------------------------------------------*/
#include "Panel_ST7789V2.hpp"
#include "lgfx/v1/Bus.hpp"
#include "lgfx/v1/platforms/common.hpp"
#include "lgfx/v1/misc/pixelcopy.hpp"
#include <esp_log.h>

static const char* TAG = "ST7789V2";

namespace lgfx
{
    inline namespace v1
    {
        // TE (Tearing Effect) interrupt handler - called on falling edge of TE pin
        void IRAM_ATTR Panel_ST7789V2::te_isr_handler(void* arg)
        {
            Panel_ST7789V2* panel = static_cast<Panel_ST7789V2*>(arg);
            if (panel && panel->_te_semaphore)
            {
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                xSemaphoreGiveFromISR(panel->_te_semaphore, &xHigherPriorityTaskWoken);
                if (xHigherPriorityTaskWoken)
                {
                    portYIELD_FROM_ISR();
                }
            }
        }

        void Panel_ST7789V2::setupTEInterrupt(void)
        {
            if (_cfg.pin_busy < 0)
            {
                ESP_LOGD(TAG, "TE pin not configured, skipping TE setup");
                return;
            }

            // Create semaphore for TE synchronization
            _te_semaphore = xSemaphoreCreateBinary();
            if (!_te_semaphore)
            {
                ESP_LOGE(TAG, "Failed to create TE semaphore");
                return;
            }

            // Configure TE pin as input with interrupt on rising edge
            gpio_config_t io_conf = {};
            io_conf.intr_type = GPIO_INTR_POSEDGE; // Raising edge interrupt
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pin_bit_mask = (1ULL << _cfg.pin_busy);
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Enable pull-up

            esp_err_t ret = gpio_config(&io_conf);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to configure TE GPIO: %d", ret);
                vSemaphoreDelete(_te_semaphore);
                _te_semaphore = nullptr;
                return;
            }

            // Install GPIO ISR service if not already installed
            ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) // ESP_ERR_INVALID_STATE means already installed
            {
                ESP_LOGE(TAG, "Failed to install GPIO ISR service: %d", ret);
                vSemaphoreDelete(_te_semaphore);
                _te_semaphore = nullptr;
                return;
            }

            // Add ISR handler for TE pin
            ret = gpio_isr_handler_add((gpio_num_t)_cfg.pin_busy, te_isr_handler, this);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to add TE ISR handler: %d", ret);
                vSemaphoreDelete(_te_semaphore);
                _te_semaphore = nullptr;
                return;
            }

            _te_enabled = true;
            ESP_LOGI(TAG, "TE interrupt configured on pin %d (rising edge)", _cfg.pin_busy);
        }

        void Panel_ST7789V2::cleanupTEInterrupt(void)
        {
            if (_cfg.pin_busy != -1)
            {
                gpio_isr_handler_remove((gpio_num_t)_cfg.pin_busy);
            }
            if (_te_semaphore)
            {
                vSemaphoreDelete(_te_semaphore);
                _te_semaphore = nullptr;
            }
            _te_enabled = false;
        }

        bool Panel_ST7789V2::waitForTE(uint32_t timeout_ms)
        {
            if (_cfg.pin_busy == -1)
            {
                return true;
            }
            if (!_te_enabled || !_te_semaphore)
            {
                ESP_LOGW(TAG, "TE not configured, proceed immediately");
                return true; // TE not configured, proceed immediately
            }

            // Wait for TE signal (falling edge indicates start of vertical blanking)
            if (xSemaphoreTake(_te_semaphore, pdMS_TO_TICKS(timeout_ms)) == pdTRUE)
            {
                return true;
            }
            ESP_LOGW(TAG, "TE wait timeout");
            return false;
        }

        void Panel_ST7789V2::command_list(const uint8_t* addr)
        {
            startWrite();
            for (;;)
            { // For each command...
                uint8_t cmd = *addr++;
                uint8_t num = *addr++; // Number of args to follow
                if (cmd == 0xff && num == 0xff)
                    break;

                uint_fast8_t ms = num & CMD_INIT_DELAY; // If hibit set, delay follows args
                num &= ~CMD_INIT_DELAY;                 // Mask out delay bit
                if (num)
                {
                    // CS keep alive after command
                    _bus->writeCommand(cmd, 8);
                    _bus->writeBytes(addr, num, false, false);
                    addr += num;
                }
                else
                {
                    // CS release after command
                    _bus->writeCommand(cmd, 8);
                }

                _bus->wait();

                if (ms)
                {
                    ms = *addr++; // Read post-command delay time (ms)
                    delay(ms);
                }
            }
            endWrite();
        }

        bool Panel_ST7789V2::init(bool use_reset)
        {
            ESP_LOGD(TAG, "init");

            if (!Panel_Device::init(use_reset))
            {
                return false;
            }
            uint32_t id = readCommand(CMD_RDDID, 0, 3);
            ESP_LOGI(TAG, "init: ID: 0x%08X", id);

            startWrite(true);

            for (uint8_t i = 0; auto cmds = getInitCommands(i); i++)
            {
                command_list(cmds);
            }

            endWrite();
            delay(200);
#if 0
            uint32_t id = readCommand(CMD_RDDID, 0, 3);
            ESP_LOGI(TAG, "init: ID: 0x%08X", id);
            id = readCommand(CMD_RDDST, 0, 4);
            ESP_LOGI(TAG, "init: STATUS: 0x%08X", id);
            id = readCommand(CMD_RDDPM, 0, 1);
            ESP_LOGI(TAG, "init: POWER MODE: 0x%08X", id);
            id = readCommand(CMD_RDDMADCTL, 0, 1);
            ESP_LOGI(TAG, "init: MADCTL: 0x%08X", id);
            id = readCommand(CMD_RDDCOLMOD, 0, 1);
            ESP_LOGI(TAG, "init: COLOR FORMAT: 0x%08X", id);
            id = readCommand(CMD_RDDIM, 0, 1);
            ESP_LOGI(TAG, "init: IMAGE MODE: 0x%08X", id);
            id = readCommand(CMD_RDDSM, 0, 1);
            ESP_LOGI(TAG, "init: SIGNAL MODE: 0x%08X", id);
            id = readCommand(CMD_RDDST, 0, 3);
            ESP_LOGI(TAG, "init: BUSY STATUS: 0x%08X", id);
            uint32_t id1 = readCommand(CMD_RDID1, 0, 1);
            uint32_t id2 = readCommand(CMD_RDID2, 0, 1);
            uint32_t id3 = readCommand(CMD_RDID3, 0, 1);
            ESP_LOGI(TAG, "init: ID: %02X, %02X, %02X", id1, id2, id3);
#endif
            // Setup TE (Tearing Effect) synchronization if pin is configured
            if (_cfg.pin_busy != -1)
            {
                // Enable TE output on the LCD (CMD_TEON with mode 0 = V-Blank only)
                startWrite();
                _bus->writeCommand(CMD_TEON, 8);
                _bus->writeData(0x00, 8); // Mode 0: TE output on V-Blank only
                endWrite();
                ESP_LOGI(TAG, "TE output enabled on LCD");
                // Setup GPIO interrupt for TE pin
                setupTEInterrupt();
            }
            else
            {
                startWrite();
                _bus->writeCommand(CMD_TEOFF, 8);
                endWrite();
                ESP_LOGI(TAG, "TE output disabled on LCD");
            }

            return true;
        }

        void Panel_ST7789V2::beginTransaction(void) { _bus->beginTransaction(); }

        void Panel_ST7789V2::endTransaction(void) { _bus->endTransaction(); }

        void Panel_ST7789V2::setInvert(bool invert)
        {
            ESP_LOGD(TAG, "setInvert");
            _invert = invert;
            startWrite();
            _bus->writeCommand((invert ^ _cfg.invert) ? CMD_INVON : CMD_INVOFF, 8);
            endWrite();
        }

        void Panel_ST7789V2::setSleep(bool flg)
        {
            ESP_LOGD(TAG, "setSleep %s", flg ? "true" : "false");
            startWrite();
            _bus->writeCommand(flg ? CMD_SLPIN : CMD_SLPOUT, 8);
            endWrite();
        }

        void Panel_ST7789V2::setPowerSave(bool flg)
        {
            ESP_LOGD(TAG, "setPowerSave");
            startWrite();
            _bus->writeCommand(flg ? CMD_IDMON : CMD_IDMOFF, 8);
            endWrite();
        }

        color_depth_t Panel_ST7789V2::setColorDepth(color_depth_t depth)
        {
            ESP_LOGD(TAG, "setColorDepth");
            setColorDepth_impl(depth);

            update_madctl();

            return _write_depth;
        }
        void Panel_ST7789V2::setRotation(uint_fast8_t r)
        {
            ESP_LOGD(TAG, "setRotation");
            r &= 7;
            _rotation = r;
            _internal_rotation = ((r + _cfg.offset_rotation) & 3) | ((r & 4) ^ (_cfg.offset_rotation & 4));

            auto ox = _cfg.offset_x;
            auto oy = _cfg.offset_y;
            auto pw = _cfg.panel_width;
            auto ph = _cfg.panel_height;
            auto mw = _cfg.memory_width;
            auto mh = _cfg.memory_height;
            if (_internal_rotation & 1)
            {
                std::swap(ox, oy);
                std::swap(pw, ph);
                std::swap(mw, mh);
            }
            _width = pw;
            _height = ph;

            _colstart = ox;
            _rowstart = oy;

            _xs = _xe = _ys = _ye = INT16_MAX;

            update_madctl();
        }

        void Panel_ST7789V2::update_madctl(void)
        {
            ESP_LOGD(TAG, "update_madctl");
            startWrite();
            _bus->writeCommand(CMD_COLMOD, 8);
            _bus->writeData(getColMod(_write_bits), 8);
            _bus->writeCommand(CMD_MADCTL, 8);
            _bus->writeData(getMadCtl(_internal_rotation) | (_cfg.rgb_order ? MAD_RGB : MAD_BGR), 8);
            endWrite();
        }

        uint32_t Panel_ST7789V2::readCommand(uint_fast16_t cmd, uint_fast8_t index, uint_fast8_t length)
        {
            ESP_LOGD(TAG, "readCommand %02X, index: %d, length: %d", cmd, index, length);
            beginTransaction();
            _bus->writeCommand(cmd, 8);
            uint32_t res = _bus->readData(length * 8 + _cfg.dummy_read_bits);
            // removing dummy bits
            res >>= _cfg.dummy_read_bits;
            endTransaction();

            return res;
        }

        uint32_t Panel_ST7789V2::readData(uint_fast8_t index, uint_fast8_t len)
        {
            ESP_LOGD(TAG, "readData index: %d, len: %d", index, len);
            return _bus->readData(len * 8);
        }

        void Panel_ST7789V2::setWindow(uint_fast16_t xs, uint_fast16_t ys, uint_fast16_t xe, uint_fast16_t ye)
        {
            ESP_LOGD(TAG, "setWindow: (%d,%d) -> (%d,%d)", xs, ys, xe, ye);

            if (xs > xe || xe > _width - 1)
            {
                return;
            }
            if (ys > ye || ye > _height - 1)
            {
                return;
            }

            // apply offsets
            xs += _colstart;
            xe += _colstart;
            // Set limit
            if ((xe - xs) >= _width)
            {
                xs = 0;
                xe = _width - 1;
            }

            {
                // Set Column Start Address (CASET)
                _bus->writeCommand(CMD_CASET, 8);
                _bus->writeData(__builtin_bswap32(xs << 16 | xe), 32);
            }

            ys += _rowstart;
            ye += _rowstart;
            if ((ye - ys) >= _height)
            {
                ys = 0;
                ye = _height - 1;
            }
            {
                // Set Row Start Address (RASET)
                _bus->writeCommand(CMD_RASET, 8);
                _bus->writeData(__builtin_bswap32(ys << 16 | ye), 32);
            }
        }

        void Panel_ST7789V2::drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y, uint32_t rawcolor)
        {
            ESP_LOGD(TAG, "drawPixelPreclipped");
            startWrite();
            setWindow(x, y, x, y);
            _bus->writeCommand(CMD_RAMWR, 8);
            _bus->writeData(rawcolor, _write_bits);
            endWrite();
        }

        void Panel_ST7789V2::writeFillRectPreclipped(
            uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, uint32_t rawcolor)
        {
            ESP_LOGD(TAG, "writeFillRectPreclipped: x: %d, y: %d, w: %d, h: %d, rawcolor: 0x%04X", x, y, w, h, rawcolor);
            startWrite();
            uint32_t len = w * h;
            uint_fast16_t xe = w + x - 1;
            uint_fast16_t ye = y + h - 1;

            setWindow(x, y, xe, ye);
            _bus->writeCommand(CMD_RAMWR, 8);
            _bus->writeDataRepeat(rawcolor, _write_bits, len);
            endWrite();
        }

        void Panel_ST7789V2::writeBlock(uint32_t rawcolor, uint32_t len)
        {
            ESP_LOGD(TAG, "writeBlock");
            startWrite();
            _bus->writeCommand(CMD_RAMWR, 8);
            _bus->writeDataRepeat(rawcolor, _write_bits, len);
            endWrite();
        }

        void Panel_ST7789V2::writePixels(pixelcopy_t* param, uint32_t len, bool use_dma)
        {
            ESP_LOGD(TAG, "writePixels: len: %d, continue: %d", len, use_dma);
            startWrite();
            // start or continue?
            _bus->writeCommand((use_dma ? CMD_WRMEMC : CMD_RAMWR), 8);
            if (param->no_convert)
            {
                _bus->writeBytes(reinterpret_cast<const uint8_t*>(param->src_data), len * _write_bits >> 3, true, use_dma);
            }
            else
            {
                _bus->writePixels(param, len);
            }
            endWrite();
        }

        void Panel_ST7789V2::writeImage(
            uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, pixelcopy_t* param, bool use_dma)
        {
            startWrite();
            auto bytes = param->dst_bits >> 3;
            auto src_x = param->src_x;

            if (param->transp == pixelcopy_t::NON_TRANSP)
            {
                setWindow(x, y, x + w - 1, y + h - 1);
                if (param->no_convert)
                {
                    auto wb = w * bytes;
                    uint32_t i = (src_x + param->src_y * param->src_bitwidth) * bytes;
                    auto src = &((const uint8_t*)param->src_data)[i];
                    _bus->writeCommand(CMD_RAMWR, 8);
                    _bus->writeBytes(src, wb * h, false, false);
                }
                else
                {
                    writePixels(param, w * h, false);
                }
            }
            else
            {
                // not implemented
                h += y;
                uint32_t wb = w * bytes;
                do
                {
                    uint32_t i = 0;
                    while (w != (i = param->fp_skip(i, w, param)))
                    {
                        auto buf = _bus->getDMABuffer(wb);
                        int32_t len = param->fp_copy(buf, 0, w - i, param);
                        setWindow(x + i, y, x + i + len - 1, y);
                        writeBytes(buf, len * bytes, true);
                        if (w == (i += len))
                            break;
                    }
                    param->src_x = src_x;
                    param->src_y++;
                } while (++y != h);
            }
            endWrite();
        }

        void Panel_ST7789V2::writeBytes(const uint8_t* data, uint32_t len, bool use_dma)
        {
            ESP_LOGD(TAG, "writeBytes");
            _bus->writeCommand(CMD_RAMWR, 8);
            _bus->writeBytes(data, len, true, use_dma);
        }

        void Panel_ST7789V2::readRect(
            uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, void* dst, pixelcopy_t* param)
        {
            uint_fast16_t bytes = param->dst_bits >> 3;
            auto len = w * h;
            if (!_cfg.readable)
            {
                memset(dst, 0, len * bytes);
                return;
            }

            startWrite();
            setWindow(x, y, x + w - 1, y + h - 1);

            _bus->writeCommand(CMD_RAMRD, 0x88);

            if (param->no_convert)
            {
                _bus->readBytes((uint8_t*)dst, len * bytes);
            }
            else
            {
                _bus->readPixels(dst, param, len);
            }
            if (_cfg.end_read_delay_us)
            {
                delayMicroseconds(_cfg.end_read_delay_us);
            }
            endWrite();
        }

        int32_t Panel_ST7789V2::getScanLine(void)
        {
            ESP_LOGD(TAG, "getScanLine");
            return getSwap16(readCommand(CMD_RDTESCAN, 0, 2));
        }
    } // namespace v1
} // namespace lgfx
