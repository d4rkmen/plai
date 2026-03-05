/**
 * @file speaker.h
 * @brief Speaker class for ESP32S3 using ESP-IDF I2S driver
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "hal/board.h"

// Pin configuration macros for M5Cardputer
#define SPEAKER_PIN_DATA_OUT 42
#define SPEAKER_PIN_BCK 41
#define SPEAKER_PIN_WS 43
#define SPEAKER_I2S_PORT I2S_NUM_1
#define SPEAKER_I2C_PORT I2C_NUM_1
#define SPEAKER_I2C_ADDR 0x18
#define SPEAKER_I2C_FREQ_HZ 400000
#define SPEAKER_I2C_TIMEOUT_MS 1000

namespace HAL
{
    class Hal;
    /**
     * @brief Configuration structure for Speaker
     */
    struct speaker_config_t
    {
        /// I2S data out pin (for speaker)
        int pin_data_out = SPEAKER_PIN_DATA_OUT;

        /// I2S bit clock pin
        int pin_bck = SPEAKER_PIN_BCK;

        /// I2S word select pin (LRCK)
        int pin_ws = SPEAKER_PIN_WS;

        /// Output sampling rate (Hz)
        uint32_t sample_rate = 48000;

        /// Use stereo output
        bool stereo = false;

        /// Multiplier for output value
        uint8_t magnification = 16;

        /// For I2S DMA buffer length (max 1024)
        size_t dma_buf_len = 256;

        /// For I2S DMA buffer count
        size_t dma_buf_count = 8;

        /// Background task priority
        uint8_t task_priority = 2;

        /// Background task pinned core
        uint8_t task_pinned_core = 1;

        /// I2S port
        i2s_port_t i2s_port = SPEAKER_I2S_PORT;
    };

    /**
     * @brief Speaker class compatible with M5Unified Speaker_Class API
     */
    class Speaker
    {
    public:
        Speaker(HAL::Hal* hal);
        virtual ~Speaker();

        /**
         * @brief Get current configuration
         */
        speaker_config_t config(void) const { return _cfg; }

        /**
         * @brief Set configuration
         */
        void config(const speaker_config_t& cfg) { _cfg = cfg; }

        /**
         * @brief Initialize and start the speaker
         */
        bool begin(void);

        /**
         * @brief Stop and deinitialize the speaker
         */
        void end(void);

        /**
         * @brief Check if speaker task is running
         */
        bool isRunning(void) const { return _task_running; }

        /**
         * @brief Check if speaker is enabled
         */
        bool isEnabled(void) const { return _cfg.pin_data_out >= 0; }

        /**
         * @brief Check if any channel is playing
         * @return true if playing
         */
        bool isPlaying(void) const volatile { return _play_channel_bits.load(); }

        /**
         * @brief Check if specific channel is playing
         * @param channel Channel number (0-7)
         * @return 0=not playing / 1=playing (room in queue) / 2=playing (no room in queue)
         */
        size_t isPlaying(uint8_t channel) const volatile;

        /**
         * @brief Get number of channels playing
         */
        size_t getPlayingChannels(void) const volatile;

        /**
         * @brief Set master volume
         * @param master_volume Volume (0-255)
         */
        void setVolume(uint8_t master_volume) { _master_volume = master_volume; }

        /**
         * @brief Get master volume
         */
        uint8_t getVolume(void) const { return _master_volume; }

        /**
         * @brief Set all channel volumes
         * @param volume Volume (0-255)
         */
        void setAllChannelVolume(uint8_t volume);

        /**
         * @brief Set specific channel volume
         * @param channel Channel number (0-7)
         * @param volume Volume (0-255)
         */
        void setChannelVolume(uint8_t channel, uint8_t volume);

        /**
         * @brief Get specific channel volume
         * @param channel Channel number (0-7)
         */
        uint8_t getChannelVolume(uint8_t channel) const;

        /**
         * @brief Stop all sound output
         */
        void stop(void);

        /**
         * @brief Stop specific channel
         * @param channel Channel number (0-7)
         */
        void stop(uint8_t channel);

        /**
         * @brief Play tone with custom waveform
         * @param frequency Tone frequency (Hz)
         * @param duration Duration (msec), UINT32_MAX for infinite
         * @param channel Channel number (-1 for auto)
         * @param stop_current_sound Stop current sound on channel
         * @param raw_data Waveform data (8bit unsigned)
         * @param array_len Size of waveform data
         * @param stereo Is stereo
         */
        bool tone(float frequency,
                  uint32_t duration,
                  int channel,
                  bool stop_current_sound,
                  const uint8_t* raw_data,
                  size_t array_len,
                  bool stereo = false);

        /**
         * @brief Play simple tone
         * @param frequency Tone frequency (Hz)
         * @param duration Duration (msec), UINT32_MAX for infinite
         * @param channel Channel number (-1 for auto)
         * @param stop_current_sound Stop current sound on channel
         */
        bool tone(float frequency, uint32_t duration = UINT32_MAX, int channel = -1, bool stop_current_sound = true);

        /**
         * @brief Play raw sound data (signed 8bit)
         */
        bool playRaw(const int8_t* raw_data,
                     size_t array_len,
                     uint32_t sample_rate = 44100,
                     bool stereo = false,
                     uint32_t repeat = 1,
                     int channel = -1,
                     bool stop_current_sound = false);

        /**
         * @brief Play raw sound data (unsigned 8bit)
         */
        bool playRaw(const uint8_t* raw_data,
                     size_t array_len,
                     uint32_t sample_rate = 44100,
                     bool stereo = false,
                     uint32_t repeat = 1,
                     int channel = -1,
                     bool stop_current_sound = false);

        /**
         * @brief Play raw sound data (signed 16bit)
         */
        bool playRaw(const int16_t* raw_data,
                     size_t array_len,
                     uint32_t sample_rate = 44100,
                     bool stereo = false,
                     uint32_t repeat = 1,
                     int channel = -1,
                     bool stop_current_sound = false);

        /**
         * @brief Play WAV format data
         * @param wav_data WAV data with header
         * @param data_len Length of data
         * @param repeat Repeat count (1 for once)
         * @param channel Channel number (-1 for auto)
         * @param stop_current_sound Stop current sound on channel
         */
        bool playWav(const uint8_t* wav_data,
                     size_t data_len = ~0u,
                     uint32_t repeat = 1,
                     int channel = -1,
                     bool stop_current_sound = false);
        inline HAL::Hal* hal() { return _hal; }

    private:
        HAL::Hal* _hal;
        BoardType _board_type;
        // i2c_master_bus_handle_t _bus_handle;
        i2c_master_dev_handle_t _dev_handle;
        static constexpr const size_t CHANNELS_NUM = 8;
        static const uint8_t _default_tone_wav[16];
        int32_t* mix_buf = nullptr;

        /**
         * @brief Wave information structure
         */
        struct wav_info_t
        {
            volatile uint32_t repeat = 0; // -1 means infinite repeat
            uint32_t sample_rate_x256 = 0;
            const void* data = nullptr;
            size_t length = 0;
            union
            {
                volatile uint8_t flg = 0;
                struct
                {
                    uint8_t is_stereo : 1;
                    uint8_t is_16bit : 1;
                    uint8_t is_signed : 1;
                    uint8_t stop_current : 1;
                    uint8_t no_clear_index : 1;
                };
            };

            void clear(void);
        };

        /**
         * @brief Channel information structure
         */
        struct channel_info_t
        {
            wav_info_t wavinfo[2]; // current/next flip info
            size_t index = 0;
            int diff = 0;
            volatile uint8_t volume = 255; // channel volume
            volatile bool flip = false;

            float liner_buf[2][2] = {{0, 0}, {0, 0}};
            inline wav_info_t* wav(void) { return &wavinfo[!flip]; }
            inline wav_info_t* next(void) { return &wavinfo[flip]; }
        };

        channel_info_t _ch_info[CHANNELS_NUM];

        speaker_config_t _cfg;
        volatile uint8_t _master_volume = 64;

        volatile bool _task_running = false;
        std::atomic<uint16_t> _play_channel_bits = {0};

        TaskHandle_t _task_handle = nullptr;
        volatile SemaphoreHandle_t _task_semaphore = nullptr;

        i2s_chan_handle_t _tx_chan = nullptr;

        /**
         * @brief Speaker task
         */
        static void spk_task(void* args);

        /**
         * @brief Setup I2S interface
         */
        bool _setup_i2s(void);

        /**
         * @brief Internal play raw function
         */
        bool _play_raw(const void* wav,
                       size_t array_len,
                       bool flg_16bit,
                       bool flg_signed,
                       float sample_rate,
                       bool flg_stereo,
                       uint32_t repeat_count,
                       int channel,
                       bool stop_current_sound,
                       bool no_clear_index);

        /**
         * @brief Set next wave for channel
         */
        bool _set_next_wav(size_t ch, const wav_info_t& wav);

        /**
         * @brief Switch to next queued wav for channel
         * @param ch Channel index
         * @return true if channel has data to play, false if channel is done
         */
        bool _get_next_wav(size_t ch);
        /**
         * @brief Mix audio channels
         */
        size_t _mix_channels(size_t samples);

        /**
         * @brief Initialize cardputer adv
         *
         * @param enabled true to enable, false to disable
         * @return true if successful, false otherwise
         */
        bool _init_cardputer_adv(bool enabled);
    };

} // namespace HAL
