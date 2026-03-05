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
#include <sdkconfig.h>

#include "Bus_IDFSPI.hpp"

#include "lgfx/v1/misc/pixelcopy.hpp"

#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <hal/gpio_ll.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

// #include "common.hpp"

#include <algorithm>
#include <stddef.h>

// Define __containerof macro if not available (similar to ESP-IDF)
#ifndef __containerof
#define __containerof(ptr, type, member) ((type*)((char*)(ptr) - offsetof(type, member)))
#endif

static const char* TAG = "Bus_IDFSPI";

#define SPI_TRANS_DC_LEVEL_MASK (1 << 20)

namespace lgfx
{
    inline namespace v1
    {
        IRAM_ATTR static void pre_trans_cb(spi_transaction_t* trans)
        {
            Bus_IDFSPI* bus = (Bus_IDFSPI*)trans->user;
            int level = (trans->flags & SPI_TRANS_DC_LEVEL_MASK) ? 1 : 0;
            int pin_dc = bus->config().pin_dc;
            if (pin_dc >= 0)
            {
                gpio_ll_set_level(&GPIO, pin_dc, level);
            }
        }
        //----------------------------------------------------------------------------

        void Bus_IDFSPI::config(const config_t& cfg) { _cfg = cfg; }

        bool Bus_IDFSPI::init(void)
        {
            ESP_LOGI(TAG, "init");
            esp_err_t ret;

            spi_bus_config_t buscfg = {};
            buscfg.data0_io_num = _cfg.pin_io0;
            buscfg.data1_io_num = _cfg.pin_io1;
            buscfg.data2_io_num = _cfg.pin_io2;
            buscfg.data3_io_num = _cfg.pin_io3;
            buscfg.sclk_io_num = _cfg.pin_sclk;
            buscfg.max_transfer_sz = _cfg.max_transfer_sz;
            buscfg.flags = SPICOMMON_BUSFLAG_MASTER;
            buscfg.intr_flags = 0;

            ret = spi_bus_initialize((spi_host_device_t)_cfg.spi_host, &buscfg, (spi_dma_chan_t)_cfg.dma_channel);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to initialize SPI bus: %d", ret);
                return false;
            }

            // Configure SPI device with optimal settings for QSPI
            spi_device_interface_config_t devcfg = {};
            memset(&devcfg, 0, sizeof(devcfg));
            devcfg.clock_speed_hz = _cfg.freq_write;
            devcfg.mode = _cfg.spi_mode;
            devcfg.spics_io_num = _cfg.pin_cs;
            devcfg.queue_size = 10;
            devcfg.flags = SPI_DEVICE_HALFDUPLEX | (_cfg.spi_3wire ? SPI_DEVICE_3WIRE : 0);
            devcfg.pre_cb = pre_trans_cb;
            devcfg.post_cb = nullptr;

            // Attach the device to the SPI bus
            ret = spi_bus_add_device((spi_host_device_t)_cfg.spi_host, &devcfg, &_spi_handle);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to add SPI device: %d", ret);
                spi_bus_free((spi_host_device_t)_cfg.spi_host);
                return false;
            }

