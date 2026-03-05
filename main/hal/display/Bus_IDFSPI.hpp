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
#pragma once

#include <string.h>

#include <driver/spi_master.h>
#include <driver/spi_common.h>

#include "lgfx/v1/Bus.hpp"
#include "lgfx/v1/platforms/common.hpp"

namespace lgfx
{
    inline namespace v1
    {
        //----------------------------------------------------------------------------

        class Bus_IDFSPI : public IBus
        {
        public:
            struct config_t
            {
                uint32_t freq_write = 40000000; // 40MHz
                uint32_t freq_read = 8000000;   // 8MHz
                int16_t pin_sclk = -1;
                int16_t pin_cs = -1;
                int16_t pin_dc = -1;
                int16_t pin_io0 = -1; // MOSI / DATA0
                int16_t pin_io1 = -1; // MISO / DATA1
                int16_t pin_io2 = -1; // DATA2
                int16_t pin_io3 = -1; // DATA3
                uint8_t spi_mode = 0;
                uint8_t cmd_bitsize = 8;
                bool spi_3wire = true; // Single IO line (MOSI/MISO on one wire)
                bool qspi = false;     // Quad SPI mode
                bool use_lock = true;  // Lock the bus for concurrent access
                uint8_t dma_channel = SPI_DMA_CH_AUTO;
                // Optimal transfer size for DMA (much larger than 1 byte)
                uint32_t max_transfer_sz = 4096;
#if !defined(CONFIG_IDF_TARGET) || defined(CONFIG_IDF_TARGET_ESP32)
                spi_host_device_t spi_host = VSPI_HOST;
#else
                spi_host_device_t spi_host = SPI2_HOST;
#endif
            };

            constexpr Bus_IDFSPI(void) = default;

            const config_t& config(void) const { return _cfg; }

            void config(const config_t& config);

            bus_type_t busType(void) const override { return bus_type_t::bus_spi; }

            bool init(void) override;
            void release(void) override;

            void beginTransaction(void) override;
            void endTransaction(void) override;
            void wait(void) override;
            bool busy(void) const override;
            uint32_t getClock(void) const override { return _cfg.freq_write; }
            void setClock(uint32_t freq) override { _cfg.freq_write = freq; }

            void flush(void) override {}
            bool writeCommand(uint32_t data, uint_fast8_t bit_length) override;
            void writeData(uint32_t data, uint_fast8_t bit_length) override;
            void writeDataRepeat(uint32_t data, uint_fast8_t bit_length, uint32_t count) override;
            void writePixels(pixelcopy_t* pc, uint32_t length) override;
            void writeBytes(const uint8_t* data, uint32_t length, bool dc, bool use_dma) override;

            void initDMA(void) override {}
            void addDMAQueue(const uint8_t* data, uint32_t length) override {};
            void execDMAQueue(void) override {};
            uint8_t* getDMABuffer(uint32_t length) override;

            void dc_control(bool enable);

            void beginRead(uint_fast8_t dummy_bits) override;
            void beginRead(void) override;
            void endRead(void) override;
            uint32_t readData(uint_fast8_t bit_length) override;
            bool readBytes(uint8_t* dst, uint32_t length, bool use_dma) override;
            void readPixels(void* dst, pixelcopy_t* pc, uint32_t length) override;

        private:
            // Transaction descriptor similar to esp_lcd_panel_io_spi
            // struct qspi_trans_descriptor_t
            // {
            //     spi_transaction_t base;
            // };

            // Helper methods for command/param/color transfers (following esp_lcd pattern)
            esp_err_t _tx_command(uint32_t lcd_cmd, bool keep_cs_active, uint32_t freq);
            // esp_err_t _tx_command(uint8_t lcd_cmd, bool keep_cs_active);
            esp_err_t _tx_param(const void* param, size_t param_size, bool keep_cs_active);
            esp_err_t _tx_color(const void* color, size_t color_size, bool keep_cs_active);

            // Low-level SPI read method (standard SPI mode, not QSPI)
            // keep_cs_active: if true, keeps CS low after the transaction for continued reads
            esp_err_t _spi_read_polling(uint8_t* data, uint32_t length, bool keep_cs_active = false);

            config_t _cfg;
            spi_device_handle_t _spi_handle = nullptr;

            // Transaction pool for queued transfers (colors)
            static constexpr size_t TRANS_QUEUE_DEPTH = 10;
            // qspi_trans_descriptor_t _trans_pool[TRANS_QUEUE_DEPTH];
            spi_transaction_t _trans_pool[TRANS_QUEUE_DEPTH];

            uint32_t _num_trans_inflight = 0;
            uint32_t _spi_trans_max_bytes = 0;

            // DMA queue for batched transfers
            struct dma_queue_item_t
            {
                const uint8_t* data;
                uint32_t length;
            };
            std::vector<dma_queue_item_t> _dma_queue;

            // Temporary buffer for small transfers (replaces FlipBuffer)
            SimpleBuffer _temp_buffer;

            bool _inited = false;
            bool _in_transaction = false;
        };

        //----------------------------------------------------------------------------
    } // namespace v1
} // namespace lgfx
