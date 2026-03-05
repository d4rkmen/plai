/**
 * @file speaker.cpp
 * @brief Speaker implementation for ESP32S3 using ESP-IDF I2S driver
 */
#include "speaker.h"
#include "hal.h"
#include <cstring>
#include <algorithm>
#include <driver/i2c_master.h>
#include "esp_log.h"

static const char* TAG = "SPEAKER";

namespace HAL
{
    // Default tone waveform (sine-like wave)
    const uint8_t Speaker::_default_tone_wav[16] = {
        0x80, 0xB0, 0xDA, 0xF6, 0xFF, 0xF6, 0xDA, 0xB0, 0x80, 0x50, 0x26, 0x0A, 0x00, 0x0A, 0x26, 0x50};

    Speaker::Speaker(HAL::Hal* hal) : _hal(hal), _board_type(hal->board_type()), _dev_handle(nullptr) { begin(); }

    Speaker::~Speaker() { end(); }

    bool Speaker::begin(void)
    {
        if (_task_running)
        {
            return true;
        }

        if (!isEnabled())
        {
            return false;
        }

        switch (_board_type)
        {
        case BoardType::CARDPUTER:
            break;
        case BoardType::CARDPUTER_ADV:
            if (!_init_cardputer_adv(true))
            {
                return false;
            }
            break;
        case BoardType::AUTO_DETECT:
            break;
        }

        // Setup I2S
        if (!_setup_i2s())
        {
            return false;
        }

        if (_task_semaphore == nullptr)
        {
            _task_semaphore = xSemaphoreCreateBinary();
        }

        // Create speaker task (no semaphore needed, we use task notifications)
        BaseType_t result;
        if (_cfg.task_pinned_core < 2)
        {
            result = xTaskCreatePinnedToCore(spk_task,
                                             "speaker_task",
                                             2048,
                                             this,
                                             _cfg.task_priority,
                                             &_task_handle,
                                             _cfg.task_pinned_core);
        }
        else
        {
            result = xTaskCreate(spk_task, "speaker_task", 2048, this, _cfg.task_priority, &_task_handle);
        }

        if (result != pdPASS || _task_semaphore == nullptr)
        {
            return false;
        }

        _task_running = true;
        return true;
    }

    void Speaker::end(void)
    {
        if (!_task_running)
        {
            return;
        }

        _task_running = false;

        // Stop all channels
        stop();

        // Wake up task to exit via task notification
        if (_task_handle)
        {
            xTaskNotifyGive(_task_handle);
        }

        // Wait for task to finish
        if (_task_handle)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            vTaskDelete(_task_handle);
            _task_handle = nullptr;
        }

        // Delete I2S channel
        if (_tx_chan)
        {
            i2s_channel_disable(_tx_chan);
            i2s_del_channel(_tx_chan);
            _tx_chan = nullptr;
        }