            // Get maximum transaction size from SPI bus
            size_t size = 0;
            ret = spi_bus_get_max_transaction_len((spi_host_device_t)_cfg.spi_host, &size);
            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to get max transaction len, using config value");
                _spi_trans_max_bytes = _cfg.max_transfer_sz;
            }
            else
            {
                ESP_LOGI(TAG, "Max transaction len: %d", size);
                _spi_trans_max_bytes = size;
                // Use the smaller of config or bus limit
                _spi_trans_max_bytes = std::min(_spi_trans_max_bytes, _cfg.max_transfer_sz);
            }

            // Initialize transaction pool
            memset(_trans_pool, 0, sizeof(_trans_pool));
            _num_trans_inflight = 0;

            // Set DC pin to output low
            if (_cfg.pin_dc >= 0)
            {
                gpio_set_level((gpio_num_t)_cfg.pin_dc, 1);
                gpio_set_direction((gpio_num_t)_cfg.pin_dc, GPIO_MODE_OUTPUT);
            }
            _inited = true;
            return true;
        }

        void Bus_IDFSPI::dc_control(bool enable)
        {
            if (_cfg.pin_dc >= 0)
            {
                gpio_set_level((gpio_num_t)_cfg.pin_dc, enable ? 1 : 0);
            }
        }

        void Bus_IDFSPI::release(void)
        {
            ESP_LOGI(TAG, "release");
            if (!_inited)
                return;
            _inited = false;

            if (_spi_handle)
            {
                spi_bus_remove_device(_spi_handle);
                _spi_handle = nullptr;
            }

            // Free the SPI bus
            spi_bus_free((spi_host_device_t)_cfg.spi_host);
        }

        void Bus_IDFSPI::beginTransaction(void)
        {
            // dc_control(true);
            // Acquire bus lock if configured
            if (_cfg.use_lock && _spi_handle)
            {
                spi_device_acquire_bus(_spi_handle, portMAX_DELAY);
            }
            _in_transaction = true;
            // Note: Don't wait here - wait only before polling transactions as needed
        }

        void Bus_IDFSPI::endTransaction(void)
        {
            if (_in_transaction)
            {
                // Wait for all queued transactions to complete before releasing
                wait();
                if (_cfg.use_lock && _spi_handle)
                {
                    spi_device_release_bus(_spi_handle);
                }
                _in_transaction = false;
            }
            // dc_control(true);
        }

        void Bus_IDFSPI::wait(void)
        {
            // ESP_LOGD(TAG, "wait - inflight: %d", _num_trans_inflight);
            // Wait for all queued transactions to complete
            if (!_spi_handle)
                return;

            spi_transaction_t* spi_trans = nullptr;
            while (_num_trans_inflight > 0)
            {
                esp_err_t ret = spi_device_get_trans_result(_spi_handle, &spi_trans, portMAX_DELAY);
                if (ret == ESP_OK && spi_trans)
                {
                    _num_trans_inflight--;
                }
                else
                {
                    ESP_LOGE(TAG, "wait failed to get transaction result: %d", ret);
                    break;
                }
            }
        }

        bool Bus_IDFSPI::busy(void) const { return _num_trans_inflight > 0; }

        // Low-level polling read (used for readData, readBytes, readPixels)
        // keep_cs_active: if true, keeps CS low after the transaction for continued reads
        esp_err_t Bus_IDFSPI::_spi_read_polling(uint8_t* data, uint32_t length, bool keep_cs_active)
        {
            if (!_spi_handle || length == 0 || !data)
                return ESP_ERR_INVALID_ARG;

            // Wait for any queued transactions before reading
            wait();

            spi_transaction_t trans = {};
            trans.user = this;
            trans.length = 0;            // No TX data
            trans.rxlength = length * 8; // RX length in bits
            trans.tx_buffer = nullptr;
            trans.rx_buffer = data;
            trans.flags = SPI_TRANS_DC_LEVEL_MASK; // Standard SPI mode for reads
            if (keep_cs_active)
            {
                trans.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
            }

            // Use polling mode for reads (as per esp_lcd reference)
            return spi_device_polling_transmit(_spi_handle, &trans);
        }

        // Send command (similar to esp_lcd panel_io_spi_tx_param command phase)
        // This should be called Aait() has been called to ensure no queued transactions are pending
        esp_err_t Bus_IDFSPI::_tx_command(uint32_t lcd_cmd, bool keep_cs_active, uint32_t freq)
        {
            ESP_LOGD(TAG, "_tx_command: %02x %02x, keep_cs: %d", lcd_cmd & 0xFF, (lcd_cmd >> 16) & 0xFF, keep_cs_active);
            if (!_spi_handle)
                return ESP_ERR_INVALID_STATE;
            // Wait for any queued transactions before reading
            wait();

            spi_transaction_t* trans = &_trans_pool[0];
            memset(trans, 0, sizeof(spi_transaction_t));

            trans->user = this;
            trans->length = _cfg.cmd_bitsize;
            trans->tx_buffer = &lcd_cmd;
            trans->flags = 0; // DC is LOW
            trans->override_freq_hz = freq;

            if (keep_cs_active)
            {
                trans->flags |= SPI_TRANS_CS_KEEP_ACTIVE;
            }

            return spi_device_polling_transmit(_spi_handle, trans);
        }

        // esp_err_t Bus_IDFSPI::_tx_command(uint8_t lcd_cmd, bool keep_cs_active)
        // {
        //     ESP_LOGD(TAG, "_tx_command: %02x, keep_cs: %d", lcd_cmd, keep_cs_active);
        //     if (!_spi_handle)
        //         return ESP_ERR_INVALID_STATE;
        //     // Wait for any queued transactions before reading
        //     wait();

        //     qspi_trans_descriptor_t* lcd_trans = &_trans_pool[0];
        //     memset(lcd_trans, 0, sizeof(qspi_trans_descriptor_t));

        //     lcd_trans->base.user = this;
        //     lcd_trans->base.length = 8;
        //     lcd_trans->base.tx_buffer = &lcd_cmd;
        //     lcd_trans->base.flags = 0;

        //     if (keep_cs_active)
        //     {
        //         lcd_trans->base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
        //     }
        //     dc_control(false);
        //     // Command is short, using polling mode (as per esp_lcd reference)
        //     esp_err_t ret = spi_device_polling_transmit(_spi_handle, &lcd_trans->base);
        //     dc_control(true);
        //     return ret;
        // }
        // Send parameter (similar to esp_lcd panel_io_spi_tx_param param phase)
        esp_err_t Bus_IDFSPI::_tx_param(const void* param, size_t param_size, bool keep_cs_active)
        {
            // ESP_LOGD(TAG, "_tx_param: size: %d, keep_cs: %d", param_size, keep_cs_active);
            if (!_spi_handle || !param || param_size == 0)
                return ESP_ERR_INVALID_ARG;
            // Wait for any queued transactions before reading
            wait();

            spi_transaction_t* trans = &_trans_pool[0];
            memset(trans, 0, sizeof(spi_transaction_t));

            trans->user = this;
            trans->length = param_size * 8; // Length in bits
            trans->tx_buffer = param;
            trans->flags = SPI_TRANS_DC_LEVEL_MASK; // DC is HIGH = data

            if (keep_cs_active)
            {
                trans->flags |= SPI_TRANS_CS_KEEP_ACTIVE;
            }

            // dc_control(true);
            // Parameter is usually short, using polling mode (as per esp_lcd reference)
            return spi_device_polling_transmit(_spi_handle, trans);
        }

        // Send color data using queued transfers (similar to esp_lcd panel_io_spi_tx_color)
        // This follows the ESP-IDF pattern for queuing large data transfers
        esp_err_t Bus_IDFSPI::_tx_color(const void* color, size_t color_size, bool keep_cs_active)
        {
            // ESP_LOGD(TAG, "_tx_color: size: %d bytes", color_size);
            if (!_spi_handle || !color || color_size == 0)
                return ESP_ERR_INVALID_ARG;

            esp_err_t ret = ESP_OK;
            const uint8_t* color_ptr = (const uint8_t*)color;
            size_t remaining = color_size;
            uint32_t count = 0;

            // dc_control(true);
            // Split large color buffer into chunks and queue them (as per esp_lcd reference)
            do
            {
                size_t chunk_size = remaining;
                spi_transaction_t* trans = nullptr;

                // Get next available transaction from pool, or recycle one
                if (_num_trans_inflight < TRANS_QUEUE_DEPTH)
                {
                    // Get the next available transaction
                    trans = &_trans_pool[_num_trans_inflight];
                }
                else
                {
                    // Transaction pool has used up, recycle one transaction
                    spi_transaction_t* spi_trans = nullptr;
                    ret = spi_device_get_trans_result(_spi_handle, &spi_trans, portMAX_DELAY);
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "recycle spi transactions failed: %d", ret);
                        return ret;
                    }
                    trans = spi_trans;
                    _num_trans_inflight--;
                }

                memset(trans, 0, sizeof(spi_transaction_t));

                trans->flags = SPI_TRANS_DC_LEVEL_MASK; // DC is HIGH = data
                if (keep_cs_active)
                {
                    trans->flags |= SPI_TRANS_CS_KEEP_ACTIVE;
                }
                // SPI per-transfer size has its limitation, cap to maximum supported by bus
                if (chunk_size > _spi_trans_max_bytes)
                {
                    chunk_size = _spi_trans_max_bytes;
                    trans->flags |= SPI_TRANS_CS_KEEP_ACTIVE;
                }
                else
                {
                    // Last chunk - CS will be released
                    // lcd_trans->base.flags = 0;
                }

                trans->user = this;
                trans->length = chunk_size * 8; // transaction length is in bits
                trans->tx_buffer = color_ptr;

                // Use QSPI mode (4-bit) for color data transfers
                if (_cfg.qspi)
                {
                    trans->flags |= SPI_TRANS_MODE_QIO;
                }

                // Color data is usually large, using queue+blocking mode (as per esp_lcd reference)
                ret = spi_device_queue_trans(_spi_handle, trans, portMAX_DELAY);
                // ret = spi_device_polling_transmit(_spi_handle, &lcd_trans->base);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "spi transmit (queue) color failed: %d", ret);
                    return ret;
                }
                _num_trans_inflight++;

                // Move on to the next chunk
                color_ptr += chunk_size;
                remaining -= chunk_size;
                count++;
            } while (remaining > 0); // Continue while we have remaining data to transmit
            // ESP_LOGD(TAG, "_tx_color: remaining: %d, count: %d", remaining, count);
            return ret;
        }

        bool Bus_IDFSPI::writeCommand(uint32_t data, uint_fast8_t bit_length)
        {
            // Keep CS active as data will follow
            return _tx_command(data, true, (bit_length & 0x80) ? _cfg.freq_read : _cfg.freq_write) == ESP_OK;
        }

        void Bus_IDFSPI::writeData(uint32_t data, uint_fast8_t bit_length)
        {
            // Small data sent as param (polling mode), similar to esp_lcd pattern
            uint8_t bytes = (bit_length + 7) >> 3;
            uint8_t buf[4];
            for (uint8_t i = 0; i < bytes; ++i)
            {
                buf[i] = (data >> (i * 8)) & 0xFF;
            }

            // Send as param (polling, CS will be released after this)
            _tx_param(buf, bytes, false);
            ESP_LOGD(TAG, "writeData: data: 0x%x, bits: %d", data, bit_length);
        }

        void Bus_IDFSPI::writeDataRepeat(uint32_t data, uint_fast8_t bit_length, uint32_t count)
        {
            // ESP_LOGI(TAG, "writeDataRepeat: data: 0x%x, bits: %d, count: %d", data, bit_length, count);
            uint8_t bytes = (bit_length + 7) >> 3;
            uint32_t total_bytes = bytes * count;
            uint32_t chunk_size = (_spi_trans_max_bytes / bytes) * bytes;
            auto temp_buf = _temp_buffer.getBuffer(chunk_size);
            if (temp_buf)
            {
                // Fast pattern fill using wide writes based on pattern size
                if (bytes == 2)
                {
                    // RGB565: duplicate 16-bit pattern into 32-bit for 2x faster fill
                    uint16_t p16 = data;
                    uint32_t p32 = p16 | (p16 << 16);
                    uint32_t* dst32 = reinterpret_cast<uint32_t*>(temp_buf);
                    uint32_t count32 = chunk_size >> 2;
                    while (count32--)
                    {
                        *dst32++ = p32;
                    }
                }
                else if (bytes == 3)
                {
                    // RGB666: create 12-byte super-pattern (LCM of 3 and 4) for 32-bit writes
                    uint8_t b = (data >> 16) & 0x3F;
                    uint8_t g = (data >> 8) & 0x3F;
                    uint8_t r = data & 0x3F;
                    uint8_t* dst = temp_buf;
                    uint8_t* end = temp_buf + chunk_size;
                    // Build 12-byte pattern (4 copies of 3-byte pattern) for aligned 32-bit access
                    uint32_t p0 = r | (g << 8) | (b << 16) | (r << 24);
                    uint32_t p1 = g | (b << 8) | (r << 16) | (g << 24);
                    uint32_t p2 = b | (r << 8) | (g << 16) | (b << 24);
                    // Fill in 12-byte chunks using 32-bit writes
                    while (dst + 12 <= end)
                    {
                        *reinterpret_cast<uint32_t*>(dst) = p0;
                        *reinterpret_cast<uint32_t*>(dst + 4) = p1;
                        *reinterpret_cast<uint32_t*>(dst + 8) = p2;
                        dst += 12;
                    }
                    // Handle remaining bytes (0, 3, 6, or 9 bytes)
                    while (dst + 3 <= end)
                    {
                        dst[0] = r;
                        dst[1] = g;
                        dst[2] = b;
                        dst += 3;
                    }
                }
                else
                {
                    // Fallback for other sizes: direct byte copy (no memcpy overhead)
                    uint8_t* dst = temp_buf;
                    uint8_t* end = temp_buf + chunk_size;
                    while (dst < end)
                    {
                        for (uint32_t i = 0; i < bytes; ++i)
                        {
                            *dst++ = data & 0xFF;
                            data >>= 8;
                        }
                    }
                }
                // send chunks of pattern data
                uint32_t remaining = total_bytes;
                do
                {
                    uint32_t len = (remaining <= chunk_size) ? remaining : chunk_size;
                    remaining -= len;
                    bool last_chunk = (remaining == 0);
                    _tx_color(temp_buf, len, last_chunk ? false : true);
                } while (remaining > 0);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to allocate buffer for writeDataRepeat");
            }
            // _temp_buffer.deleteBuffer();
        }

        void Bus_IDFSPI::writePixels(pixelcopy_t* param, uint32_t length)
        {
            // ESP_LOGD(TAG, "writePixels: length: %d pixels", length);
            const uint8_t dst_bytes = param->dst_bits >> 3;
            const uint32_t line_width = param->src_bitwidth; // pixels per horizontal line

            // Allocate buffer for one horizontal line
            uint32_t line_bytes = line_width * dst_bytes;
            auto dmabuf = _temp_buffer.getBuffer(line_bytes);
            if (dmabuf == nullptr)
            {
                ESP_LOGE(TAG, "Failed to allocate buffer for writePixels");
                return;
            }

            uint32_t line_count = 0;
            int16_t old_src_x = param->src_x;

            // Process each horizontal line
            while (length > 0)
            {
                uint32_t line_pixels = std::min(line_width, length);
                bool keep_cs_active = (length > line_pixels);

                // Copy one horizontal line of pixels to DMA buffer
                param->fp_copy(dmabuf, 0, line_pixels, param);

                // Send the horizontal line
                _tx_color(dmabuf, line_pixels * dst_bytes, keep_cs_active);

                // Advance to next line
                param->src_y++;
                param->src_x = old_src_x;
                length -= line_pixels;
                line_count++;
            }

            ESP_LOGD(TAG, "writePixels done, lines: %d", line_count);
        }

        void Bus_IDFSPI::writeBytes(const uint8_t* data, uint32_t length, bool dc, bool use_dma)
        {
            // Use queued color transfer for raw byte data
            uint32_t count = 0;
            uint32_t total = length;
            do
            {
                uint32_t chunk = std::min(length, _spi_trans_max_bytes);
                bool keep_cs_active = length == chunk ? false : true;
                _tx_param(data, chunk, keep_cs_active);
                data += chunk;
                length -= chunk;
                count++;
            } while (length > 0);
            ESP_LOGD(TAG, "writeBytes: %d, chunks: %d", total, count);
        }

        uint8_t* Bus_IDFSPI::getDMABuffer(uint32_t length)
        {
            // ESP_LOGD(TAG, "getDMABuffer: %d bytes", length);
            // Return DMA-capable buffer from temp buffer pool
            return _temp_buffer.getBuffer(length);
        }

        void Bus_IDFSPI::beginRead(uint_fast8_t dummy_bits)
        {
            readData(dummy_bits);
            beginRead();
        }

        void Bus_IDFSPI::beginRead(void)
        {
            // Ensure all queued write transactions are complete before reading
            wait();
        }

        void Bus_IDFSPI::endRead(void)
        {
            // Nothing special needed after read
        }

        uint32_t Bus_IDFSPI::readData(uint_fast8_t bit_length)
        {
            // ESP_LOGD(TAG, "readData: %d bits", bit_length);
            uint8_t bytes = (bit_length + 7) >> 3;
            uint8_t buf[4] = {0};

            if (_spi_read_polling(buf, bytes, false) != ESP_OK)
            {
                ESP_LOGE(TAG, "readData failed");
                return 0;
            }

            // Reconstruct value from bytes
            uint32_t result = 0;
            for (uint8_t i = 0; i < bytes; ++i)
            {
                result |= ((uint32_t)buf[i]) << (i * 8);
            }

            return result;
        }

        bool Bus_IDFSPI::readBytes(uint8_t* dst, uint32_t length, bool use_dma)
        {
            if (!dst || length == 0)
                return false;
            // read always RGB666 (3 bytes)
            uint32_t step = (_spi_trans_max_bytes / 3) * 3;

            while (length > 0)
            {
                uint32_t chunk = std::min(length, step);
                // Keep CS active if there's more data to read after this chunk
                bool keep_cs_active = (length > chunk);
                if (_spi_read_polling(dst, chunk, keep_cs_active) != ESP_OK)
                {
                    ESP_LOGE(TAG, "readBytes chunk failed");
                    return false;
                }
                dst += chunk;
                length -= chunk;
            }
            return true;
        }

        void Bus_IDFSPI::readPixels(void* dst, pixelcopy_t* param, uint32_t length)
        {
            const uint32_t bytes = param->src_bits >> 3;
            uint32_t limit = _spi_trans_max_bytes / bytes;
            uint32_t len;
            int32_t dstindex = 0;

            // Get buffer for reading pixel data
            param->src_data = _temp_buffer.getBuffer(limit * bytes);
            if (!param->src_data)
            {
                ESP_LOGE(TAG, "Failed to allocate buffer for readPixels");
                return;
            }
            // Read and convert pixels in chunks
            do
            {
                len = (limit <= length) ? limit : length;
                // Keep CS active if there's more data to read after this chunk
                bool keep_cs_active = (length > len);
                if (_spi_read_polling((uint8_t*)param->src_data, len * bytes, keep_cs_active) != ESP_OK)
                {
                    ESP_LOGE(TAG, "readPixels read failed");
                    break;
                }
                param->src_x = 0;
                dstindex = param->fp_copy(dst, dstindex, dstindex + len, param);
                length -= len;
            } while (length);
            _temp_buffer.deleteBuffer();
        }

        //----------------------------------------------------------------------------
    } // namespace v1
} // namespace lgfx