        switch (_board_type)
        {
        case BoardType::CARDPUTER:
            break;
        case BoardType::CARDPUTER_ADV:
            _init_cardputer_adv(false);
            break;
        case BoardType::AUTO_DETECT:
            break;
        }
        // Delete task semaphore
        if (_task_semaphore)
        {
            vSemaphoreDelete(_task_semaphore);
            _task_semaphore = nullptr;
        }
    }

    bool Speaker::_init_cardputer_adv(bool enabled)
    {
        struct RegValue
        {
            uint8_t value[2];
        };
        // const uint8_t REG_RESET = 0x00;
        // const uint8_t REG_CLOCK_MANAGER = 0x01;
        // const uint8_t REG_CLOCK_MANAGER_MCLK = 0x02;
        // const uint8_t REG_CLOCK_MANAGER_MULT_PRE = 0x03;
        // const uint8_t REG_SYSTEM = 0x0D;
        // const uint8_t REG_SYSTEM_POWER_UP_ANALOG = 0x12;
        // const uint8_t REG_SYSTEM_POWER_UP_DAC = 0x13;
        // const uint8_t REG_SYSTEM_ENABLE_OUTPUT_TO_HP = 0x14;
        // const uint8_t REG_DAC = 0x32;
        const RegValue enable_data[] = {
            {0x00, 0x80}, // 0x00 RESET/  CSM POWER ON
            {0x01, 0xB5}, // 0x01 CLOCK_MANAGER/ MCLK=BCLK
            {0x02, 0x18}, // 0x02 CLOCK_MANAGER/ MULT_PRE=3
            {0x0D, 0x01}, // 0x0D SYSTEM/ Power up analog circuitry
            {0x12, 0x00}, // 0x12 SYSTEM/ power-up DAC - NOT default
            {0x13, 0x10}, // 0x13 SYSTEM/ Enable output to HP drive - NOT default
            {0x32, 0xBF}, // 0x32 DAC/ DAC volume (0xBF == ±0 dB )
            {0x37, 0x08}, // 0x37 DAC/ Bypass DAC equalizer - NOT default
        };

        // add or remove device to bus
        esp_err_t ret;
        if (enabled)
        {
            ret = _hal->i2c()->add_device(SPEAKER_I2C_ADDR, SPEAKER_I2C_FREQ_HZ, nullptr, &_dev_handle);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to add device to I2C bus");
                return false;
            }
            for (auto& chunk : enable_data)
            {
                ESP_LOGD(TAG, "Writing to I2C device: 0x%02X = 0x%02X", chunk.value[0], chunk.value[1]);
                ret = i2c_master_transmit(_dev_handle, chunk.value, 2, SPEAKER_I2C_TIMEOUT_MS);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to write to I2C device");
                    return false;
                }
            }
        }
        else
        {
            ret = hal()->i2c()->remove_device(_dev_handle);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to remove device from I2C bus");
                return false;
            }
        }
        return true;
    }

    bool Speaker::_setup_i2s(void)
    {
        // Configure I2S channel
        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(_cfg.i2s_port, I2S_ROLE_MASTER);
        chan_cfg.dma_desc_num = _cfg.dma_buf_count;
        chan_cfg.dma_frame_num = _cfg.dma_buf_len;
        chan_cfg.auto_clear = true;

        esp_err_t ret = i2s_new_channel(&chan_cfg, &_tx_chan, nullptr);
        if (ret != ESP_OK)
        {
            return false;
        }

        // Configure I2S standard mode
        i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_cfg.sample_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                            _cfg.stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO),
            .gpio_cfg =
                {
                    .mclk = I2S_GPIO_UNUSED,
                    .bclk = (gpio_num_t)_cfg.pin_bck,
                    .ws = (gpio_num_t)_cfg.pin_ws,
                    .dout = (gpio_num_t)_cfg.pin_data_out,
                    .din = I2S_GPIO_UNUSED,
                    .invert_flags =
                        {
                            .mclk_inv = false,
                            .bclk_inv = false,
                            .ws_inv = false,
                        },
                },
        };

        ret = i2s_channel_init_std_mode(_tx_chan, &std_cfg);
        if (ret != ESP_OK)
        {
            i2s_del_channel(_tx_chan);
            _tx_chan = nullptr;
            return false;
        }

        // Enable I2S channel
        ret = i2s_channel_enable(_tx_chan);
        if (ret != ESP_OK)
        {
            i2s_del_channel(_tx_chan);
            _tx_chan = nullptr;
            return false;
        }

        return true;
    }

    void Speaker::wav_info_t::clear(void)
    {
        repeat = 0;
        sample_rate_x256 = 0;
        data = nullptr;
        length = 0;
        flg = 0;
    }

    size_t Speaker::isPlaying(uint8_t channel) const volatile
    {
        if (channel >= CHANNELS_NUM)
        {
            return 0;
        }
        return ((bool)_ch_info[channel].wavinfo[0].repeat) + ((bool)_ch_info[channel].wavinfo[1].repeat);
    }

    size_t Speaker::getPlayingChannels(void) const volatile { return __builtin_popcount(_play_channel_bits.load()); }

    void Speaker::setAllChannelVolume(uint8_t volume)
    {
        for (size_t ch = 0; ch < CHANNELS_NUM; ++ch)
        {
            _ch_info[ch].volume = volume;
        }
    }

    void Speaker::setChannelVolume(uint8_t channel, uint8_t volume)
    {
        if (channel < CHANNELS_NUM)
        {
            _ch_info[channel].volume = volume;
        }
    }

    uint8_t Speaker::getChannelVolume(uint8_t channel) const { return (channel < CHANNELS_NUM) ? _ch_info[channel].volume : 0; }

    void Speaker::stop(void)
    {
        for (size_t ch = 0; ch < CHANNELS_NUM; ++ch)
        {
            stop(ch);
        }
    }

    void Speaker::stop(uint8_t channel)
    {
        if (channel < CHANNELS_NUM)
        {
            _ch_info[channel].wavinfo[0].clear();
            _ch_info[channel].wavinfo[1].clear();
            _ch_info[channel].index = 0;
        }
    }

    bool Speaker::tone(float frequency,
                       uint32_t duration,
                       int channel,
                       bool stop_current_sound,
                       const uint8_t* raw_data,
                       size_t array_len,
                       bool stereo)
    {
        return _play_raw(raw_data,
                         array_len,
                         false,
                         false,
                         frequency * (array_len >> stereo),
                         stereo,
                         (duration != UINT32_MAX) ? (uint32_t)(duration * frequency / 1000) : UINT32_MAX,
                         channel,
                         stop_current_sound,
                         true);
    }

    bool Speaker::tone(float frequency, uint32_t duration, int channel, bool stop_current_sound)
    {
        return tone(frequency, duration, channel, stop_current_sound, _default_tone_wav, sizeof(_default_tone_wav), false);
    }

    bool Speaker::playRaw(const int8_t* raw_data,
                          size_t array_len,
                          uint32_t sample_rate,
                          bool stereo,
                          uint32_t repeat,
                          int channel,
                          bool stop_current_sound)
    {
        return _play_raw(static_cast<const void*>(raw_data),
                         array_len,
                         false,
                         true,
                         sample_rate,
                         stereo,
                         repeat,
                         channel,
                         stop_current_sound,
                         false);
    }

    bool Speaker::playRaw(const uint8_t* raw_data,
                          size_t array_len,
                          uint32_t sample_rate,
                          bool stereo,
                          uint32_t repeat,
                          int channel,
                          bool stop_current_sound)
    {
        return _play_raw(static_cast<const void*>(raw_data),
                         array_len,
                         false,
                         false,
                         sample_rate,
                         stereo,
                         repeat,
                         channel,
                         stop_current_sound,
                         false);
    }

    bool Speaker::playRaw(const int16_t* raw_data,
                          size_t array_len,
                          uint32_t sample_rate,
                          bool stereo,
                          uint32_t repeat,
                          int channel,
                          bool stop_current_sound)
    {
        return _play_raw(static_cast<const void*>(raw_data),
                         array_len,
                         true,
                         true,
                         sample_rate,
                         stereo,
                         repeat,
                         channel,
                         stop_current_sound,
                         false);
    }

    bool Speaker::playWav(const uint8_t* wav_data, size_t data_len, uint32_t repeat, int channel, bool stop_current_sound)
    {
        // Parse WAV header
        if (data_len < 44)
        {
            return false;
        }

        // Check RIFF header
        if (memcmp(wav_data, "RIFF", 4) != 0 || memcmp(wav_data + 8, "WAVE", 4) != 0)
        {
            return false;
        }

        // Find fmt chunk
        size_t offset = 12;
        while (offset + 8 <= data_len)
        {
            if (memcmp(wav_data + offset, "fmt ", 4) == 0)
            {
                break;
            }
            uint32_t chunk_size = *(uint32_t*)(wav_data + offset + 4);
            offset += 8 + chunk_size;
        }

        if (offset + 24 > data_len)
        {
            return false;
        }

        uint16_t audio_format = *(uint16_t*)(wav_data + offset + 8);
        uint16_t num_channels = *(uint16_t*)(wav_data + offset + 10);
        uint32_t sample_rate = *(uint32_t*)(wav_data + offset + 12);
        uint16_t bits_per_sample = *(uint16_t*)(wav_data + offset + 22);

        if (audio_format != 1) // Only PCM supported
        {
            return false;
        }

        // Find data chunk
        offset += 8 + *(uint32_t*)(wav_data + offset + 4);
        while (offset + 8 <= data_len)
        {
            if (memcmp(wav_data + offset, "data", 4) == 0)
            {
                break;
            }
            uint32_t chunk_size = *(uint32_t*)(wav_data + offset + 4);
            offset += 8 + chunk_size;
        }

        if (offset + 8 > data_len)
        {
            return false;
        }

        uint32_t data_size = *(uint32_t*)(wav_data + offset + 4);
        const uint8_t* audio_data = wav_data + offset + 8;
        size_t sample_count = data_size / (bits_per_sample / 8) / num_channels;

        bool stereo = (num_channels == 2);
        bool is_16bit = (bits_per_sample == 16);
        bool is_signed = is_16bit; // 16bit is typically signed

        return _play_raw(audio_data,
                         sample_count,
                         is_16bit,
                         is_signed,
                         sample_rate,
                         stereo,
                         repeat,
                         channel,
                         stop_current_sound,
                         false);
    }

    bool Speaker::_play_raw(const void* wav,
                            size_t array_len,
                            bool flg_16bit,
                            bool flg_signed,
                            float sample_rate,
                            bool flg_stereo,
                            uint32_t repeat_count,
                            int channel,
                            bool stop_current_sound,
                            bool no_clear_index)
    {
        if (!_task_running || wav == nullptr || array_len == 0)
        {
            return false;
        }

        // Find available channel
        if (channel < 0)
        {
            for (size_t ch = 0; ch < CHANNELS_NUM; ++ch)
            {
                if (_ch_info[ch].wavinfo[0].repeat == 0)
                {
                    channel = ch;
                    break;
                }
            }
            if (channel < 0)
            {
                return false;
            }
        }

        if (channel >= CHANNELS_NUM)
        {
            return false;
        }

        // Prepare wave info
        wav_info_t wav_info;
        wav_info.data = wav;
        wav_info.length = array_len;
        wav_info.repeat = repeat_count;
        wav_info.sample_rate_x256 = (uint32_t)(sample_rate * 256.0f);
        wav_info.is_stereo = flg_stereo;
        wav_info.is_16bit = flg_16bit;
        wav_info.is_signed = flg_signed;
        wav_info.stop_current = stop_current_sound;
        wav_info.no_clear_index = no_clear_index;

        return _set_next_wav(channel, wav_info);
    }

    bool Speaker::_set_next_wav(size_t ch, const wav_info_t& wav)
    {
        auto ch_info = &_ch_info[ch];
        uint8_t chmask = 1 << ch;
        if (!wav.stop_current)
        {
            // waiting for the next wave slot to be free
            while ((_play_channel_bits.load() & chmask) && (ch_info->next()->repeat))
            {
                // if current wav is infinite repeat, return false, new wav will never play
                if (ch_info->wav()->repeat == ~0u)
                {
                    return false;
                }
                // wait current wav to finish to load next wav for free buffer
                xSemaphoreTake(_task_semaphore, portMAX_DELAY);
            }
        }
        // load new wav to next buffer
        *(ch_info->next()) = wav;

        // set channel playing bit
        _play_channel_bits.fetch_or(chmask);
        // wake up playback task to load next wav
        xTaskNotifyGive(_task_handle);
        return true;
    }

    bool Speaker::_get_next_wav(size_t ch)
    {
        auto ch_info = &_ch_info[ch];
        bool clear_idx = (ch_info->next()->repeat == 0 || !ch_info->next()->no_clear_index ||
                          (ch_info->next()->data != ch_info->wav()->data));
        ch_info->wav()->clear();
        // flip channel buffers and release the semaphore to load next wav
        ch_info->flip = !ch_info->flip;
        xSemaphoreGive(_task_semaphore);

        if (clear_idx)
        {
            ch_info->index = 0;
            if (ch_info->wav()->repeat == 0)
            {
                // Not playing anymore
                _play_channel_bits.fetch_and(~(1 << ch));
                ch_info->diff = 0;
                ch_info->index = 0;
                return false;
            }
        }
        return true;
    }

    size_t Speaker::_mix_channels(size_t samples)
    {
        // using same mix buffer for output
        int16_t* output = (int16_t*)mix_buf;
        const bool out_stereo = _cfg.stereo;
        const int32_t out_rate_x256 = _cfg.sample_rate * 256;

        memset(mix_buf, 0, samples * sizeof(int32_t));
        // actual mixed sampless
        size_t mix_samples = 0;

        // Calculate base volume: magnification * (master_volume^2) / sample_rate / 2^28
        const float base_volume =
            (_cfg.magnification << out_stereo) * (_master_volume * _master_volume) / (float)out_rate_x256 / (1 << 28);

        // Mix each active channel
        for (size_t ch = 0; ch < CHANNELS_NUM; ++ch)
        {
            if (!(_play_channel_bits.load() & (1 << ch)))
                continue;

            auto ch_info = &_ch_info[ch];

            // Switch to queued sound if current finished or interrupted
            if (ch_info->wav()->repeat == 0 || ch_info->next()->stop_current)
            {
                if (!_get_next_wav(ch))
                    continue;
            }

            // Calculate channel volume (squared for perceptual linearity, boost 8-bit)
            int32_t vol_sq = ch_info->volume * ch_info->volume;
            if (!ch_info->wav()->is_16bit)
                vol_sq <<= 8;
            const float ch_volume = base_volume * vol_sq;

            const bool in_stereo = ch_info->wav()->is_stereo;
            const int32_t in_rate_x256 = ch_info->wav()->sample_rate_x256;
            float* curr_sample = ch_info->liner_buf[0]; // Current interpolated sample
            float* prev_sample = ch_info->liner_buf[1]; // Previous sample for interpolation

            // int diff = ch_info->diff; // Sample rate converter accumulator
            // size_t in_idx = ch_info->index; // Source audio position
            size_t out_idx = 0; // Destination buffer position

            // Mix loop: read source samples and resample to output rate
            do
            {
                // Read new source samples when accumulator is non-negative
                do
                {
                    // Handle loop wrap-around
                    if (ch_info->index >= ch_info->wav()->length)
                    {
                        ch_info->index -= ch_info->wav()->length;
                        // not infinite repeat? decrement and check if repeat count is 0
                        if (ch_info->wav()->repeat != ~0u && --ch_info->wav()->repeat == 0)
                        {
                            // Save state before switching
                            if (!_get_next_wav(ch))
                                goto no_more_samples;
                        }
                    }

                    // Read sample (L and R channels)
                    int32_t left, right;
                    if (ch_info->wav()->is_16bit)
                    {
                        auto data16 = (const int16_t*)ch_info->wav()->data;
                        left = data16[ch_info->index];
                        right = data16[ch_info->index + in_stereo];
                        ch_info->index += 1 + in_stereo;

                        if (!ch_info->wav()->is_signed)
                        {
                            left = (left & 0xFFFF) + INT16_MIN;
                            right = (right & 0xFFFF) + INT16_MIN;
                        }
                    }
                    else
                    {
                        auto data8 = (const uint8_t*)ch_info->wav()->data;
                        left = data8[ch_info->index];
                        right = data8[ch_info->index + in_stereo];
                        ch_info->index += 1 + in_stereo;

                        if (ch_info->wav()->is_signed)
                        {
                            left = (int8_t)left;
                            right = (int8_t)right;
                        }
                        else
                        {
                            left += INT8_MIN;
                            right += INT8_MIN;
                        }
                    }

                    // Store for interpolation and apply volume
                    prev_sample[0] = curr_sample[0];
                    if (out_stereo)
                    {
                        prev_sample[1] = curr_sample[1];
                        curr_sample[1] = right * ch_volume;
                    }
                    else
                    {
                        left += right; // Mix stereo to mono
                    }
                    curr_sample[0] = left * ch_volume;

                    ch_info->diff -= out_rate_x256; // reduce diff by output rate, then we will add input
                } while (ch_info->diff >= 0);
                // Linear interpolation: generate output samples between prev and curr
                float lerp_left = curr_sample[0];
                float delta_left = lerp_left - prev_sample[0];
                float start_left = lerp_left * out_rate_x256 + delta_left * ch_info->diff;
                float step_left = delta_left * in_rate_x256;

                if (out_stereo)
                {
                    float lerp_right = curr_sample[1];
                    float delta_right = lerp_right - prev_sample[1];
                    float start_right = lerp_right * out_rate_x256 + delta_right * ch_info->diff;
                    float step_right = delta_right * in_rate_x256;

                    // Write stereo samples
                    do
                    {
                        mix_buf[out_idx++] += (int32_t)start_left;
                        mix_buf[out_idx++] += (int32_t)start_right;
                        start_left += step_left;
                        start_right += step_right;
                        ch_info->diff += in_rate_x256; // now increasing diff
                    } while (out_idx < samples && ch_info->diff < 0);
                }
                else
                {
                    // Write mono samples
                    do
                    {
                        mix_buf[out_idx++] += (int32_t)start_left;
                        start_left += step_left;
                        ch_info->diff += in_rate_x256;
                    } while (out_idx < samples && ch_info->diff < 0);
                }
                // get max channel output samples
                if (out_idx > mix_samples)
                {
                    mix_samples = out_idx;
                }
            } while (out_idx < samples);
        no_more_samples:
            // continue to next channel
        }
        // Convert mixed int32 buffer to int16 output with clamping
        for (size_t i = 0; i < mix_samples; i++)
        {
            // scale down to 256
            int32_t val = mix_buf[i] >> 8;
            output[i] = (val < INT16_MIN) ? INT16_MIN : (val > INT16_MAX) ? INT16_MAX : (int16_t)val;
        }
        return mix_samples;
    }

    void Speaker::spk_task(void* args)
    {
        Speaker* self = static_cast<Speaker*>(args);
        const size_t samples_per_frame = self->_cfg.dma_buf_len;
        const size_t buffer_size = samples_per_frame << self->_cfg.stereo;
        // Allocate int32 mixing buffer for better precision
        self->mix_buf = new int32_t[buffer_size];

        uint8_t buf_cnt = 0;
        bool flg_nodata = false;

        while (self->_task_running)
        {
            // Handle no-data state - send silence and wait
            if (flg_nodata)
            {
                if (buf_cnt)
                {
                    // Decrement buffer count and wait
                    --buf_cnt;
                    // wait time = 1ms + frame playback time
                    uint32_t wait_msec = 1 + (samples_per_frame * 1000 / self->_cfg.sample_rate);
                    flg_nodata = (0 == ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(wait_msec)));
                }

                if (flg_nodata && 0 == buf_cnt)
                {
                    // Fill all DMA buffers with silence
                    memset(self->mix_buf, 0, buffer_size * sizeof(int32_t));
                    size_t retry = self->_cfg.dma_buf_count + 1;
                    while (!ulTaskNotifyTake(pdTRUE, 0) && --retry)
                    {
                        size_t bytes_written;
                        i2s_channel_write(self->_tx_chan,
                                          self->mix_buf,
                                          buffer_size * sizeof(int32_t),
                                          &bytes_written,
                                          portMAX_DELAY);
                    }

                    if (!retry)
                    {
                        // Wait for new data
                        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    }
                }
            }

            // Clear any pending notifications to avoid spinning too fast
            ulTaskNotifyTake(pdTRUE, 0);

            if (!self->_task_running)
            {
                break;
            }

            flg_nodata = true;
            size_t mix_samples = 0;

            if (self->_play_channel_bits.load() == 0)
            {
                // No channels playing, send silence
                memset(self->mix_buf, 0, buffer_size * sizeof(int32_t));
            }
            else
            {
                // Mix channels
                mix_samples = self->_mix_channels(samples_per_frame);
                flg_nodata = mix_samples == 0;
            }

            // Track buffer count
            if (!flg_nodata)
            {
                // Write to I2S - this blocks until buffer space available
                size_t bytes_to_send = mix_samples * sizeof(int16_t);
                size_t bytes_total = 0;
                do
                {
                    size_t bytes_written = 0;
                    if (i2s_channel_write(self->_tx_chan,
                                          self->mix_buf + bytes_total,
                                          bytes_to_send - bytes_total,
                                          &bytes_written,
                                          portMAX_DELAY) == ESP_OK)
                    {
                        bytes_total += bytes_written;
                    }
                    else
                    {
                        // error, wait
                        ESP_LOGE(TAG, "I2S write error");
                        break;
                    }
                } while (bytes_total < bytes_to_send);

                if (++buf_cnt >= self->_cfg.dma_buf_count)
                {
                    buf_cnt = self->_cfg.dma_buf_count;
                }
            }
        }

        delete[] self->mix_buf;
        vTaskDelete(nullptr);
    }

} // namespace HAL
