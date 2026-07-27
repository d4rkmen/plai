/**
 * @file mesh_service.cpp
 * @author d4rkmen
 * @brief Meshtastic protocol service implementation
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "mesh_service.h"
#include "node_db.h"
#include "mesh_data.h"
#include "common_define.h"
#include "esp_log.h"
#include <time.h>
#include <format>
#include "esp_mac.h"
#include "esp_random.h"
#if __has_include("mbedtls/aes.h")
#ifndef MBEDTLS_ALLOW_PRIVATE_ACCESS
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#endif
#include "mbedtls/aes.h"
#include "mbedtls/ccm.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#define MESH_HAS_MBEDTLS 1
#else
#define MESH_HAS_MBEDTLS 0
#endif
#include <pb_encode.h>
#include <pb_decode.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include "hal/gps/gps.h"

static const char* TAG = "MESH";

namespace Mesh
{

    static void expandGreetingTemplate(const char* tmpl,
                                       char* out,
                                       size_t out_size,
                                       const NodeInfo* node,
                                       uint32_t node_id,
                                       uint8_t hops,
                                       int16_t rssi,
                                       float snr);

// Region definitions
#define RDEF(name, freq_start, freq_end, duty_cycle, spacing, power_limit, audio_permitted, frequency_switching, wide_lora)    \
    {meshtastic_Config_LoRaConfig_RegionCode_##name,                                                                           \
     freq_start,                                                                                                               \
     freq_end,                                                                                                                 \
     duty_cycle,                                                                                                               \
     spacing,                                                                                                                  \
     power_limit,                                                                                                              \
     audio_permitted,                                                                                                          \
     frequency_switching,                                                                                                      \
     wide_lora,                                                                                                                \
     #name}

    static const RegionInfo regions[] = {RDEF(US, 902.0f, 928.0f, 100, 0, 30, true, false, false),
                                         RDEF(EU_433, 433.0f, 434.0f, 10, 0, 10, true, false, false),
                                         RDEF(EU_868, 869.4f, 869.65f, 10, 0, 27, false, false, false),
                                         RDEF(CN, 470.0f, 510.0f, 100, 0, 19, true, false, false),
                                         RDEF(JP, 920.5f, 923.5f, 100, 0, 13, true, false, false),
                                         RDEF(ANZ, 915.0f, 928.0f, 100, 0, 30, true, false, false),
                                         RDEF(KR, 920.0f, 923.0f, 100, 0, 23, true, false, false),
                                         RDEF(TW, 920.0f, 925.0f, 100, 0, 27, true, false, false),
                                         RDEF(RU, 868.7f, 869.2f, 100, 0, 20, true, false, false),
                                         RDEF(IN, 865.0f, 867.0f, 100, 0, 30, true, false, false),
                                         RDEF(NZ_865, 864.0f, 868.0f, 100, 0, 36, true, false, false),
                                         RDEF(TH, 920.0f, 925.0f, 100, 0, 16, true, false, false),
                                         RDEF(LORA_24, 2400.0f, 2483.5f, 100, 0, 10, true, false, true),
                                         RDEF(UA_433, 433.0f, 434.7f, 10, 0, 30, true, false, false),
                                         RDEF(UA_868, 868.0f, 868.6f, 1, 0, 14, true, false, false),
                                         RDEF(MY_433, 433.0f, 435.0f, 100, 0, 20, true, false, false),
                                         RDEF(MY_919, 919.0f, 924.0f, 100, 0, 27, true, true, false),
                                         RDEF(SG_923, 917.0f, 925.0f, 100, 0, 20, true, false, false),
                                         RDEF(PH_433, 433.0f, 434.7f, 100, 0, 10, true, false, false),
                                         RDEF(PH_868, 868.0f, 869.4f, 100, 0, 14, true, false, false),
                                         RDEF(PH_915, 915.0f, 918.0f, 100, 0, 24, true, false, false),
                                         RDEF(UNSET, 902.0f, 928.0f, 100, 0, 30, true, false, false)};

    // Modem preset lookup table (indexed by meshtastic_Config_LoRaConfig_ModemPreset enum 0..9)
    //                                                preset enum                                                       name
    //                                                short    bw      bw_wide    cr  sf
    const ModemPresetInfo modem_presets[MODEM_PRESET_COUNT] = {
        {meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, "LongFast", "LongF", 250.0f, 812.5f, 5, 11},
        {meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, "LongSlow", "LongS", 125.0f, 406.25f, 8, 12},
        {meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW, "VeryLongSlow", "VLongS", 62.5f, 203.125f, 8, 12},
        {meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW, "MediumSlow", "MedS", 250.0f, 812.5f, 5, 10},
        {meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, "MediumFast", "MedF", 250.0f, 812.5f, 5, 9},
        {meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW, "ShortSlow", "ShrtS", 250.0f, 812.5f, 5, 8},
        {meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST, "ShortFast", "ShrtF", 250.0f, 812.5f, 5, 7},
        {meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE, "LongMod", "LongM", 125.0f, 406.25f, 8, 11},
        {meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, "ShortTurbo", "ShrtT", 500.0f, 1625.0f, 5, 7},
        {meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO, "LongTurbo", "LongT", 500.0f, 1625.0f, 8, 11},
    };

    // Hash function for channel name (djb2)
    static uint32_t hashChannelName(const char* str)
    {
        uint32_t hash = 5381;
        int c;
        while ((c = *str++) != 0)
        {
            hash = ((hash << 5) + hash) + (unsigned char)c; /* hash * 33 + c */
        }
        return hash;
    }

    namespace
    {
        constexpr uint8_t kDefaultPsk[16] = {
            0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

        void writeU32Le(uint8_t* dst, uint32_t value)
        {
            dst[0] = (uint8_t)(value & 0xFFu);
            dst[1] = (uint8_t)((value >> 8) & 0xFFu);
            dst[2] = (uint8_t)((value >> 16) & 0xFFu);
            dst[3] = (uint8_t)((value >> 24) & 0xFFu);
        }

        void writeU64Le(uint8_t* dst, uint64_t value)
        {
            dst[0] = (uint8_t)(value & 0xFFu);
            dst[1] = (uint8_t)((value >> 8) & 0xFFu);
            dst[2] = (uint8_t)((value >> 16) & 0xFFu);
            dst[3] = (uint8_t)((value >> 24) & 0xFFu);
            dst[4] = (uint8_t)((value >> 32) & 0xFFu);
            dst[5] = (uint8_t)((value >> 40) & 0xFFu);
            dst[6] = (uint8_t)((value >> 48) & 0xFFu);
            dst[7] = (uint8_t)((value >> 56) & 0xFFu);
        }

        // PKC overhead: 8-byte AES-CCM auth tag + 4-byte extra nonce
        constexpr size_t PKC_OVERHEAD = 12;

#if MESH_HAS_MBEDTLS
        // RNG callback for mbedtls (required by ecp_mul for side-channel blinding)
        int esp_rng_cb(void* /*ctx*/, unsigned char* buf, size_t len)
        {
            esp_fill_random(buf, len);
            return 0;
        }
        /**
         * Encrypt payload using PKC (X25519 DH + SHA256 + AES-256-CCM).
         * Compatible with Meshtastic firmware encryptCurve25519().
         *
         * @param our_private_key  Our 32-byte X25519 private key
         * @param remote_public_key  Remote node's 32-byte X25519 public key
         * @param from_node  Our node ID
         * @param packet_id  Packet ID
         * @param plaintext  Input plaintext
         * @param plaintext_len  Length of plaintext
         * @param out  Output buffer (must hold plaintext_len + PKC_OVERHEAD bytes)
         * @return true on success
         */
        /**
         * Compute X25519 shared secret using mbedtls ECP low-level API.
         * Performs scalar multiplication: shared = clamp(private_key) * public_point
         * Result is written as 32 little-endian bytes.
         */
        bool x25519_shared_secret(const uint8_t* our_private_key, const uint8_t* remote_public_key, uint8_t* shared_out)
        {
            mbedtls_ecp_group grp;
            mbedtls_ecp_point Q;
            mbedtls_mpi d, z;

            mbedtls_ecp_group_init(&grp);
            mbedtls_ecp_point_init(&Q);
            mbedtls_mpi_init(&d);
            mbedtls_mpi_init(&z);

            bool ok = false;

            // Load Curve25519 group
            if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) != 0)
            {
                ESP_LOGE(TAG, "PKC: ecp_group_load failed");
                goto cleanup;
            }

            // Load private key scalar (little-endian, native X25519 format)
            if (mbedtls_mpi_read_binary_le(&d, our_private_key, 32) != 0)
            {
                ESP_LOGE(TAG, "PKC: failed to load private key");
                goto cleanup;
            }

            // Load remote public key as Montgomery point (X coordinate only)
            if (mbedtls_mpi_read_binary_le(&Q.MBEDTLS_PRIVATE(X), remote_public_key, 32) != 0 ||
                mbedtls_mpi_lset(&Q.MBEDTLS_PRIVATE(Z), 1) != 0)
            {
                ESP_LOGE(TAG, "PKC: failed to load remote public key");
                goto cleanup;
            }

            // Compute shared secret z = d * Q (X25519 scalar multiplication)
            // Note: mbedtls requires RNG for Montgomery curve blinding (side-channel protection)
            if (mbedtls_ecdh_compute_shared(&grp, &z, &Q, &d, esp_rng_cb, nullptr) != 0)
            {
                ESP_LOGE(TAG, "PKC: DH compute_shared failed");
                goto cleanup;
            }

            // Write result as 32 little-endian bytes (native X25519 format)
            memset(shared_out, 0, 32);
            if (mbedtls_mpi_write_binary_le(&z, shared_out, 32) != 0)
            {
                ESP_LOGE(TAG, "PKC: failed to write shared secret");
                goto cleanup;
            }

            ok = true;

        cleanup:
            mbedtls_mpi_free(&z);
            mbedtls_mpi_free(&d);
            mbedtls_ecp_point_free(&Q);
            mbedtls_ecp_group_free(&grp);
            return ok;
        }

        bool encryptPKC(const uint8_t* our_private_key,
                        const uint8_t* remote_public_key,
                        uint32_t from_node,
                        uint32_t packet_id,
                        const uint8_t* plaintext,
                        size_t plaintext_len,
                        uint8_t* out)
        {
            // Step 1: X25519 DH - compute shared secret
            uint8_t shared_key[32] = {};
            if (!x25519_shared_secret(our_private_key, remote_public_key, shared_key))
            {
                return false;
            }

            // Step 2: SHA256 hash of shared secret (matches Meshtastic CryptoEngine::hash)
            mbedtls_sha256(shared_key, 32, shared_key, 0); // SHA-256 (not SHA-224)

            // Step 3: Generate random extra nonce
            uint32_t extra_nonce = esp_random();

            // Step 4: Build 13-byte nonce (matches Meshtastic CryptoEngine::initNonce with extraNonce)
            // nonce[0..7] = packetId, then nonce[4..7] overwritten by extraNonce, nonce[8..11] = fromNode
            uint8_t nonce[13] = {};
            uint64_t pkt_id_64 = (uint64_t)packet_id;
            memcpy(nonce, &pkt_id_64, sizeof(uint64_t));       // nonce[0..7] = packet_id
            memcpy(nonce + 4, &extra_nonce, sizeof(uint32_t)); // nonce[4..7] = extra_nonce (overwrites upper 4 bytes of id)
            memcpy(nonce + 8, &from_node, sizeof(uint32_t));   // nonce[8..11] = from_node
            // nonce[12] = 0

            // Step 5: AES-256-CCM encrypt with 8-byte auth tag
            mbedtls_ccm_context ccm;
            mbedtls_ccm_init(&ccm);

            if (mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, shared_key, 256) != 0)
            {
                mbedtls_ccm_free(&ccm);
                ESP_LOGE(TAG, "PKC: ccm_setkey failed");
                return false;
            }

            // out layout: ciphertext(plaintext_len) + auth_tag(8) + extra_nonce(4)
            uint8_t* ciphertext = out;
            uint8_t* auth_tag = out + plaintext_len;

            int ret = mbedtls_ccm_encrypt_and_tag(&ccm,
                                                  plaintext_len,
                                                  nonce,
                                                  13, // 13-byte nonce
                                                  nullptr,
                                                  0, // no AAD
                                                  plaintext,
                                                  ciphertext,
                                                  auth_tag,
                                                  8); // 8-byte auth tag
            mbedtls_ccm_free(&ccm);

            if (ret != 0)
            {
                ESP_LOGE(TAG, "PKC: AES-CCM encrypt failed (%d)", ret);
                return false;
            }

            // Append extra nonce after auth tag
            memcpy(auth_tag + 8, &extra_nonce, sizeof(uint32_t));

            ESP_LOGD(TAG, "PKC encrypt ok: %u + %u overhead bytes", (unsigned)plaintext_len, (unsigned)PKC_OVERHEAD);
            return true;
        }
#endif // MESH_HAS_MBEDTLS

        uint8_t xorHash(const uint8_t* data, size_t len)
        {
            uint8_t code = 0;
            for (size_t i = 0; i < len; ++i)
            {
                code ^= data[i];
            }
            return code;
        }

        const char* getPresetDisplayName(meshtastic_Config_LoRaConfig_ModemPreset preset) { return getPresetName(preset); }

        const char* getChannelNameForHash(const Mesh::MeshConfig& config)
        {
            return resolveChannelName(config.primary_channel.settings, config);
        }

        bool expandChannelPsk(const meshtastic_ChannelSettings& settings, uint8_t* key, size_t& key_len, bool& no_crypto)
        {
            key_len = 0;
            no_crypto = false;

            const size_t psk_len = settings.psk.size;
            if (psk_len == 0 || (psk_len == 1 && settings.psk.bytes[0] == 0))
            {
                no_crypto = true;
                return true;
            }

            if (psk_len == 1)
            {
                memcpy(key, kDefaultPsk, sizeof(kDefaultPsk));
                key[sizeof(kDefaultPsk) - 1] = (uint8_t)(key[sizeof(kDefaultPsk) - 1] + (settings.psk.bytes[0] - 1));
                key_len = sizeof(kDefaultPsk);
                return true;
            }

            if (psk_len == 16 || psk_len == 32)
            {
                memcpy(key, settings.psk.bytes, psk_len);
                key_len = psk_len;
                return true;
            }

            if (psk_len < 16)
            {
                memcpy(key, settings.psk.bytes, psk_len);
                memset(key + psk_len, 0, 16 - psk_len);
                key_len = 16;
                return true;
            }

            if (psk_len < 32)
            {
                memcpy(key, settings.psk.bytes, psk_len);
                memset(key + psk_len, 0, 32 - psk_len);
                key_len = 32;
                return true;
            }

            return false;
        }

        bool computeChannelHash(const Mesh::MeshConfig& config, const uint8_t* key, size_t key_len, uint8_t& out_hash)
        {
            const char* channelName = getChannelNameForHash(config);
            uint8_t nameHash = xorHash(reinterpret_cast<const uint8_t*>(channelName), strlen(channelName));
            uint8_t keyHash = key_len == 0 ? 0 : xorHash(key, key_len);
            out_hash = nameHash ^ keyHash;
            return true;
        }

        bool computeChannelHashFromSettings(const meshtastic_ChannelSettings& settings,
                                            const Mesh::MeshConfig& config,
                                            const uint8_t* key,
                                            size_t key_len,
                                            uint8_t& out_hash)
        {
            const char* channelName = resolveChannelName(settings, config);
            uint8_t nameHash = xorHash(reinterpret_cast<const uint8_t*>(channelName), strlen(channelName));
            uint8_t keyHash = key_len == 0 ? 0 : xorHash(key, key_len);
            out_hash = nameHash ^ keyHash;
            return true;
        }

        bool matchDefaultPresetForHash(uint8_t channelHash, uint8_t* key, size_t& key_len)
        {
            for (int preset = _meshtastic_Config_LoRaConfig_ModemPreset_MIN;
                 preset <= _meshtastic_Config_LoRaConfig_ModemPreset_MAX;
                 ++preset)
            {
                const char* name = getPresetDisplayName(static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(preset));
                if (!name || strcmp(name, "Invalid") == 0)
                {
                    continue;
                }

                uint8_t h = xorHash(reinterpret_cast<const uint8_t*>(name), strlen(name));
                uint8_t tmp = h ^ xorHash(kDefaultPsk, sizeof(kDefaultPsk));
                if (tmp == channelHash)
                {
                    memcpy(key, kDefaultPsk, sizeof(kDefaultPsk));
                    key_len = sizeof(kDefaultPsk);
                    return true;
                }
            }
            return false;
        }
    } // namespace

    const char* resolveChannelName(const meshtastic_ChannelSettings& settings, const MeshConfig& config)
    {
        if (settings.name[0] != '\0')
        {
            return settings.name;
        }
        if (config.lora_config.use_preset)
        {
            return getPresetName(config.lora_config.modem_preset);
        }
        return "Custom";
    }

    // Static instance for callbacks
    MeshService* MeshService::_instance = nullptr;

    MeshService::MeshService(HAL::Hal* hal)
        : _my_region(nullptr), _bw(250.0f), _sf(11), _cr(5), _saved_freq(0.0f), _saved_channel_num(0), _radio(nullptr),
          _gps(nullptr), _gps_queue(nullptr), _nodedb(nullptr), _router(), _config(), _state(MeshState::UNINITIALIZED),
          _message_callback(nullptr), _battery_callback(nullptr), _fromradio_state(FromRadioState::IDLE),
          _fromradio_config_id(0), _fromradio_node_index(0), _fromradio_channel_index(0), _last_nodeinfo_broadcast_ms(0),
          _force_nodeinfo_broadcast(false), _last_neighborinfo_broadcast_ms(0), _last_position_broadcast_ms(0),
          _last_telemetry_broadcast_ms(0), _tx_in_progress(false), _last_tx_start_ms(0), _last_rx_rssi(0), _last_rx_snr(0.0f),
          _airtime_window_start_ms(0), _airtime_tx_ms(0), _airtime_rx_ms(0), _airtime_tx_ms_prev(0), _airtime_rx_ms_prev(0),
          _slot_time_ms(28), _tx_not_before_ms(0), _cad_in_progress(false)
    {
        _hal = hal;
        memset(&_config, 0, sizeof(_config));
        _gps_queue = xQueueCreate(1, sizeof(HAL::GpsData));
        _instance = this;
    }

    MeshService::~MeshService()
    {
        stop();
        if (_gps_queue)
        {
            vQueueDelete(_gps_queue);
            _gps_queue = nullptr;
        }
        _instance = nullptr;
    }

    uint8_t MeshService::getChannelHash(const meshtastic_ChannelSettings& settings) const
    {
        uint8_t key[32] = {};
        size_t key_len = 0;
        bool no_crypto = false;
        expandChannelPsk(settings, key, key_len, no_crypto);

        uint8_t hash = 0;
        computeChannelHashFromSettings(settings, _config, key, key_len, hash);
        return hash;
    }

    bool MeshService::init(HAL::RadioInterface* radio, NodeDB* nodedb, const MeshConfig& config)
    {
        ESP_LOGI(TAG, "Initializing mesh service");

        if (!radio)
        {
            ESP_LOGE(TAG, "Radio interface is required");
            return false;
        }

        _radio = radio;
        _nodedb = nodedb;

        // Initialize packet router
        if (!_router.init())
        {
            ESP_LOGE(TAG, "Failed to initialize packet router");
            return false;
        }

        // Set up router callback
        _router.setPacketReceivedCallback([this](const meshtastic_MeshPacket& packet, int16_t rssi, float snr)
                                          { onPacketReceived(packet, rssi, snr); });

        setConfig(config);

        // Pass our node ID to the router for duplicate detection / relayer tracking
        _router.setNodeId(_config.node_id);

        ESP_LOGW(TAG, "Short name: %s, Long name: %s", _config.short_name, _config.long_name);

        // Set default primary channel
        if (_config.primary_channel.settings.psk.size == 0)
        {
            _config.primary_channel.index = 0;
            _config.primary_channel.role = meshtastic_Channel_Role_PRIMARY;
            _config.primary_channel.settings.name[0] = '\0';
            _config.primary_channel.settings.psk.size = 1;
            _config.primary_channel.settings.psk.bytes[0] = 1;
        }

        _state = MeshState::STARTING;
        ESP_LOGI(TAG, "Mesh service initialized, node ID: 0x%08lX", (unsigned long)_config.node_id);
        return true;
    }

    bool MeshService::start()
    {
        ESP_LOGD(TAG, "Starting mesh service");

        // Apply LoRa configuration to radio
        applyModemConfig();

        // Set up radio event callback
        _radio->setEventCallback([this](HAL::RadioEvent event) { onRadioEvent(event); });

        // Start radio in receive mode
        if (!_radio->startReceive(0))
        {
            ESP_LOGE(TAG, "Failed to start radio RX");
            return false;
        }

        _state = MeshState::READY;
        _last_nodeinfo_broadcast_ms = millis();
        return true;
    }

    void MeshService::stop()
    {
        ESP_LOGD(TAG, "Stopping mesh service");

        // Put radio to sleep
        if (_radio)
        {
            _radio->setMode(HAL::RadioMode::SLEEP);
        }

        _state = MeshState::UNINITIALIZED;
    }

    void MeshService::update()
    {
        if (_state != MeshState::READY)
        {
            return;
        }

        // Process deferred GPS data (posted from the GPS background task)
        HAL::GpsData gps_snapshot;
        if (_gps_queue && xQueueReceive(_gps_queue, &gps_snapshot, 0) == pdTRUE)
        {
            _onGpsData(gps_snapshot);
        }

        // Process radio events
        if (_radio)
        {
            _radio->processEvents();
        }

        // Process RX queue
        _router.processRxQueue();

        // Recover if TX_DONE never arrives
        uint32_t now = millis();
        if (_tx_in_progress && _radio)
        {
            if (_radio->getMode() != HAL::RadioMode::TX)
            {
                _tx_in_progress = false;
            }
            else if (now - _last_tx_start_ms > TX_WATCHDOG_TIMEOUT_MS)
            {
                ESP_LOGW(TAG, "TX watchdog fired, forcing RX restart (%lu - %lu)", now, _last_tx_start_ms);
                _tx_in_progress = false;
                _radio->startReceive(0);
            }
        }

        // Ensure radio stays in RX mode when idle (skip during CAD scan)
        if (_radio && !_radio->isBusy() && !_cad_in_progress && _radio->getMode() != HAL::RadioMode::RX &&
            _radio->getMode() != HAL::RadioMode::CAD)
        {
            ESP_LOGW(TAG, "Radio not in RX mode, restarting receive");
            _radio->startReceive(0);
        }

        // Check TX queue: when CSMA/CA delay has elapsed, start CAD before transmitting
        if (_router.hasTxPackets() && _radio && !_radio->isBusy() && !_cad_in_progress && now >= _tx_not_before_ms)
        {
            startTxCAD();
        }

        // Periodic node info broadcast (every 60 seconds), or forced immediately
        if (_force_nodeinfo_broadcast || (_config.nodeinfo_broadcast_interval_ms > 0 &&
                                          now - _last_nodeinfo_broadcast_ms >= _config.nodeinfo_broadcast_interval_ms))
        {
            broadcastNodeInfo();
            _last_nodeinfo_broadcast_ms = now;
            _force_nodeinfo_broadcast = false;
        }

        // Periodic neighbor info broadcast
        if (_config.neighborinfo_enabled && _config.neighborinfo_broadcast_interval_ms > 0 &&
            now - _last_neighborinfo_broadcast_ms >= _config.neighborinfo_broadcast_interval_ms)
        {
            sendNeighborInfo(0xFFFFFFFF, _config.primary_channel.index);
            _last_neighborinfo_broadcast_ms = now;
        }

        // Periodic position broadcast (every 15 minutes)
        // Only send if we have a position source (GPS fix or fixed position)
        if (_config.position != MeshConfig::POSITION_OFF && _config.position_broadcast_interval_ms > 0 &&
            now - _last_position_broadcast_ms >= _config.position_broadcast_interval_ms)
        {
            // If fixed position is configured, only send that (privacy: never leak live GPS)
            // Otherwise, send live GPS if available
            bool should_send = false;
            if (_config.position == MeshConfig::POSITION_FIXED)
            {
                should_send = true;
                ESP_LOGD(TAG, "Broadcasting fixed position");
            }
            else if (_config.position == MeshConfig::POSITION_GPS && _gps && _gps->hasFix())
            {
                should_send = true;
                ESP_LOGD(TAG, "Broadcasting GPS position");
            }

            if (should_send)
            {
                sendPosition(0xFFFFFFFF, _config.primary_channel.index); // Broadcast on primary channel
            }

            _last_position_broadcast_ms = now;
        }

        // Periodic device telemetry broadcast (every 15 minutes)
        if (_config.telemetry_broadcast_interval_ms > 0 &&
            now - _last_telemetry_broadcast_ms >= _config.telemetry_broadcast_interval_ms)
        {
            sendDeviceTelemetry();
            _last_telemetry_broadcast_ms = now;
        }

        // Check for timed-out pending ACKs
        checkPendingAcks();
    }

    uint32_t MeshService::computeSlotTimeMsec() const
    {
        // Meshtastic-compatible slot time: time to detect a transmission has started
        // = CAD duration + propagation + Tx/Rx turnaround + MAC processing
        float prop_turnaround_mac_ms = 0.2f + 0.4f + 7.0f; // 7.6 ms
        float symbol_time_ms = (float)(1u << _sf) / _bw;   // 2^sf / bw_kHz

        if (_my_region && _my_region->wide_lora)
        {
            // 2.4 GHz CAD duration (AN1200.22 of SX1280)
            return (uint32_t)((4 + (2 * _sf + 3) / 32) * symbol_time_ms + prop_turnaround_mac_ms);
        }
        // Sub-GHz: CAD = max(2.25, NUM_SYM_CAD + 0.5) symbols, NUM_SYM_CAD = 2
        float cad_symbols = 2.5f; // max(2.25, 2 + 0.5)
        return (uint32_t)(cad_symbols * symbol_time_ms + prop_turnaround_mac_ms);
    }

    uint32_t MeshService::getTxDelayMsec() const
    {
        // Meshtastic-compatible CSMA/CA delay: random(0, 2^CWsize) * slotTime
        // CWsize is scaled by channel utilization: low util → small CW, high util → large CW
        float channel_util = _getChannelUtilization();
        uint8_t cw_size = CW_MIN + (uint8_t)(channel_util * (CW_MAX - CW_MIN) / 100.0f);
        if (cw_size > CW_MAX)
            cw_size = CW_MAX;
        uint32_t cw_slots = 1u << cw_size; // 2^CWsize
        return (esp_random() % cw_slots) * _slot_time_ms;
    }

    void MeshService::setTxDelay()
    {
        uint32_t now = millis();
        _tx_not_before_ms = now + getTxDelayMsec();
    }

    void MeshService::startTxCAD()
    {
        if (_radio && _radio->startCAD())
        {
            _cad_in_progress = true;
            ESP_LOGD(TAG, "CAD started before TX");
        }
        else
        {
            ESP_LOGW(TAG, "CAD start failed, resetting TX delay");
            setTxDelay();
        }
    }

    void MeshService::checkPendingAcks()
    {
        if (_pending_acks.empty())
            return;

        uint32_t now = millis();

        for (auto it = _pending_acks.begin(); it != _pending_acks.end();)
        {
            if (now - it->second.send_time_ms >= ACK_TIMEOUT_MS)
            {
                if (it->second.retries_left > 0)
                {
                    // Retry: re-enqueue the raw packet
                    it->second.retries_left--;
                    it->second.send_time_ms = now;
                    ESP_LOGW(TAG,
                             "ACK timeout for packet 0x%08lX, retrying (%u retries left)",
                             (unsigned long)it->first,
                             it->second.retries_left);
                    _router.enqueueTxRaw(it->second.raw_data,
                                         it->second.raw_len,
                                         PacketPriority::RELIABLE,
                                         it->second.port_hint);
                    ++it;
                }
                else
                {
                    // All retries exhausted
                    ESP_LOGW(TAG,
                             "ACK timeout for packet 0x%08lX, no retries left (waited %lums)",
                             (unsigned long)it->first,
                             (unsigned long)(now - it->second.send_time_ms));
                    MeshDataStore::getInstance().updateMessageStatus(it->first,
                                                                     it->second.dest_node_id,
                                                                     TextMessage::Status::FAILED,
                                                                     (uint8_t)meshtastic_Routing_Error_TIMEOUT,
                                                                     it->second.channel);
                    it = _pending_acks.erase(it);
                }
            }
            else
            {
                ++it;
            }
        }
    }

    uint32_t MeshService::sendText(const char* text, uint32_t dest, uint8_t channel)
    {
        if (!text || strlen(text) == 0)
        {
            return 0;
        }

        size_t text_len = strlen(text);
        if (text_len > 237)
        {
            ESP_LOGW(TAG, "Text message too long (%u bytes), truncating", (unsigned)text_len);
            text_len = 237;
        }

        // Build meshtastic_Data payload with TEXT_MESSAGE_APP portnum
        meshtastic_Data data_msg = meshtastic_Data_init_default;
        data_msg.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        data_msg.payload.size = text_len;
        memcpy(data_msg.payload.bytes, text, text_len);
        data_msg.want_response = false;
        applyOkToMqtt(data_msg);

        // Protobuf-encode the Data payload
        uint8_t data_buf[MAX_LORA_PAYLOAD] = {};
        pb_ostream_t data_stream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
        if (!pb_encode(&data_stream, meshtastic_Data_fields, &data_msg))
        {
            ESP_LOGE(TAG, "Failed to encode text Data payload: %s", PB_GET_ERROR(&data_stream));
            return 0;
        }

        bool is_broadcast = (dest == 0xFFFFFFFF);
        uint32_t packet_id = _router.generatePacketId();
        uint8_t hop_limit = _config.lora_config.hop_limit;
        bool want_ack = true;
        size_t encoded_len = data_stream.bytes_written;

        uint8_t payload[MAX_LORA_PAYLOAD] = {};
        size_t payload_len = 0;
        uint8_t header_channel = 0;
        bool use_pkc = false;

        // For DMs (non-broadcast), use PKC encryption if destination public key is known
        // Meshtastic 2.5+ firmware rejects legacy (channel-PSK) DMs for TEXT_MESSAGE_APP
        if (!is_broadcast && _config.public_key_len == 32 && _nodedb)
        {
            NodeInfo dest_node;
            if (getNode(dest, dest_node) && dest_node.info.user.public_key.size == 32)
            {
#if MESH_HAS_MBEDTLS
                // Check payload fits with PKC overhead
                if (encoded_len + PKC_OVERHEAD > (MAX_LORA_PAYLOAD - MESHTASTIC_HEADER_LENGTH))
                {
                    ESP_LOGE(TAG, "Text payload too large for PKC");
                    return 0;
                }

                if (encryptPKC(_config.private_key,
                               dest_node.info.user.public_key.bytes,
                               _config.node_id,
                               packet_id,
                               data_buf,
                               encoded_len,
                               payload))
                {
                    payload_len = encoded_len + PKC_OVERHEAD;
                    header_channel = 0; // PKC packets use channel = 0
                    use_pkc = true;
                    ESP_LOGD(TAG, "Using PKC encryption for DM to 0x%08lX", (unsigned long)dest);
                }
                else
                {
                    ESP_LOGW(TAG, "PKC encryption failed, falling back to channel PSK");
                }
#else
                ESP_LOGW(TAG, "PKC not available (no mbedtls), using channel PSK");
#endif
            }
            else
            {
                ESP_LOGW(TAG, "Destination 0x%08lX has no public key, using channel PSK", (unsigned long)dest);
            }
        }

        // Fallback or broadcast: use channel PSK encryption (AES-CTR)
        if (!use_pkc)
        {
            const meshtastic_ChannelSettings* ch_settings = &_config.primary_channel.settings;
            // if (channel != 0 && _nodedb)
            if (_nodedb)
            {
                meshtastic_Channel* ch = _nodedb->getChannel(channel);
                if (ch && ch->has_settings)
                    ch_settings = &ch->settings;
            }

            uint8_t key[32] = {};
            size_t key_len = 0;
            bool no_crypto = false;
            if (!expandChannelPsk(*ch_settings, key, key_len, no_crypto))
            {
                ESP_LOGE(TAG, "Failed to expand channel PSK for text message");
                return 0;
            }

            uint8_t ch_hash = 0;
            computeChannelHashFromSettings(*ch_settings, _config, key, key_len, ch_hash);
            header_channel = ch_hash;

            if (encoded_len > (MAX_LORA_PAYLOAD - MESHTASTIC_HEADER_LENGTH))
            {
                ESP_LOGE(TAG, "Text payload too large for LoRa");
                return 0;
            }

            if (no_crypto)
            {
                memcpy(payload, data_buf, encoded_len);
            }
            else
            {
#if MESH_HAS_MBEDTLS
                mbedtls_aes_context ctx;
                mbedtls_aes_init(&ctx);
                if (mbedtls_aes_setkey_enc(&ctx, key, (int)(key_len * 8)) != 0)
                {
                    mbedtls_aes_free(&ctx);
                    ESP_LOGE(TAG, "Failed to set AES key for text message");
                    return 0;
                }

                uint8_t nonce[16] = {};
                writeU64Le(nonce, (uint64_t)packet_id);
                writeU32Le(nonce + 8, _config.node_id);

                size_t nc_off = 0;
                uint8_t stream_block[16] = {};
                if (mbedtls_aes_crypt_ctr(&ctx, encoded_len, &nc_off, nonce, stream_block, data_buf, payload) != 0)
                {
                    mbedtls_aes_free(&ctx);
                    ESP_LOGE(TAG, "AES-CTR encrypt failed for text message");
                    return 0;
                }
                mbedtls_aes_free(&ctx);
#else
                ESP_LOGE(TAG, "AES support not available for text message");
                return 0;
#endif
            }
            payload_len = encoded_len;
        }

        // Build on-air packet header
        PacketHeader header = {};
        header.to = dest;
        header.from = _config.node_id;
        header.id = packet_id;
        header.channel = header_channel;
        header.next_hop = 0;
        header.relay_node = (uint8_t)(_config.node_id & 0xFFu);
        header.flags = (hop_limit & PACKET_FLAGS_HOP_LIMIT_MASK) |
                       ((hop_limit << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK) |
                       (want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0);

        uint8_t radio_buf[MAX_LORA_PAYLOAD] = {};
        memcpy(radio_buf, &header, sizeof(header));
        memcpy(radio_buf + sizeof(header), payload, payload_len);
        const size_t radio_len = sizeof(header) + payload_len;

        ESP_LOGI(TAG,
                 "Sending text to 0x%08lX (id=0x%08lX, %u bytes, ack=%d, pkc=%d): %s",
                 (unsigned long)dest,
                 (unsigned long)packet_id,
                 (unsigned)radio_len,
                 want_ack ? 1 : 0,
                 use_pkc ? 1 : 0,
                 text);

        PacketPriority priority = want_ack ? PacketPriority::RELIABLE : PacketPriority::DEFAULT;
        if (_router.enqueueTxRaw(radio_buf, (uint8_t)radio_len, priority, meshtastic_PortNum_TEXT_MESSAGE_APP))
        {
            // Track pending ACK for DMs (want_ack messages) with retry data
            if (want_ack)
            {
                uint32_t now = millis();
                // Evict oldest if full
                if (_pending_acks.size() >= MAX_PENDING_ACKS)
                {
                    auto oldest = _pending_acks.begin();
                    for (auto it = _pending_acks.begin(); it != _pending_acks.end(); ++it)
                    {
                        if (it->second.send_time_ms < oldest->second.send_time_ms)
                            oldest = it;
                    }
                    _pending_acks.erase(oldest);
                }
                PendingAck pa = {};
                pa.send_time_ms = now;
                pa.dest_node_id = dest;
                pa.channel = channel;
                pa.retries_left = MAX_TX_RETRIES;
                pa.raw_len = (uint8_t)radio_len;
                pa.port_hint = meshtastic_PortNum_TEXT_MESSAGE_APP;
                memcpy(pa.raw_data, radio_buf, radio_len);
                _pending_acks[packet_id] = pa;
            }
            return packet_id;
        }
        return 0;
    }

    bool MeshService::sendData(const uint8_t* data, size_t len, meshtastic_PortNum port_num, uint32_t dest)
    {
        if (!data || len == 0 || len > 237)
        {
            return false;
        }

        // Build meshtastic_Data payload
        meshtastic_Data data_msg = meshtastic_Data_init_default;
        data_msg.portnum = port_num;
        data_msg.payload.size = len;
        memcpy(data_msg.payload.bytes, data, len);
        data_msg.want_response = false;
        applyOkToMqtt(data_msg);

        uint8_t data_buf[MAX_LORA_PAYLOAD] = {};
        pb_ostream_t data_stream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
        if (!pb_encode(&data_stream, meshtastic_Data_fields, &data_msg))
        {
            ESP_LOGE(TAG, "Failed to encode Data payload: %s", PB_GET_ERROR(&data_stream));
            return false;
        }

        uint8_t raw_buf[MAX_LORA_PAYLOAD];
        size_t raw_len = 0;
        uint32_t packet_id = encryptAndSend(data_buf,
                                            data_stream.bytes_written,
                                            dest,
                                            true,
                                            _config.lora_config.hop_limit,
                                            PacketPriority::RELIABLE,
                                            port_num,
                                            raw_buf,
                                            &raw_len);

        if (packet_id == 0)
            return false;

        uint32_t now = millis();
        if (_pending_acks.size() >= MAX_PENDING_ACKS)
        {
            auto oldest = _pending_acks.begin();
            for (auto it = _pending_acks.begin(); it != _pending_acks.end(); ++it)
            {
                if (it->second.send_time_ms < oldest->second.send_time_ms)
                    oldest = it;
            }
            _pending_acks.erase(oldest);
        }
        PendingAck pa = {};
        pa.send_time_ms = now;
        pa.dest_node_id = dest;
        pa.retries_left = MAX_TX_RETRIES;
        pa.raw_len = (uint8_t)raw_len;
        pa.port_hint = port_num;
        memcpy(pa.raw_data, raw_buf, raw_len);
        _pending_acks[packet_id] = pa;
        return true;
    }

    void MeshService::setGps(HAL::GPS* gps)
    {
        if (_gps)
        {
            _gps->setDataCallback(nullptr);
        }
        _gps = gps;
        if (_gps && _gps_queue)
        {
            _gps->setDataCallback([this](const HAL::GpsData& data) { xQueueOverwrite(_gps_queue, &data); });
        }
    }

    void MeshService::_onGpsData(const HAL::GpsData& data)
    {
        // blonk yellow led
        _hal->led()->blink_once(HAL::Color(255, 255, 0), 100);
        // BUILD_TIMESTAMP is derived from the local build clock; GPS time is UTC.
        // Allow timezone/build-latency slack so valid UTC fixes are not rejected.
        if ((time_t)data.time <= BUILD_TIMESTAMP - BUILD_TIME_SLACK_S)
        {
            return;
        }
        time_t sys_now = 0;
        time(&sys_now);
        time_t drift = (time_t)data.time - sys_now;
        if (drift < 0)
            drift = -drift;
        // check if gps has fix
        if (data.has_fix)
        {
            if (!_hal->isGPSAdjusted() || drift > GPS_SIGNIFICANT_DRIFT_S)
            {
                struct timeval tv = {.tv_sec = (time_t)data.time, .tv_usec = 0};
                settimeofday(&tv, nullptr);
                if (!_hal->isGPSAdjusted())
                {
                    _hal->playNotificationSound(HAL::Hal::NotificationSound::GPS);
                    _hal->setGPSAdjusted(true);
                }
                // dump new system date abd time dd.MM.yyyy HH:mm:ss
                struct tm timeinfo;
                localtime_r(&tv.tv_sec, &timeinfo);
                ESP_LOGI(TAG,
                         "System time adjusted from GPS: %lu (drift: %lds) = %02d.%02d.%04d %02d:%02d:%02d",
                         (unsigned long)data.time,
                         (long)drift,
                         timeinfo.tm_mday,
                         timeinfo.tm_mon + 1,
                         timeinfo.tm_year + 1900,
                         timeinfo.tm_hour,
                         timeinfo.tm_min,
                         timeinfo.tm_sec);
            }
        }
        else
        {
            _hal->setGPSAdjusted(false);
        }
    }

    void MeshService::setMessageCallback(MessageCallback callback) { _message_callback = callback; }

    size_t MeshService::getNodeCount() const
    {
        if (_nodedb)
        {
            return _nodedb->getNodeCount();
        }
        return 1; // Just us
    }

    uint32_t MeshService::getNodeInfoBroadcastRemainingMs() const
    {
        if (_config.nodeinfo_broadcast_interval_ms == 0)
            return 0;
        uint32_t now = (uint32_t)millis();
        uint32_t elapsed = now - _last_nodeinfo_broadcast_ms;
        if (elapsed >= _config.nodeinfo_broadcast_interval_ms)
            return 0;
        return _config.nodeinfo_broadcast_interval_ms - elapsed;
    }

    uint32_t MeshService::getPositionBroadcastRemainingMs() const
    {
        if (_config.position_broadcast_interval_ms == 0)
            return 0;
        uint32_t now = (uint32_t)millis();
        uint32_t elapsed = now - _last_position_broadcast_ms;
        if (elapsed >= _config.position_broadcast_interval_ms)
            return 0;
        return _config.position_broadcast_interval_ms - elapsed;
    }

    uint32_t MeshService::getTelemetryBroadcastRemainingMs() const
    {
        if (_config.telemetry_broadcast_interval_ms == 0)
            return 0;
        uint32_t now = (uint32_t)millis();
        uint32_t elapsed = now - _last_telemetry_broadcast_ms;
        if (elapsed >= _config.telemetry_broadcast_interval_ms)
            return 0;
        return _config.telemetry_broadcast_interval_ms - elapsed;
    }

    uint32_t MeshService::getNeighborInfoBroadcastRemainingMs() const
    {
        if (!_config.neighborinfo_enabled || _config.neighborinfo_broadcast_interval_ms == 0)
            return 0;
        uint32_t now = (uint32_t)millis();
        uint32_t elapsed = now - _last_neighborinfo_broadcast_ms;
        if (elapsed >= _config.neighborinfo_broadcast_interval_ms)
            return 0;
        return _config.neighborinfo_broadcast_interval_ms - elapsed;
    }

    bool MeshService::setConfig(const MeshConfig& config)
    {
        // Node identity
        _config.node_id = generateNodeId();
        std::string short_name = config.short_name;
        std::string long_name = config.long_name;
        if (short_name.empty())
        {
            short_name = std::format("{:04x}", _config.node_id & 0xFFFF);
        }
        if (long_name.empty())
        {
            long_name = std::format("Plai {}", short_name.c_str());
        }
        strncpy(_config.short_name, short_name.c_str(), 4);
        strncpy(_config.long_name, long_name.c_str(), sizeof(_config.long_name) - 1);
        _config.role = config.role;
        _config.rebroadcast_mode = config.rebroadcast_mode;
        _config.is_unmessagable = config.is_unmessagable;

        // LoRa & channel
        _config.lora_config = config.lora_config;
        _config.primary_channel = config.primary_channel;
        // Ensure primary channel has valid defaults
        if (_config.primary_channel.settings.psk.size == 0)
        {
            _config.primary_channel.index = 0;
            _config.primary_channel.role = meshtastic_Channel_Role_PRIMARY;
            _config.primary_channel.settings.psk.size = 1;
            _config.primary_channel.settings.psk.bytes[0] = 1;
        }

        // X25519 keypair
        if (config.public_key_len == 32)
        {
            memcpy(_config.public_key, config.public_key, 32);
            memcpy(_config.private_key, config.private_key, 32);
            _config.public_key_len = 32;
        }

        // Position configuration
        _config.position = config.position;
        _config.fixed_latitude = config.fixed_latitude;
        _config.fixed_longitude = config.fixed_longitude;
        _config.fixed_altitude = config.fixed_altitude;
        _config.position_flags = config.position_flags;

        // Neighbor info module
        _config.neighborinfo_enabled = config.neighborinfo_enabled;
        _config.neighborinfo_broadcast_interval_ms = config.neighborinfo_broadcast_interval_ms;

        // Broadcast intervals
        _config.nodeinfo_broadcast_interval_ms = config.nodeinfo_broadcast_interval_ms;
        _config.position_broadcast_interval_ms = config.position_broadcast_interval_ms;
        _config.telemetry_broadcast_interval_ms = config.telemetry_broadcast_interval_ms;

        // Device telemetry field flags
        _config.telemetry_bat_level = config.telemetry_bat_level;
        _config.telemetry_voltage = config.telemetry_voltage;
        _config.telemetry_ch_util = config.telemetry_ch_util;
        _config.telemetry_air_util = config.telemetry_air_util;
        _config.telemetry_uptime = config.telemetry_uptime;

        initRegion();
        // Apply changes
        if (_state == MeshState::READY && _radio)
        {
            applyModemConfig();
            _radio->startReceive(0);
        }

        return true;
    }

    uint32_t MeshService::_estimateAirtimeMs(size_t payload_len) const
    {
        // LoRa time-on-air estimation (simplified SX1262 formula)
        // Uses current radio parameters: _bw (kHz), _sf, _cr
        float bw_hz = _bw * 1000.0f;
        float t_sym = (float)(1 << _sf) / bw_hz; // Symbol time in seconds

        // Preamble time: (preamble_len + 4.25) * t_sym
        float t_preamble = (8.0f + 4.25f) * t_sym; // 8 = default preamble length

        // Payload symbols (explicit header, CRC on, low data rate optimize for SF >= 11)
        bool ldr = (_sf >= 11);
        int de = ldr ? 1 : 0;
        int header = 1; // explicit header = 1
        int pl = (int)payload_len;
        int numerator = 8 * pl - 4 * _sf + 28 + 16 - 20 * (1 - header);
        int denominator = 4 * (_sf - 2 * de);
        if (numerator < 0)
            numerator = 0;
        int n_payload = 8 + (((numerator + denominator - 1) / denominator) * (_cr));

        float t_payload = (float)n_payload * t_sym;

        float total_s = t_preamble + t_payload;
        return (uint32_t)(total_s * 1000.0f) + 1; // +1 to round up
    }

    void MeshService::_recordAirtime(uint32_t ms, bool is_tx)
    {
        uint32_t now = millis();

        // Rotate window if needed
        if (_airtime_window_start_ms == 0)
        {
            _airtime_window_start_ms = now;
        }
        else if (now - _airtime_window_start_ms >= AIRTIME_WINDOW_MS)
        {
            // Shift current to previous, reset current
            _airtime_tx_ms_prev = _airtime_tx_ms;
            _airtime_rx_ms_prev = _airtime_rx_ms;
            _airtime_tx_ms = 0;
            _airtime_rx_ms = 0;
            _airtime_window_start_ms = now;
        }

        if (is_tx)
        {
            _airtime_tx_ms += ms;
        }
        else
        {
            _airtime_rx_ms += ms;
        }
    }

    float MeshService::_getChannelUtilization() const
    {
        // Blend current window with previous for smooth 1-hour estimate
        uint32_t now = millis();
        uint32_t elapsed = (now > _airtime_window_start_ms) ? (now - _airtime_window_start_ms) : 1;
        if (elapsed > AIRTIME_WINDOW_MS)
            elapsed = AIRTIME_WINDOW_MS;

        // Weight: how far into current window (0.0 = just started, 1.0 = full window)
        float weight = (float)elapsed / (float)AIRTIME_WINDOW_MS;

        // Total activity = TX + RX (all channel usage)
        float current = (float)(_airtime_tx_ms + _airtime_rx_ms);
        float previous = (float)(_airtime_tx_ms_prev + _airtime_rx_ms_prev);

        // Blended total over a full window period
        float total_ms = current + previous * (1.0f - weight);
        float pct = (total_ms / (float)AIRTIME_WINDOW_MS) * 100.0f;
        return (pct > 100.0f) ? 100.0f : pct;
    }

    float MeshService::_getAirUtilTx() const
    {
        uint32_t now = millis();
        uint32_t elapsed = (now > _airtime_window_start_ms) ? (now - _airtime_window_start_ms) : 1;
        if (elapsed > AIRTIME_WINDOW_MS)
            elapsed = AIRTIME_WINDOW_MS;

        float weight = (float)elapsed / (float)AIRTIME_WINDOW_MS;

        float current = (float)_airtime_tx_ms;
        float previous = (float)_airtime_tx_ms_prev;

        float total_ms = current + previous * (1.0f - weight);
        float pct = (total_ms / (float)AIRTIME_WINDOW_MS) * 100.0f;
        return (pct > 100.0f) ? 100.0f : pct;
    }

    void MeshService::onRadioEvent(HAL::RadioEvent event)
    {
        switch (event)
        {
        case HAL::RadioEvent::TX_DONE:
        {
            ESP_LOGD(TAG, "Radio TX done");
            // Record TX airtime
            uint32_t tx_now = millis();
            if (_last_tx_start_ms > 0 && tx_now > _last_tx_start_ms)
            {
                _recordAirtime(tx_now - _last_tx_start_ms, true);
            }
            _tx_in_progress = false;
            setTxDelay();
            // Restart receive
            _radio->startReceive(0);
            break;
        }

        case HAL::RadioEvent::RX_DONE:
        {
            ESP_LOGD(TAG, "Radio RX done");
            uint8_t buffer[256];
            HAL::RxPacketInfo info;
            int len = _radio->readPacket(buffer, 255, &info);
            if (len > 0)
            {
                ESP_LOGD(TAG,
                         "RX packet len=%d rssi=%d snr=%.1f freq=%.3fMHz crc_ok=%s",
                         len,
                         info.rssi,
                         info.snr,
                         (float)info.frequency / 1000000.0f,
                         info.crc_ok ? "true" : "false");
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, len, ESP_LOG_DEBUG);

                // Estimate RX airtime from packet length
                _recordAirtime(_estimateAirtimeMs(len), false);

                _router.enqueueRx(buffer, len, info.rssi, info.snr);
            }
            else
            {
                ESP_LOGW(TAG, "Radio RX done but readPacket returned %d", len);
            }
            setTxDelay();
            // Continue receiving
            _radio->startReceive(0);
            break;
        }

        case HAL::RadioEvent::RX_TIMEOUT:
            ESP_LOGD(TAG, "Radio RX timeout");
            _radio->startReceive(0);
            break;

        case HAL::RadioEvent::RX_ERROR:
        {
            ESP_LOGW(TAG, "Radio RX CRC error");
#if HAL_USE_LED
            if (_hal->led())
                _hal->led()->blink_once({255, 0, 0}, 50);
#endif
            uint8_t buffer[256];
            HAL::RxPacketInfo info = {};
            int len = _radio->readPacket(buffer, 255, &info);
            if (len >= (int)sizeof(PacketHeader))
            {
                PacketHeader hdr;
                memcpy(&hdr, buffer, sizeof(hdr));
                PacketLogEntry le = {};
                le.timestamp_ms = (uint32_t)millis();
                le.from = hdr.from;
                le.to = hdr.to;
                le.id = hdr.id;
                le.size = (uint16_t)len;
                le.rssi = info.rssi;
                le.snr = info.snr;
                le.channel = hdr.channel;
                le.relay_node = hdr.relay_node;
                le.hop_limit = hdr.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
                le.hop_start = (hdr.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
                le.want_ack = (hdr.flags & PACKET_FLAGS_WANT_ACK_MASK) != 0;
                le.via_mqtt = (hdr.flags & PACKET_FLAGS_VIA_MQTT_MASK) != 0;
                le.is_tx = false;
                le.decoded = false;
                le.crc_error = true;
                le.port = 0;
                MeshDataStore::getInstance().addPacketLogEntry(le);
            }
            else if (len > 0)
            {
                PacketLogEntry le = {};
                le.timestamp_ms = (uint32_t)millis();
                le.from = 0;
                le.to = 0;
                le.id = 0;
                le.size = (uint16_t)len;
                le.rssi = info.rssi;
                le.snr = info.snr;
                le.is_tx = false;
                le.decoded = false;
                le.crc_error = true;
                le.port = 0;
                MeshDataStore::getInstance().addPacketLogEntry(le);
            }
            setTxDelay();
            _radio->startReceive(0);
            break;
        }

        case HAL::RadioEvent::CAD_DONE:
        {
            _cad_in_progress = false;
            ESP_LOGD(TAG, "CAD clear, transmitting");
            uint32_t tx_now = millis();
            QueuedPacket qp;
            if (_router.dequeueTx(qp))
            {
                ESP_LOGD(TAG, "Transmitting packet, %d bytes", qp.raw_len);
                if (_radio->transmit(qp.raw_data, qp.raw_len))
                {
                    _tx_in_progress = true;
                    _last_tx_start_ms = tx_now;
#if HAL_USE_LED
                    if (_hal->led())
                        _hal->led()->blink_once({0, 255, 0}, 50);
#endif
                    if (qp.raw_len >= sizeof(PacketHeader))
                    {
                        PacketHeader hdr;
                        memcpy(&hdr, qp.raw_data, sizeof(hdr));
                        PacketLogEntry le = {};
                        le.timestamp_ms = tx_now;
                        le.from = hdr.from;
                        le.to = hdr.to;
                        le.id = hdr.id;
                        le.size = qp.raw_len;
                        le.channel = hdr.channel;
                        le.relay_node = hdr.relay_node;
                        le.hop_limit = hdr.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
                        le.hop_start = (hdr.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
                        le.want_ack = (hdr.flags & PACKET_FLAGS_WANT_ACK_MASK) != 0;
                        le.via_mqtt = (hdr.flags & PACKET_FLAGS_VIA_MQTT_MASK) != 0;
                        le.is_tx = true;
                        le.port = qp.port_hint;
                        le.decoded = (le.port != 0);
                        MeshDataStore::getInstance().addPacketLogEntry(le);
                    }
                }
                else
                {
                    ESP_LOGW(TAG, "Radio TX start failed after CAD");
                    _radio->startReceive(0);
                }
            }
            else
            {
                ESP_LOGW(TAG, "No packet to transmit after CAD");
                _radio->startReceive(0);
            }
            break;
        }

        case HAL::RadioEvent::CAD_DETECTED:
            _cad_in_progress = false;
            ESP_LOGD(TAG, "CAD detected activity, backing off");
            setTxDelay();
            _radio->startReceive(0);
            break;

        case HAL::RadioEvent::TX_TIMEOUT:
            ESP_LOGW(TAG, "Radio TX timeout");
            _tx_in_progress = false;
            _cad_in_progress = false;
            _radio->startReceive(0);
            break;

        case HAL::RadioEvent::ERROR:
            ESP_LOGE(TAG, "Radio error");
            _tx_in_progress = false;
            _cad_in_progress = false;
            _radio->startReceive(0);
            break;
        }
    }

    void MeshService::onPacketReceived(const meshtastic_MeshPacket& packet, int16_t rssi, float snr)
    {
        ESP_LOGI(TAG, "Packet received from 0x%08lX, RSSI=%d, SNR=%.1f", (unsigned long)packet.from, rssi, snr);

#if HAL_USE_LED
        if (_hal->led())
            _hal->led()->blink_once({0, 0, 255}, 50);
#endif

        // Store RSSI/SNR for node updates and traceroute handling
        _last_rx_rssi = rssi;
        _last_rx_snr = snr;

        handleMeshPacket(packet);
    }

    meshtastic_Routing_Error MeshService::decodeMeshPacket(const meshtastic_MeshPacket& packet,
                                                           meshtastic_MeshPacket& decoded) const
    {
        decoded = packet;

        if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag)
        {
            return meshtastic_Routing_Error_NONE;
        }

        if (packet.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
        {
            return meshtastic_Routing_Error_BAD_REQUEST;
        }

        if (packet.encrypted.size == 0)
        {
            return meshtastic_Routing_Error_BAD_REQUEST;
        }

        // ---- PKC decryption attempt (channel==0, DM to us, sender has public key) ----
        // Track why PKC failed so we can return the right error code when PSK also fails.
        meshtastic_Routing_Error pkc_result = meshtastic_Routing_Error_NO_CHANNEL; // default = PKC not attempted
#if MESH_HAS_MBEDTLS
        if (packet.channel == 0 && packet.to != 0xFFFFFFFF /*&& packet.to == _config.node_id */ &&
            packet.encrypted.size > PKC_OVERHEAD && _nodedb && _config.public_key_len == 32)
        {
            // For our own packets relayed back to us, the other party is the recipient (packet.to),
            // not the sender (packet.from == us). Use recipient's key to reconstruct the shared secret.
            bool is_own_packet = (packet.from == _config.node_id);
            uint32_t peer_id = is_own_packet ? packet.to : packet.from;

            NodeInfo sender_node;
            bool have_sender = getNode(peer_id, sender_node);
            bool have_sender_key = have_sender && sender_node.info.user.public_key.size == 32;

            if (!have_sender)
            {
                ESP_LOGW(TAG, "PKC: peer 0x%08lX not in NodeDB, cannot decrypt", (unsigned long)peer_id);
                pkc_result = meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY;
            }
            else if (!have_sender_key)
            {
                ESP_LOGW(TAG,
                         "PKC: peer 0x%08lX has no public key (size=%u)",
                         (unsigned long)peer_id,
                         (unsigned)sender_node.info.user.public_key.size);
                pkc_result = meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY;
            }
            else
            {
                // We have the peer key — any further failure is a cryptographic mismatch
                pkc_result = meshtastic_Routing_Error_PKI_FAILED;

                ESP_LOGD(TAG,
                         "PKC: attempt decrypt from 0x%08lX (%u bytes)",
                         (unsigned long)packet.from,
                         (unsigned)packet.encrypted.size);

                // Compute shared secret using peer key (recipient when packet is ours, sender otherwise)
                uint8_t shared_key[32] = {};
                if (x25519_shared_secret(_config.private_key, sender_node.info.user.public_key.bytes, shared_key))
                {
                    // SHA256 hash of shared secret
                    mbedtls_sha256(shared_key, 32, shared_key, 0);

                    // Extract extra nonce from end of encrypted payload
                    const size_t raw_size = packet.encrypted.size;
                    const uint8_t* auth = packet.encrypted.bytes + raw_size - PKC_OVERHEAD;
                    uint32_t extra_nonce = 0;
                    memcpy(&extra_nonce, auth + 8, sizeof(uint32_t));

                    // Build nonce (same as encrypt side)
                    uint8_t nonce[13] = {};
                    uint64_t pkt_id_64 = (uint64_t)packet.id;
                    memcpy(nonce, &pkt_id_64, sizeof(uint64_t));
                    memcpy(nonce + 4, &extra_nonce, sizeof(uint32_t));
                    uint32_t from_node = packet.from;
                    memcpy(nonce + 8, &from_node, sizeof(uint32_t));

                    // AES-256-CCM decrypt
                    size_t cipher_len = raw_size - PKC_OVERHEAD;
                    uint8_t plaintext_buf[256] = {};

                    mbedtls_ccm_context ccm;
                    mbedtls_ccm_init(&ccm);
                    if (mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, shared_key, 256) == 0)
                    {
                        int ret = mbedtls_ccm_auth_decrypt(&ccm,
                                                           cipher_len,
                                                           nonce,
                                                           13,
                                                           nullptr,
                                                           0,
                                                           packet.encrypted.bytes,
                                                           plaintext_buf,
                                                           auth,
                                                           8);
                        mbedtls_ccm_free(&ccm);

                        if (ret == 0)
                        {
                            // Decode protobuf
                            meshtastic_Data decoded_data = meshtastic_Data_init_default;
                            pb_istream_t stream = pb_istream_from_buffer(plaintext_buf, cipher_len);
                            if (pb_decode(&stream, meshtastic_Data_fields, &decoded_data) &&
                                decoded_data.portnum != meshtastic_PortNum_UNKNOWN_APP)
                            {
                                ESP_LOGD(TAG,
                                         "PKC decryption succeeded from 0x%08lX (port=%d)",
                                         (unsigned long)packet.from,
                                         decoded_data.portnum);
                                decoded.decoded = decoded_data;
                                decoded.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
                                return meshtastic_Routing_Error_NONE;
                            }
                            else
                            {
                                ESP_LOGW(TAG, "PKC decrypted but protobuf decode failed");
                            }
                        }
                        else
                        {
                            ESP_LOGW(TAG, "PKC AES-CCM auth_decrypt failed (ret=%d) - key mismatch?", ret);
                        }
                    }
                    else
                    {
                        mbedtls_ccm_free(&ccm);
                        ESP_LOGE(TAG, "PKC ccm_setkey failed");
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "PKC x25519_shared_secret failed");
                }
            }
        }
#endif // MESH_HAS_MBEDTLS

        // ---- Channel PSK decryption ----
        uint8_t plaintext[sizeof(decoded.encrypted.bytes)] = {};
        const size_t payload_len = packet.encrypted.size;
        if (payload_len > sizeof(plaintext))
        {
            ESP_LOGW(TAG, "Encrypted payload too large (%u bytes)", (unsigned int)payload_len);
            return meshtastic_Routing_Error_TOO_LARGE;
        }

        uint8_t nonce_counter[16] = {};

        auto decodePayload = [&](const uint8_t* data, size_t len, meshtastic_Data& out) -> bool
        {
            pb_istream_t stream = pb_istream_from_buffer(data, len);
            return pb_decode(&stream, meshtastic_Data_fields, &out);
        };

        auto tryDecryptAndDecode = [&](const uint8_t* key_data,
                                       size_t key_bits,
                                       const uint8_t* nonce,
                                       const char* variant,
                                       meshtastic_Data& out) -> bool
        {
#if MESH_HAS_MBEDTLS
            mbedtls_aes_context ctx;
            mbedtls_aes_init(&ctx);
            if (mbedtls_aes_setkey_enc(&ctx, key_data, (int)key_bits) != 0)
            {
                mbedtls_aes_free(&ctx);
                return false;
            }
            size_t nc_off = 0;
            uint8_t stream_block[16] = {};
            if (mbedtls_aes_crypt_ctr(&ctx,
                                      payload_len,
                                      &nc_off,
                                      (unsigned char*)nonce,
                                      stream_block,
                                      packet.encrypted.bytes,
                                      plaintext) != 0)
            {
                mbedtls_aes_free(&ctx);
                return false;
            }
            mbedtls_aes_free(&ctx);

            if (decodePayload(plaintext, payload_len, out))
            {
                ESP_LOGD(TAG, "Decrypted using %s", variant);
                return true;
            }
            return false;
#else
            (void)key_data;
            (void)key_bits;
            (void)nonce;
            (void)variant;
            (void)out;
            return false;
#endif
        };

        // Try all nonce variants with a given key; returns true on first successful decode
        auto tryAllNonceVariants = [&](const uint8_t* k, size_t k_len, bool is_no_crypto, meshtastic_Data& out) -> bool
        {
            if (is_no_crypto)
            {
                memcpy(plaintext, packet.encrypted.bytes, payload_len);
                if (decodePayload(plaintext, payload_len, out))
                    return true;

                // Fallback: channel has no PSK but remote uses the default key
#if MESH_HAS_MBEDTLS
                if (k_len == 0)
                {
                    ESP_LOGI(TAG, "Fallback: channel has no PSK but remote uses the default key");
                    uint8_t fb[16];
                    memcpy(fb, kDefaultPsk, sizeof(kDefaultPsk));
                    writeU64Le(nonce_counter, (uint64_t)packet.id);
                    writeU32Le(nonce_counter + 8, packet.from);
                    if (tryDecryptAndDecode(fb, 128, nonce_counter, "fallback_default", out))
                        return true;
                }
#endif
                return false;
            }

            size_t key_bits = k_len * 8;
            if (key_bits == 0)
                return false;

#if MESH_HAS_MBEDTLS
            writeU64Le(nonce_counter, (uint64_t)packet.id);
            writeU32Le(nonce_counter + 8, packet.from);
            if (tryDecryptAndDecode(k, key_bits, nonce_counter, "id64_from32", out))
                return true;

            {
                uint8_t n[16] = {};
                writeU32Le(n, packet.from);
                writeU64Le(n + 4, (uint64_t)packet.id);
                if (tryDecryptAndDecode(k, key_bits, n, "from32_id64", out))
                    return true;
            }
            {
                uint8_t n[16] = {};
                writeU32Le(n, packet.id);
                writeU32Le(n + 4, packet.from);
                if (tryDecryptAndDecode(k, key_bits, n, "id32_from32", out))
                    return true;
            }
            {
                uint8_t n[16] = {};
                writeU32Le(n, packet.from);
                writeU32Le(n + 4, packet.id);
                if (tryDecryptAndDecode(k, key_bits, n, "from32_id32", out))
                    return true;
            }
#else
            ESP_LOGW(TAG, "AES support not available, cannot decrypt payload");
#endif
            return false;
        };

        ESP_LOGD(TAG,
                 "PSK decrypt: packet hash=0x%02X from=0x%08lX id=0x%08lX len=%u",
                 packet.channel,
                 (unsigned long)packet.from,
                 (unsigned long)packet.id,
                 (unsigned)payload_len);

        bool decoded_ok = false;
        meshtastic_Data decoded_data = meshtastic_Data_init_default;
        int8_t matched_channel_index = -1;

        // Iterate all enabled channels: match hash, then try decrypt
        if (_nodedb)
        {
            for (uint8_t i = 0; i < 8; i++)
            {
                auto* ch = _nodedb->getChannel(i);
                if (!ch || ch->role == meshtastic_Channel_Role_DISABLED)
                    continue;

                uint8_t ch_key[32] = {};
                size_t ch_key_len = 0;
                bool ch_no_crypto = false;
                if (!expandChannelPsk(ch->settings, ch_key, ch_key_len, ch_no_crypto))
                    continue;

                uint8_t ch_hash = 0;
                computeChannelHashFromSettings(ch->settings, _config, ch_key, ch_key_len, ch_hash);
                if (packet.channel != ch_hash)
                    continue;

                ESP_LOGD(TAG,
                         "Hash 0x%02X matched channel '%s' (index %d, role %d), trying decrypt",
                         packet.channel,
                         ch->settings.name,
                         ch->index,
                         ch->role);

                decoded_data = meshtastic_Data_init_default;
                if (tryAllNonceVariants(ch_key, ch_key_len, ch_no_crypto, decoded_data))
                {
                    matched_channel_index = ch->index;
                    decoded_ok = true;
                    ESP_LOGD(TAG, "Decrypted on channel '%s' (index %d)", ch->settings.name, ch->index);
                    break;
                }

                ESP_LOGI(TAG, "Decrypt failed on channel '%s' (index %d), continuing", ch->settings.name, ch->index);
            }
        }

        // Fallback: try default preset keys
        if (!decoded_ok)
        {
            if (packet.channel == 0 && packet.to != 0xFFFFFFFF)
            {
                // Channel-0 DM must be PKC; propagate the specific PKC error to the caller.
                ESP_LOGW(TAG, "PKC packet (ch=0) for 0x%08lX failed decryption", (unsigned long)packet.to);
                return pkc_result; // PKI_UNKNOWN_PUBKEY or PKI_FAILED
            }
            // fallback to default preset keys
            uint8_t preset_key[32] = {};
            size_t preset_len = 0;
            if (matchDefaultPresetForHash(packet.channel, preset_key, preset_len))
            {
                ESP_LOGD(TAG, "Hash 0x%02X matched default preset key, trying decrypt", packet.channel);
                decoded_data = meshtastic_Data_init_default;
                if (tryAllNonceVariants(preset_key, preset_len, false, decoded_data))
                {
                    // save to primary index
                    matched_channel_index = _config.primary_channel.index;
                    decoded_ok = true;
                    ESP_LOGD(TAG, "Decrypted using default preset key");
                }
            }
        }

        if (!decoded_ok)
        {
            ESP_LOGW(TAG, "Failed to decode packet (hash=0x%02X from=0x%08lX)", packet.channel, (unsigned long)packet.from);
            return meshtastic_Routing_Error_NO_CHANNEL;
        }

        if (decoded_data.portnum == meshtastic_PortNum_UNKNOWN_APP)
        {
            ESP_LOGW(TAG, "Decoded payload has unknown port");
            return meshtastic_Routing_Error_BAD_REQUEST;
        }

        decoded.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        decoded.decoded = decoded_data;
        decoded.channel = matched_channel_index;
        return meshtastic_Routing_Error_NONE;
    }

    void MeshService::handleMeshPacket(const meshtastic_MeshPacket& packet)
    {
        // Check if packet is for us
        bool for_us = (packet.to == _config.node_id || packet.to == 0xFFFFFFFF);

        ESP_LOGI(
            TAG,
            "RX header: from=0x%08lX to=0x%08lX id=0x%08lX ch=0x%02X hop=%u hop_start=%u want_ack=%u via_mqtt=%u relay=0x%02X",
            (unsigned long)packet.from,
            (unsigned long)packet.to,
            (unsigned long)packet.id,
            packet.channel,
            packet.hop_limit,
            packet.hop_start,
            packet.want_ack ? 1 : 0,
            packet.via_mqtt ? 1 : 0,
            packet.relay_node);

        // Skip packets from ignored nodes (but not our own relayed packets)
        if (packet.from != _config.node_id && Mesh::ignorelist_contains(packet.from))
        {
            ESP_LOGW(TAG, "Ignoring packet from 0x%08lX (in ignore list)", (unsigned long)packet.from);
            return;
        }

        if (packet.via_mqtt && _config.lora_config.ignore_mqtt)
        {
            ESP_LOGW(TAG, "Ignoring MQTT packet from 0x%08lX (ignore_mqtt enabled)", (unsigned long)packet.from);
            return;
        }

        meshtastic_MeshPacket decoded_packet = meshtastic_MeshPacket_init_default;
        meshtastic_Routing_Error decode_err = decodeMeshPacket(packet, decoded_packet);
        bool decoded_ok = (decode_err == meshtastic_Routing_Error_NONE);

        if (!decoded_ok && packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag)
        {
            ESP_LOGW(TAG, "Encrypted payload received (%u bytes), unable to decrypt", packet.encrypted.size);

            // For PKC-encrypted DMs directed at us, NACK the sender immediately so they know
            // whether to retry with PSK or re-do key exchange, rather than waiting for timeout.
            if (packet.to == _config.node_id && packet.want_ack &&
                (decode_err == meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY ||
                 decode_err == meshtastic_Routing_Error_PKI_FAILED))
            {
                uint8_t hop_limit = getHopLimitForResponse(packet.hop_start, packet.hop_limit);
                sendRouting(packet.from, packet.id, packet.channel, hop_limit, decode_err);
            }
        }

        // Log RX packet
        {
            PacketLogEntry le = {};
            le.timestamp_ms = (uint32_t)millis();
            le.from = packet.from;
            le.to = packet.to;
            le.id = packet.id;
            le.size = (packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag)
                          ? packet.encrypted.size + (uint16_t)sizeof(PacketHeader)
                          : 0;
            le.rssi = _last_rx_rssi;
            le.snr = _last_rx_snr;
            le.channel = packet.channel;
            le.hop_limit = packet.hop_limit;
            le.hop_start = packet.hop_start;
            le.want_ack = packet.want_ack;
            le.via_mqtt = packet.via_mqtt;
            le.relay_node = packet.relay_node;
            le.is_tx = false;
            le.decoded = decoded_ok;
            le.port = decoded_ok ? decoded_packet.decoded.portnum : 0;
            if (decoded_ok)
            {
                const uint8_t* pb = decoded_packet.decoded.payload.bytes;
                size_t ps = decoded_packet.decoded.payload.size;
                switch (decoded_packet.decoded.portnum)
                {
                case meshtastic_PortNum_TEXT_MESSAGE_APP:
                {
                    size_t n = std::min(ps, sizeof(le.payload_desc) - 1);
                    memcpy(le.payload_desc, pb, n);
                    le.payload_desc[n] = '\0';
                    break;
                }
                case meshtastic_PortNum_POSITION_APP:
                {
                    meshtastic_Position pos = meshtastic_Position_init_zero;
                    pb_istream_t s = pb_istream_from_buffer(pb, ps);
                    if (pb_decode(&s, meshtastic_Position_fields, &pos) && pos.has_latitude_i && pos.has_longitude_i)
                    {
                        int w = snprintf(le.payload_desc,
                                         sizeof(le.payload_desc),
                                         "%.7f, %.7f",
                                         pos.latitude_i * 1e-7,
                                         pos.longitude_i * 1e-7);
                        if (pos.has_altitude && w > 0 && w < (int)sizeof(le.payload_desc) - 1)
                            snprintf(le.payload_desc + w, sizeof(le.payload_desc) - w, ", %dm", (int)pos.altitude);
                    }
                    break;
                }
                case meshtastic_PortNum_NODEINFO_APP:
                {
                    meshtastic_User user = meshtastic_User_init_default;
                    pb_istream_t s = pb_istream_from_buffer(pb, ps);
                    if (pb_decode(&s, meshtastic_User_fields, &user))
                        snprintf(le.payload_desc,
                                 sizeof(le.payload_desc),
                                 "%s (%s) [%s]",
                                 user.long_name,
                                 user.short_name,
                                 NodeDB::getRoleName(user.role));
                    break;
                }
                case meshtastic_PortNum_ROUTING_APP:
                {
                    meshtastic_Routing routing = meshtastic_Routing_init_default;
                    pb_istream_t s = pb_istream_from_buffer(pb, ps);
                    if (pb_decode(&s, meshtastic_Routing_fields, &routing) &&
                        routing.which_variant == meshtastic_Routing_error_reason_tag)
                    {
                        uint8_t err = (uint8_t)routing.error_reason;
                        if (err == meshtastic_Routing_Error_NONE)
                        {
                            strncpy(le.payload_desc, "ACK", sizeof(le.payload_desc));
                        }
                        else
                        {
                            static const struct
                            {
                                uint8_t code;
                                const char* name;
                            } errs[] = {
                                {meshtastic_Routing_Error_NO_ROUTE, "NO_ROUTE"},
                                {meshtastic_Routing_Error_GOT_NAK, "GOT_NAK"},
                                {meshtastic_Routing_Error_TIMEOUT, "TIMEOUT"},
                                {meshtastic_Routing_Error_NO_INTERFACE, "NO_IFACE"},
                                {meshtastic_Routing_Error_MAX_RETRANSMIT, "MAX_RETRY"},
                                {meshtastic_Routing_Error_NO_CHANNEL, "NO_CHAN"},
                                {meshtastic_Routing_Error_TOO_LARGE, "TOO_LARGE"},
                                {meshtastic_Routing_Error_NO_RESPONSE, "NO_RESP"},
                                {meshtastic_Routing_Error_DUTY_CYCLE_LIMIT, "DUTY_LIM"},
                                {meshtastic_Routing_Error_BAD_REQUEST, "BAD_REQ"},
                                {meshtastic_Routing_Error_NOT_AUTHORIZED, "UNAUTH"},
                                {meshtastic_Routing_Error_PKI_FAILED, "PKI_FAIL"},
                                {meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY, "NO_KEY"},
                            };
                            const char* name = nullptr;
                            for (const auto& e : errs)
                                if (e.code == err)
                                {
                                    name = e.name;
                                    break;
                                }
                            if (name)
                                snprintf(le.payload_desc, sizeof(le.payload_desc), "%s (0x%02x)", name, err);
                            else
                                snprintf(le.payload_desc, sizeof(le.payload_desc), "ERR_%u (0x%02x)", err, err);
                        }
                    }
                    break;
                }
                case meshtastic_PortNum_TELEMETRY_APP:
                {
                    meshtastic_Telemetry tel = meshtastic_Telemetry_init_zero;
                    pb_istream_t s = pb_istream_from_buffer(pb, ps);
                    if (pb_decode(&s, meshtastic_Telemetry_fields, &tel) &&
                        tel.which_variant == meshtastic_Telemetry_device_metrics_tag)
                    {
                        const auto& dm = tel.variant.device_metrics;
                        snprintf(le.payload_desc,
                                 sizeof(le.payload_desc),
                                 "%u%%, %.2fV",
                                 (unsigned)dm.battery_level,
                                 dm.voltage);
                    }
                    break;
                }
                case meshtastic_PortNum_TRACEROUTE_APP:
                {
                    int hops = (packet.hop_start > 0) ? (packet.hop_start - packet.hop_limit) : 0;
                    snprintf(le.payload_desc,
                             sizeof(le.payload_desc),
                             "%s, %d hops",
                             decoded_packet.decoded.want_response ? "Request" : "Response",
                             hops);
                    break;
                }
                case meshtastic_PortNum_NEIGHBORINFO_APP:
                {
                    meshtastic_NeighborInfo ninfo = meshtastic_NeighborInfo_init_zero;
                    pb_istream_t s = pb_istream_from_buffer(pb, ps);
                    if (pb_decode(&s, meshtastic_NeighborInfo_fields, &ninfo))
                        snprintf(le.payload_desc, sizeof(le.payload_desc), "%u neighbors", (unsigned)ninfo.neighbors_count);
                    break;
                }
                default:
                    break;
                }
            }
            MeshDataStore::getInstance().addPacketLogEntry(le);
        }

        // Implicit ACK: hearing another node rebroadcast our packet confirms delivery.
        // LoRa is half-duplex so we never receive our own TX; this is always a relay.
        if (packet.from == _config.node_id)
        {
            auto it = _pending_acks.find(packet.id);
            if (it != _pending_acks.end())
            {
                ESP_LOGI(TAG,
                         "Implicit ACK: packet 0x%08lX rebroadcast by relay 0x%02X",
                         (unsigned long)packet.id,
                         packet.relay_node);
                MeshDataStore::getInstance().updateMessageStatus(packet.id,
                                                                 it->second.dest_node_id,
                                                                 TextMessage::Status::ACK,
                                                                 0,
                                                                 it->second.channel);
                _pending_acks.erase(it);
            }
            return;
        }

        if (decoded_ok && decoded_packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag)
        {
            ESP_LOGI(TAG,
                     "RX decoded: port=%u payload=%u bytes %s",
                     decoded_packet.decoded.portnum,
                     decoded_packet.decoded.payload.size,
                     for_us ? "(for us)" : "(not for us)");

            // Calculate hops away: hop_start - hop_limit
            uint8_t hops_away = 0;
            if (decoded_packet.hop_start > 0 && decoded_packet.hop_start >= decoded_packet.hop_limit)
            {
                hops_away = decoded_packet.hop_start - decoded_packet.hop_limit;
            }
            else if (decoded_packet.hop_start == 0 && _config.lora_config.hop_limit > decoded_packet.hop_limit)
            {
                hops_away = _config.lora_config.hop_limit - decoded_packet.hop_limit;
            }

            // Update node's RSSI/SNR and last heard time on every successfully decoded packet
            if (_nodedb && decoded_packet.from != _config.node_id)
            {
                Mesh::NodeInfo node_info;
                if (getNode(decoded_packet.from, node_info))
                {
                    // Update existing node with new signal values
                    node_info.last_rssi = _last_rx_rssi;
                    node_info.info.snr = _last_rx_snr;
                    node_info.info.last_heard = (uint32_t)time(nullptr);
                    node_info.info.hops_away = hops_away;
                    node_info.info.has_hops_away = true;
                    node_info.relay_node = decoded_packet.relay_node;
                    _nodedb->updateNode(node_info.info, _last_rx_rssi, _last_rx_snr, decoded_packet.relay_node);
                    // format last heard to date time string
                    time_t last_heard_time = (time_t)node_info.info.last_heard;
                    char last_heard_str[20];
                    strftime(last_heard_str, sizeof(last_heard_str), "%Y-%m-%d %H:%M:%S", localtime(&last_heard_time));
                    ESP_LOGW(TAG,
                             "Updated node 0x%08lX: RSSI=%d, SNR=%.1f, hops=%d (start=%d, limit=%d), last_heard(%lu)=%s, "
                             "relay=0x%02X",
                             (unsigned long)decoded_packet.from,
                             _last_rx_rssi,
                             _last_rx_snr,
                             hops_away,
                             decoded_packet.hop_start,
                             decoded_packet.hop_limit,
                             node_info.info.last_heard,
                             last_heard_str,
                             decoded_packet.relay_node);
                }
                else
                {
                    // play sound knock knock
                    _hal->playNotificationSound(HAL::Hal::NotificationSound::KNOCK);
                    // Create new node entry with signal values
                    ESP_LOGW(TAG,
                             "New node 0x%08lX: RSSI=%d, SNR=%.1f, hops=%d",
                             (unsigned long)decoded_packet.from,
                             _last_rx_rssi,
                             _last_rx_snr,
                             hops_away);
                    meshtastic_NodeInfo new_node = meshtastic_NodeInfo_init_default;
                    new_node.num = decoded_packet.from;
                    new_node.hops_away = hops_away;
                    new_node.has_hops_away = true;
                    snprintf(new_node.user.id, sizeof(new_node.user.id), "!%08lx", (unsigned long)decoded_packet.from);
                    _nodedb->updateNode(new_node, _last_rx_rssi, _last_rx_snr, decoded_packet.relay_node);
                }
            }

            switch (decoded_packet.decoded.portnum)
            {
            case meshtastic_PortNum_TEXT_MESSAGE_APP:
                if (for_us)
                {
                    ESP_LOGI(TAG,
                             "Text message from 0x%08lX: %.*s",
                             (unsigned long)decoded_packet.from,
                             decoded_packet.decoded.payload.size,
                             decoded_packet.decoded.payload.bytes);

                    // Store the message in MeshDataStore
                    {
                        TextMessage msg;
                        msg.id = decoded_packet.id;
                        msg.from = decoded_packet.from;
                        msg.to = decoded_packet.to;
                        msg.timestamp = decoded_packet.rx_time > 0 ? decoded_packet.rx_time : (uint32_t)time(nullptr);
                        msg.channel = decoded_packet.channel;
                        msg.is_direct = (decoded_packet.to != 0xFFFFFFFF);
                        msg.read = false; // Mark as unread
                        msg.text =
                            std::string((char*)decoded_packet.decoded.payload.bytes, decoded_packet.decoded.payload.size);
                        msg.status = TextMessage::Status::DELIVERED;
                        msg.hops_away = hops_away;
                        msg.rx_snr = (int8_t)(_last_rx_snr * 4.0f);

                        MeshDataStore::getInstance().addMessage(msg);
                        ESP_LOGD(TAG, "Message stored: is_direct=%d, to=0x%08lX", msg.is_direct, (unsigned long)msg.to);
                    }

                    // Send ACK if requested and this is a direct message (not broadcast)
                    if (decoded_packet.want_ack && decoded_packet.to != 0xFFFFFFFF)
                    {
                        uint8_t ack_hop_limit = getHopLimitForResponse(decoded_packet.hop_start, decoded_packet.hop_limit);
                        sendAck(decoded_packet.from, decoded_packet.id, decoded_packet.channel, ack_hop_limit);
                    }

                    // Process #invite DM: create a channel from invitation
                    if (decoded_packet.to != 0xFFFFFFFF && _nodedb && _hal && _hal->settings() &&
                        _hal->settings()->getBool("security", "invitations"))
                    {
                        std::string text((char*)decoded_packet.decoded.payload.bytes, decoded_packet.decoded.payload.size);
                        if (text.size() > 8 && text.rfind("#invite ", 0) == 0)
                        {
                            auto eq_pos = text.find('=', 8);
                            if (eq_pos != std::string::npos && eq_pos > 8 && (eq_pos - 8) <= 11)
                            {
                                std::string ch_name = text.substr(8, eq_pos - 8);
                                std::string ch_key_b64 = text.substr(eq_pos + 1);
                                std::erase(ch_name, ' ');
                                std::erase(ch_key_b64, ' ');
                                ESP_LOGI(TAG, "Invite channel name: '%s', key: '%s'", ch_name.c_str(), ch_key_b64.c_str());
                                if (!ch_name.empty() && ch_name.size() <= 11 && !ch_key_b64.empty() && ch_key_b64.size() <= 50)
                                {
                                    size_t key_len = 0;
                                    uint8_t key_buf[32] = {};
                                    int rc = mbedtls_base64_decode(key_buf,
                                                                   sizeof(key_buf),
                                                                   &key_len,
                                                                   (const unsigned char*)ch_key_b64.c_str(),
                                                                   ch_key_b64.size());
                                    if (rc == 0 && key_len > 0 && key_len <= 32)
                                    {
                                        bool exists = false;
                                        for (uint8_t i = 0; i < 8; i++)
                                        {
                                            auto* ch = _nodedb->getChannel(i);
                                            if (!ch || ch->role == meshtastic_Channel_Role_DISABLED)
                                                continue;
                                            if (strcmp(resolveChannelName(ch->settings, _config), ch_name.c_str()) == 0 &&
                                                ch->settings.psk.size == key_len &&
                                                memcmp(ch->settings.psk.bytes, key_buf, key_len) == 0)
                                            {
                                                exists = true;
                                                break;
                                            }
                                        }

                                        if (!exists)
                                        {
                                            int free_slot = -1;
                                            for (uint8_t i = 0; i < 8; i++)
                                            {
                                                auto* ch = _nodedb->getChannel(i);
                                                if (!ch || ch->role == meshtastic_Channel_Role_DISABLED)
                                                {
                                                    free_slot = i;
                                                    break;
                                                }
                                            }

                                            if (free_slot >= 0)
                                            {
                                                meshtastic_Channel new_ch = meshtastic_Channel_init_default;
                                                new_ch.index = (int8_t)free_slot;
                                                new_ch.has_settings = true;
                                                new_ch.role = meshtastic_Channel_Role_SECONDARY;
                                                strncpy(new_ch.settings.name,
                                                        ch_name.c_str(),
                                                        sizeof(new_ch.settings.name) - 1);
                                                new_ch.settings.psk.size = key_len;
                                                memcpy(new_ch.settings.psk.bytes, key_buf, key_len);

                                                _nodedb->setChannel((uint8_t)free_slot, new_ch);
                                                _nodedb->saveChannels();
                                                ESP_LOGI(TAG,
                                                         "Channel '%s' created from invite (slot %d)",
                                                         ch_name.c_str(),
                                                         free_slot);
                                            }
                                            else
                                            {
                                                ESP_LOGW(TAG, "No free channel slot for invite '%s'", ch_name.c_str());
                                            }
                                        }
                                        else
                                        {
                                            ESP_LOGI(TAG, "Invite channel '%s' already exists, skipping", ch_name.c_str());
                                        }
                                    }
                                    else
                                    {
                                        ESP_LOGW(TAG, "Invalid base64 key in invite");
                                    }
                                }
                            }
                        }
                    }

                    // Process #ping on channel: auto-reply if configured
                    if (decoded_packet.to == 0xFFFFFFFF && _nodedb && decoded_packet.channel < 8)
                    {
                        const char* payload = (const char*)decoded_packet.decoded.payload.bytes;
                        size_t plen = decoded_packet.decoded.payload.size;
                        bool has_ping = false;
                        for (size_t i = 0; i + 5 <= plen; i++)
                        {
                            if (payload[i] == '#' && payload[i + 1] == 'p' && payload[i + 2] == 'i' && payload[i + 3] == 'n' &&
                                payload[i + 4] == 'g')
                            {
                                has_ping = true;
                                break;
                            }
                        }
                        if (has_ping)
                        {
                            const auto& greeting = _nodedb->getGreeting(decoded_packet.channel);
                            if (greeting.ping_text[0])
                            {
                                uint8_t hops_away = 0;
                                if (decoded_packet.hop_start > 0 && decoded_packet.hop_start >= decoded_packet.hop_limit)
                                    hops_away = decoded_packet.hop_start - decoded_packet.hop_limit;

                                NodeInfo node_info;
                                const NodeInfo* np = getNode(decoded_packet.from, node_info) ? &node_info : nullptr;

                                char expanded[256];
                                expandGreetingTemplate(greeting.ping_text,
                                                       expanded,
                                                       sizeof(expanded),
                                                       np,
                                                       decoded_packet.from,
                                                       hops_away,
                                                       _last_rx_rssi,
                                                       _last_rx_snr);

                                uint32_t pid = sendText(expanded, 0xFFFFFFFF, decoded_packet.channel);
                                if (pid)
                                {
                                    TextMessage pmsg;
                                    pmsg.id = pid;
                                    pmsg.from = _config.node_id;
                                    pmsg.to = 0xFFFFFFFF;
                                    pmsg.timestamp = (uint32_t)time(nullptr);
                                    pmsg.channel = decoded_packet.channel;
                                    pmsg.is_direct = false;
                                    pmsg.read = true;
                                    pmsg.text = expanded;
                                    pmsg.status = TextMessage::Status::PENDING;
                                    MeshDataStore::getInstance().addMessage(pmsg);
                                }
                                ESP_LOGI(TAG,
                                         "Ping reply on ch%d to 0x%08lX: %s",
                                         decoded_packet.channel,
                                         (unsigned long)decoded_packet.from,
                                         expanded);
                            }
                        }
                    }

                    if (_message_callback)
                    {
                        _message_callback(decoded_packet);
                    }
                }
                break;

            case meshtastic_PortNum_NODEINFO_APP:
                handleNodeInfoPacket(decoded_packet);
                break;

            case meshtastic_PortNum_POSITION_APP:
                handlePositionPacket(decoded_packet);
                break;

            case meshtastic_PortNum_TRACEROUTE_APP:
                handleTraceRoutePacket(decoded_packet, _last_rx_snr);
                break;

            case meshtastic_PortNum_ADMIN_APP:
                if (for_us)
                {
                    handleAdminMessage(decoded_packet);
                }
                break;

            case meshtastic_PortNum_TELEMETRY_APP:
                handleTelemetryPacket(decoded_packet);
                break;

            case meshtastic_PortNum_NEIGHBORINFO_APP:
                handleNeighborInfoPacket(decoded_packet);
                break;

            case meshtastic_PortNum_ROUTING_APP:
                if (for_us && decoded_packet.decoded.request_id != 0)
                {
                    // This is an ACK/NACK for a packet we sent
                    uint32_t orig_id = decoded_packet.decoded.request_id;
                    auto pa_it = _pending_acks.find(orig_id);
                    uint32_t dest_id = (pa_it != _pending_acks.end()) ? pa_it->second.dest_node_id : decoded_packet.from;
                    uint8_t pa_channel = (pa_it != _pending_acks.end()) ? pa_it->second.channel : decoded_packet.channel;

                    meshtastic_Routing routing = meshtastic_Routing_init_default;
                    pb_istream_t routing_stream =
                        pb_istream_from_buffer(decoded_packet.decoded.payload.bytes, decoded_packet.decoded.payload.size);

                    if (pb_decode(&routing_stream, meshtastic_Routing_fields, &routing))
                    {
                        if (routing.which_variant == meshtastic_Routing_error_reason_tag &&
                            routing.error_reason == meshtastic_Routing_Error_NONE)
                        {
                            ESP_LOGI(TAG,
                                     "ACK received for packet 0x%08lX from 0x%08lX",
                                     (unsigned long)orig_id,
                                     (unsigned long)decoded_packet.from);
                            MeshDataStore::getInstance().updateMessageStatus(orig_id,
                                                                             dest_id,
                                                                             TextMessage::Status::DELIVERED,
                                                                             0,
                                                                             pa_channel);
                        }
                        else
                        {
                            ESP_LOGW(TAG,
                                     "NACK received for packet 0x%08lX, error=%d",
                                     (unsigned long)orig_id,
                                     (int)routing.error_reason);
                            MeshDataStore::getInstance().updateMessageStatus(orig_id,
                                                                             dest_id,
                                                                             TextMessage::Status::NACK,
                                                                             (uint8_t)routing.error_reason,
                                                                             pa_channel);
                        }
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Failed to decode Routing message from 0x%08lX", (unsigned long)decoded_packet.from);
                    }
                    // Remove from pending ACK tracking regardless of result
                    if (pa_it != _pending_acks.end())
                        _pending_acks.erase(pa_it);
                }
                break;

            default:
                ESP_LOGD(TAG, "Port %d message received", decoded_packet.decoded.portnum);
                break;
            }
        }

        // --- Rebroadcast decision ---
        // Never rebroadcast our own packets, packets addressed to us, or packets with no hops remaining
        if (packet.from != _config.node_id && packet.to != _config.node_id && packet.hop_limit > 0)
        {
            bool should_rebroadcast = false;
            const auto role = _config.role;
            const auto mode = _config.rebroadcast_mode;

            // CLIENT_MUTE role never rebroadcasts
            if (role == meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE)
            {
                should_rebroadcast = false;
            }
            // CLIENT_HIDDEN role rebroadcasts packets we can decrypt (on our channel)
            else if (role == meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN)
            {
                should_rebroadcast = decoded_ok;
            }
            // NONE mode: only permitted for SENSOR, TRACKER, TAK_TRACKER roles
            else if (mode == meshtastic_Config_DeviceConfig_RebroadcastMode_NONE)
            {
                should_rebroadcast = false;
            }
            // ALL / ALL_SKIP_DECODING: rebroadcast everything (including foreign/undecryptable)
            else if (mode == meshtastic_Config_DeviceConfig_RebroadcastMode_ALL ||
                     mode == meshtastic_Config_DeviceConfig_RebroadcastMode_ALL_SKIP_DECODING)
            {
                should_rebroadcast = true;
            }
            // LOCAL_ONLY: only rebroadcast packets we can decrypt (on our channel)
            else if (mode == meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY)
            {
                should_rebroadcast = decoded_ok;
            }
            // KNOWN_ONLY: like LOCAL_ONLY + sender must be in our NodeDB
            else if (mode == meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY)
            {
                if (decoded_ok && _nodedb)
                {
                    Mesh::NodeInfo node_info;
                    should_rebroadcast = getNode(packet.from, node_info);
                }
            }
            // CORE_PORTNUMS_ONLY: only standard portnums (must be decodable)
            else if (mode == meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY)
            {
                if (decoded_ok)
                {
                    auto port = decoded_packet.decoded.portnum;
                    should_rebroadcast = (port == meshtastic_PortNum_TEXT_MESSAGE_APP ||
                                          port == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP ||
                                          port == meshtastic_PortNum_POSITION_APP || port == meshtastic_PortNum_NODEINFO_APP ||
                                          port == meshtastic_PortNum_ROUTING_APP || port == meshtastic_PortNum_TELEMETRY_APP);
                }
            }

            if (should_rebroadcast)
            {
                meshtastic_MeshPacket rebroadcast = packet;
                rebroadcast.hop_limit--;
                rebroadcast.relay_node = (uint8_t)(_config.node_id & 0xFF);

                // ROUTER role: higher priority rebroadcast
                PacketPriority prio =
                    (role == meshtastic_Config_DeviceConfig_Role_ROUTER) ? PacketPriority::DEFAULT : PacketPriority::BACKGROUND;

                uint8_t rb_port = (decoded_ok && decoded_packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag)
                                      ? (uint8_t)decoded_packet.decoded.portnum
                                      : 0;
                if (_router.enqueueTx(rebroadcast, prio, false, rb_port))
                {
                    ESP_LOGI(TAG,
                             "Rebroadcast packet 0x%08lX from 0x%08lX (hop_limit=%u, mode=%d)",
                             (unsigned long)packet.id,
                             (unsigned long)packet.from,
                             rebroadcast.hop_limit,
                             (int)mode);
                }
                else
                {
                    ESP_LOGW(TAG, "Failed to enqueue rebroadcast for packet 0x%08lX", (unsigned long)packet.id);
                }
            }
        }
    }

    void MeshService::handleAdminMessage(const meshtastic_MeshPacket& packet)
    {
        ESP_LOGI(TAG, "Admin message received");
        // TODO: Handle admin commands (config changes, reboot, etc.)
    }

    void MeshService::handleNodeInfoPacket(const meshtastic_MeshPacket& packet)
    {
        // Ignore packets from our own node
        if (packet.from == _config.node_id)
        {
            ESP_LOGW(TAG, "Ignoring NodeInfo from our own node: 0x%08lx", (unsigned long)packet.from);
            return;
        }

        // Decode User from payload (NODEINFO_APP payload is meshtastic_User)
        meshtastic_User user = meshtastic_User_init_default;
        pb_istream_t stream = pb_istream_from_buffer(packet.decoded.payload.bytes, packet.decoded.payload.size);
        if (!pb_decode(&stream, meshtastic_User_fields, &user))
        {
            ESP_LOGE(TAG, "Failed to decode User from NodeInfo packet: %s", PB_GET_ERROR(&stream));
            return;
        }

        // Coerce user.id to be derived from the node number (security measure)
        snprintf(user.id, sizeof(user.id), "!%08lx", (unsigned long)packet.from);

        ESP_LOGI(TAG,
                 "NodeInfo received from 0x%08lx: %s / %s (hw_model: %d)",
                 (unsigned long)packet.from,
                 user.short_name,
                 user.long_name,
                 user.hw_model);

        // Update node database; send greeting on first NodeInfo
        if (_nodedb)
        {
            NodeInfo prev;
            bool had_user = getNode(packet.from, prev) && prev.info.has_user;

            _nodedb->updateUser(packet.from, user);

            if (!had_user)
            {
                NodeInfo updated;
                uint8_t hops = 0;
                int16_t rssi = 0;
                float snr = 0;
                if (getNode(packet.from, updated))
                {
                    hops = updated.info.has_hops_away ? updated.info.hops_away : 0;
                    rssi = updated.last_rssi;
                    snr = updated.info.snr;
                }
                sendNewNodeGreeting(packet.from, packet.channel, hops, rssi, snr);
            }
        }

        // Handle want_response - send our NodeInfo back if requested
        if (packet.decoded.want_response && packet.to == _config.node_id)
        {
            // Certain roles should not respond to NodeInfo requests
            // TRACKER and SENSOR are low-power devices that shouldn't respond
            // CLIENT_HIDDEN should remain hidden
            if (_config.role == meshtastic_Config_DeviceConfig_Role_TRACKER ||
                _config.role == meshtastic_Config_DeviceConfig_Role_SENSOR ||
                _config.role == meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN)
            {
                ESP_LOGD(TAG, "Skipping NodeInfo response due to device role (%d)", static_cast<int>(_config.role));
                return;
            }

            ESP_LOGI(TAG, "Responding to NodeInfo request from 0x%08lx", (unsigned long)packet.from);
            sendNodeInfo(packet.from, packet.channel, false);
        }
    }

    void MeshService::handlePositionPacket(const meshtastic_MeshPacket& packet)
    {
        // Ignore packets from our own node
        if (packet.from == _config.node_id)
        {
            return;
        }

        // Decode Position from payload
        meshtastic_Position position = meshtastic_Position_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(packet.decoded.payload.bytes, packet.decoded.payload.size);
        if (!pb_decode(&stream, meshtastic_Position_fields, &position))
        {
            ESP_LOGE(TAG, "Failed to decode Position: %s", PB_GET_ERROR(&stream));
            return;
        }

        // Log position data
        ESP_LOGI(TAG,
                 "Position from 0x%08lX: lat=%ld lon=%ld alt=%ld sats=%lu time=%lu",
                 (unsigned long)packet.from,
                 (long)(position.has_latitude_i ? position.latitude_i : 0),
                 (long)(position.has_longitude_i ? position.longitude_i : 0),
                 (long)(position.has_altitude ? position.altitude : 0),
                 (unsigned long)position.sats_in_view,
                 (unsigned long)position.time);

        // Update node database with position
        if (_nodedb)
        {
            _nodedb->updatePosition(packet.from, position);
        }

        // Handle want_response - certain roles should not respond to position requests
        // TRACKER and TAK_TRACKER are GPS-focused devices but respond on their own schedule
        // SENSOR is a low-power device that shouldn't respond
        // CLIENT_HIDDEN should remain hidden
        if (packet.decoded.want_response && packet.to == _config.node_id)
        {
            if (_config.position == MeshConfig::POSITION_OFF)
            {
                ESP_LOGD(TAG, "Skipping Position response due to position mode (off)");
                return;
            }
            if (_config.role == meshtastic_Config_DeviceConfig_Role_TRACKER ||
                _config.role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER ||
                _config.role == meshtastic_Config_DeviceConfig_Role_SENSOR ||
                _config.role == meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN)
            {
                ESP_LOGD(TAG, "Skipping Position response due to device role (%d)", static_cast<int>(_config.role));
                return;
            }
            ESP_LOGI(TAG, "Responding to Position request from 0x%08lx channel %d", (unsigned long)packet.from, packet.channel);
            sendPosition(packet.from, packet.channel);
        }
    }

    void MeshService::handleTelemetryPacket(const meshtastic_MeshPacket& packet)
    {
        // Ignore packets from our own node
        if (packet.from == _config.node_id)
        {
            return;
        }

        // Decode Telemetry from payload
        meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(packet.decoded.payload.bytes, packet.decoded.payload.size);
        if (!pb_decode(&stream, meshtastic_Telemetry_fields, &telemetry))
        {
            ESP_LOGE(TAG, "Failed to decode Telemetry: %s", PB_GET_ERROR(&stream));
            return;
        }

        // Handle device metrics (battery level, voltage, etc.)
        if (telemetry.which_variant == meshtastic_Telemetry_device_metrics_tag)
        {
            const auto& dm = telemetry.variant.device_metrics;
            ESP_LOGI(TAG,
                     "Telemetry from 0x%08lX: battery=%lu%%, voltage=%.2fV, uptime=%lus",
                     (unsigned long)packet.from,
                     dm.has_battery_level ? (unsigned long)dm.battery_level : 0,
                     dm.has_voltage ? dm.voltage : 0.0f,
                     dm.has_uptime_seconds ? (unsigned long)dm.uptime_seconds : 0);

            // Update node database with device metrics
            if (_nodedb)
            {
                Mesh::NodeInfo node_info;
                if (getNode(packet.from, node_info))
                {
                    // Update device metrics
                    node_info.info.has_device_metrics = true;
                    node_info.info.device_metrics = dm;
                    _nodedb->updateNode(node_info.info, -1, 0); // Keep existing RSSI/SNR
                }
                else
                {
                    // Create new node with device metrics
                    meshtastic_NodeInfo new_node = meshtastic_NodeInfo_init_default;
                    new_node.num = packet.from;
                    new_node.has_device_metrics = true;
                    new_node.device_metrics = dm;
                    snprintf(new_node.user.id, sizeof(new_node.user.id), "!%08lx", (unsigned long)packet.from);
                    _nodedb->updateNode(new_node, -1, 0);
                }
            }
        }
        else if (telemetry.which_variant == meshtastic_Telemetry_environment_metrics_tag)
        {
            const auto& em = telemetry.variant.environment_metrics;
            ESP_LOGI(TAG,
                     "Environment from 0x%08lX: temp=%.1f°C, humidity=%.1f%%, pressure=%.1fhPa",
                     (unsigned long)packet.from,
                     em.has_temperature ? em.temperature : 0.0f,
                     em.has_relative_humidity ? em.relative_humidity : 0.0f,
                     em.has_barometric_pressure ? em.barometric_pressure : 0.0f);
            // Environment metrics can be stored if needed
        }
        else
        {
            ESP_LOGD(TAG, "Telemetry type %d from 0x%08lX", telemetry.which_variant, (unsigned long)packet.from);
        }
    }

    void MeshService::handleNeighborInfoPacket(const meshtastic_MeshPacket& packet)
    {
        if (!_config.neighborinfo_enabled)
        {
            ESP_LOGD(TAG, "NeighborInfo module disabled, ignoring packet");
            return;
        }

        meshtastic_NeighborInfo ni = meshtastic_NeighborInfo_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(packet.decoded.payload.bytes, packet.decoded.payload.size);
        if (!pb_decode(&stream, meshtastic_NeighborInfo_fields, &ni))
        {
            ESP_LOGE(TAG, "Failed to decode NeighborInfo: %s", PB_GET_ERROR(&stream));
            return;
        }

        uint32_t source = ni.node_id ? ni.node_id : packet.from;
        ESP_LOGI(TAG, "NeighborInfo from 0x%08lX: %u neighbors", (unsigned long)source, (unsigned)ni.neighbors_count);

        std::vector<NeighborEntry> entries;
        entries.reserve(ni.neighbors_count);
        for (pb_size_t i = 0; i < ni.neighbors_count; i++)
        {
            entries.push_back({ni.neighbors[i].node_id, ni.neighbors[i].snr});
        }
        neighbors_save(source, entries);
        if (_nodedb)
            _nodedb->markDirty();

        if (packet.decoded.want_response && packet.to == _config.node_id)
        {
            ESP_LOGI(TAG, "Responding to NeighborInfo request from 0x%08lX", (unsigned long)packet.from);
            sendNeighborInfo(packet.from, packet.channel, false);
        }
    }

    void MeshService::handleTraceRoutePacket(const meshtastic_MeshPacket& packet, float snr)
    {
        // Decode RouteDiscovery from payload
        meshtastic_RouteDiscovery route = meshtastic_RouteDiscovery_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(packet.decoded.payload.bytes, packet.decoded.payload.size);
        if (!pb_decode(&stream, meshtastic_RouteDiscovery_fields, &route))
        {
            ESP_LOGE(TAG, "Failed to decode RouteDiscovery: %s", PB_GET_ERROR(&stream));
            return;
        }

        // Determine if this is a request (going towards destination) or response (coming back)
        bool is_request = (packet.decoded.request_id == 0);
        bool for_us = (packet.to == _config.node_id);

        ESP_LOGI(TAG,
                 "TraceRoute %s from 0x%08lx to 0x%08lx (for_us=%d, route_count=%d, route_back_count=%d)",
                 is_request ? "request" : "response",
                 (unsigned long)packet.from,
                 (unsigned long)packet.to,
                 for_us,
                 route.route_count,
                 route.route_back_count);

        // Append our node ID and SNR to the appropriate route
        // SNR is stored as int8_t * 4 (quarter dB precision)
        // NOTE: If the packet is addressed TO US, we only append SNR (not our node ID)
        // because route arrays contain only intermediate hops, not endpoints
        int8_t snr_encoded = (int8_t)(snr * 4);

        if (is_request)
        {
            // Request going towards destination - append to route and snr_towards
            // Only append our node ID if we are NOT the final destination (intermediate hop)
            if (!for_us && route.route_count < 8)
            {
                route.route[route.route_count] = _config.node_id;
                route.route_count++;
            }
            if (route.snr_towards_count < 8)
            {
                route.snr_towards[route.snr_towards_count] = snr_encoded;
                route.snr_towards_count++;
            }
        }
        else
        {
            // Response going back - append to route_back and snr_back
            // Only append our node ID if we are NOT the final destination (intermediate hop)
            if (!for_us && route.route_back_count < 8)
            {
                route.route_back[route.route_back_count] = _config.node_id;
                route.route_back_count++;
            }
            if (route.snr_back_count < 8)
            {
                route.snr_back[route.snr_back_count] = snr_encoded;
                route.snr_back_count++;
            }
        }

        // If this packet is for us and it's a request, send a response back
        if (for_us && is_request)
        {
            ESP_LOGI(TAG, "TraceRoute request reached us, sending response back to 0x%08lx", (unsigned long)packet.from);

            // Encode the updated RouteDiscovery
            uint8_t route_buf[meshtastic_RouteDiscovery_size];
            pb_ostream_t route_stream = pb_ostream_from_buffer(route_buf, sizeof(route_buf));
            if (!pb_encode(&route_stream, meshtastic_RouteDiscovery_fields, &route))
            {
                ESP_LOGE(TAG, "Failed to encode RouteDiscovery response: %s", PB_GET_ERROR(&route_stream));
                return;
            }

            // Create Data payload
            meshtastic_Data data = meshtastic_Data_init_default;
            data.portnum = meshtastic_PortNum_TRACEROUTE_APP;
            data.payload.size = route_stream.bytes_written;
            memcpy(data.payload.bytes, route_buf, data.payload.size);
            data.request_id = packet.id; // Mark as response to original request
            data.want_response = false;
            applyOkToMqtt(data);

            uint8_t data_buf[MAX_LORA_PAYLOAD] = {};
            pb_ostream_t data_stream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
            if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
            {
                ESP_LOGE(TAG, "Failed to encode TraceRoute data payload: %s", PB_GET_ERROR(&data_stream));
                return;
            }

            uint32_t pid = encryptAndSend(data_buf,
                                          data_stream.bytes_written,
                                          packet.from,
                                          false,
                                          _config.lora_config.hop_limit,
                                          PacketPriority::DEFAULT,
                                          meshtastic_PortNum_TRACEROUTE_APP);
            if (pid)
                ESP_LOGI(TAG, "TraceRoute response sent to 0x%08lX", (unsigned long)packet.from);
            else
                ESP_LOGW(TAG, "Failed to enqueue TraceRoute response");
        }
        else if (for_us && !is_request)
        {
            ESP_LOGI(TAG, "TraceRoute response received from 0x%08lx", (unsigned long)packet.from);

            ESP_LOGI(TAG, "Route to destination (%d hops):", route.route_count);
            for (int i = 0; i < route.route_count; i++)
            {
                float snr_db = (route.snr_towards[i] != INT8_MIN) ? (float)route.snr_towards[i] / 4.0f : 0.0f;
                ESP_LOGI(TAG, "  Hop %d: 0x%08lx (SNR: %.1f dB)", i + 1, (unsigned long)route.route[i], snr_db);
            }

            ESP_LOGI(TAG, "Route back (%d hops):", route.route_back_count);
            for (int i = 0; i < route.route_back_count; i++)
            {
                float snr_db = (route.snr_back[i] != INT8_MIN) ? (float)route.snr_back[i] / 4.0f : 0.0f;
                ESP_LOGI(TAG, "  Hop %d: 0x%08lx (SNR: %.1f dB)", i + 1, (unsigned long)route.route_back[i], snr_db);
            }
            // play trace sound
            _hal->playNotificationSound(HAL::Hal::NotificationSound::TRACE);

            // Find the matching record for this target (PENDING or FAILED/timeout) and update it
            uint32_t target = packet.from;
            auto& store = MeshDataStore::getInstance();
            uint32_t count = store.getTraceRouteCount(target);
            uint32_t updated_index = UINT32_MAX;

            for (int i = (int)count - 1; i >= 0; i--)
            {
                TraceRouteResult res;
                if (!store.getTraceRouteByIndex(target, (uint32_t)i, res))
                    continue;
                // Update PENDING (in time) or FAILED (late response after timeout)
                if (res.status == TraceRouteResult::Status::SUCCESS)
                    break; // Already completed, skip

                res.status = TraceRouteResult::Status::SUCCESS;
                uint32_t now_ts = (uint32_t)time(nullptr);
                uint32_t elapsed = (now_ts > res.timestamp) ? (now_ts - res.timestamp) : 0;
                res.duration_sec = (elapsed > UINT16_MAX) ? UINT16_MAX : (uint16_t)elapsed;
                res.route_to.clear();
                res.route_back.clear();

                for (int j = 0; j < route.route_count; j++)
                {
                    float snr_db = (j < route.snr_towards_count && route.snr_towards[j] != INT8_MIN)
                                       ? (float)route.snr_towards[j] / 4.0f
                                       : 0.0f;
                    res.route_to.push_back({route.route[j], snr_db});
                }
                int dest_idx = route.route_count;
                res.dest_snr = (dest_idx < route.snr_towards_count && route.snr_towards[dest_idx] != INT8_MIN)
                                   ? (float)route.snr_towards[dest_idx] / 4.0f
                                   : 0.0f;

                for (int j = 0; j < route.route_back_count; j++)
                {
                    float snr_db =
                        (j < route.snr_back_count && route.snr_back[j] != INT8_MIN) ? (float)route.snr_back[j] / 4.0f : 0.0f;
                    res.route_back.push_back({route.route_back[j], snr_db});
                }
                int orig_idx = route.route_back_count;
                res.origin_snr = (orig_idx < route.snr_back_count && route.snr_back[orig_idx] != INT8_MIN)
                                     ? (float)route.snr_back[orig_idx] / 4.0f
                                     : 0.0f;

                store.updateTraceRoute(target, (uint32_t)i, res);
                updated_index = (uint32_t)i;
                break;
            }

            if (_traceroute_callback && updated_index != UINT32_MAX)
            {
                _traceroute_callback(target, updated_index);
            }
        }
        // If not for us, the packet will be rebroadcasted by the normal routing logic
        // but we need to update the payload with our appended route info
        // Note: This requires modifying the packet before rebroadcast, which is handled elsewhere
    }

    bool MeshService::sendTraceRoute(uint32_t dest, uint8_t channel)
    {
        if (dest == _config.node_id)
        {
            ESP_LOGW(TAG, "Cannot trace route to self");
            return false;
        }

        if (dest == 0xFFFFFFFF)
        {
            ESP_LOGW(TAG, "Cannot trace route to broadcast address");
            return false;
        }

        ESP_LOGI(TAG, "Starting traceroute to 0x%08lx", (unsigned long)dest);

        // Create empty RouteDiscovery
        meshtastic_RouteDiscovery route = meshtastic_RouteDiscovery_init_zero;

        // Encode RouteDiscovery
        uint8_t route_buf[meshtastic_RouteDiscovery_size];
        pb_ostream_t route_stream = pb_ostream_from_buffer(route_buf, sizeof(route_buf));
        if (!pb_encode(&route_stream, meshtastic_RouteDiscovery_fields, &route))
        {
            ESP_LOGE(TAG, "Failed to encode RouteDiscovery: %s", PB_GET_ERROR(&route_stream));
            return false;
        }

        // Create Data payload
        meshtastic_Data data = meshtastic_Data_init_default;
        data.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        data.payload.size = route_stream.bytes_written;
        memcpy(data.payload.bytes, route_buf, data.payload.size);
        data.want_response = true;
        applyOkToMqtt(data);

        uint8_t data_buf[MAX_LORA_PAYLOAD] = {};
        pb_ostream_t data_stream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
        if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
        {
            ESP_LOGE(TAG, "Failed to encode TraceRoute data payload: %s", PB_GET_ERROR(&data_stream));
            return false;
        }

        uint32_t pid = encryptAndSend(data_buf,
                                      data_stream.bytes_written,
                                      dest,
                                      false,
                                      _config.lora_config.hop_limit,
                                      PacketPriority::DEFAULT,
                                      meshtastic_PortNum_TRACEROUTE_APP);
        if (pid)
        {
            ESP_LOGI(TAG, "TraceRoute request sent to 0x%08lX", (unsigned long)dest);
            return true;
        }
        ESP_LOGW(TAG, "Failed to enqueue TraceRoute request");
        return false;
    }

    meshtastic_Config_LoRaConfig_RegionCode MeshService::regionCodeFromName(const std::string& name)
    {
        for (const auto& r : regions)
        {
            if (name == r.name)
                return r.code;
        }
        return meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    }

    bool MeshService::generateKeypair(uint8_t* out_private, uint8_t* out_public)
    {
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_ecp_group grp;
        mbedtls_mpi d;
        mbedtls_ecp_point Q;

        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_ecp_group_init(&grp);
        mbedtls_mpi_init(&d);
        mbedtls_ecp_point_init(&Q);

        const char* pers = "meshtastic_x25519";
        bool gen_ok = false;

        int rc_seed =
            mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, strlen(pers));
        int rc_grp = (rc_seed == 0) ? mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) : -1;
        int rc_gen = (rc_grp == 0) ? mbedtls_ecdh_gen_public(&grp, &d, &Q, mbedtls_ctr_drbg_random, &ctr_drbg) : -1;

        if (rc_gen == 0)
        {
            mbedtls_mpi inv_z;
            mbedtls_mpi_init(&inv_z);
            if (mbedtls_mpi_cmp_int(&Q.MBEDTLS_PRIVATE(Z), 1) != 0)
            {
                mbedtls_mpi_inv_mod(&inv_z, &Q.MBEDTLS_PRIVATE(Z), &grp.P);
                mbedtls_mpi_mul_mpi(&Q.MBEDTLS_PRIVATE(X), &Q.MBEDTLS_PRIVATE(X), &inv_z);
                mbedtls_mpi_mod_mpi(&Q.MBEDTLS_PRIVATE(X), &Q.MBEDTLS_PRIVATE(X), &grp.P);
                mbedtls_mpi_lset(&Q.MBEDTLS_PRIVATE(Z), 1);
            }
            mbedtls_mpi_free(&inv_z);

            if (out_private)
            {
                memset(out_private, 0, 32);
                mbedtls_mpi_write_binary_le(&d, out_private, 32);
            }
            if (out_public)
            {
                memset(out_public, 0, 32);
                mbedtls_mpi_write_binary_le(&Q.MBEDTLS_PRIVATE(X), out_public, 32);
            }

            gen_ok = true;
            ESP_LOGD(TAG, "X25519 keypair generated successfully");
        }
        else
        {
            ESP_LOGE(TAG, "X25519 keygen failed: seed=%d grp=%d gen=%d", rc_seed, rc_grp, rc_gen);
        }

        mbedtls_ecp_point_free(&Q);
        mbedtls_mpi_free(&d);
        mbedtls_ecp_group_free(&grp);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);

        return gen_ok;
    }

    bool MeshService::derivePublicFromPrivate(const uint8_t* private_key, uint8_t* out_public)
    {
        if (!private_key || !out_public)
            return false;
        mbedtls_ecp_group grp;
        mbedtls_mpi d;
        mbedtls_ecp_point Q;

        mbedtls_ecp_group_init(&grp);
        mbedtls_mpi_init(&d);
        mbedtls_ecp_point_init(&Q);

        bool ok = false;
        if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) != 0)
        {
            ESP_LOGE(TAG, "derivePublic: ecp_group_load failed");
            goto cleanup;
        }
        if (mbedtls_mpi_read_binary_le(&d, private_key, 32) != 0)
        {
            ESP_LOGE(TAG, "derivePublic: failed to load private key");
            goto cleanup;
        }
        if (mbedtls_ecp_mul(&grp, &Q, &d, &grp.MBEDTLS_PRIVATE(G), esp_rng_cb, nullptr) != 0)
        {
            ESP_LOGE(TAG, "derivePublic: ecp_mul failed");
            goto cleanup;
        }
        mbedtls_mpi inv_z;
        mbedtls_mpi_init(&inv_z);
        if (mbedtls_mpi_cmp_int(&Q.MBEDTLS_PRIVATE(Z), 1) != 0)
        {
            mbedtls_mpi_inv_mod(&inv_z, &Q.MBEDTLS_PRIVATE(Z), &grp.P);
            mbedtls_mpi_mul_mpi(&Q.MBEDTLS_PRIVATE(X), &Q.MBEDTLS_PRIVATE(X), &inv_z);
            mbedtls_mpi_mod_mpi(&Q.MBEDTLS_PRIVATE(X), &Q.MBEDTLS_PRIVATE(X), &grp.P);
            mbedtls_mpi_lset(&Q.MBEDTLS_PRIVATE(Z), 1);
        }
        mbedtls_mpi_free(&inv_z);
        memset(out_public, 0, 32);
        if (mbedtls_mpi_write_binary_le(&Q.MBEDTLS_PRIVATE(X), out_public, 32) == 0)
            ok = true;
        else
            ESP_LOGE(TAG, "derivePublic: failed to write public key");
    cleanup:
        mbedtls_ecp_point_free(&Q);
        mbedtls_mpi_free(&d);
        mbedtls_ecp_group_free(&grp);
        return ok;
    }

    void MeshService::initRegion()
    {
        const RegionInfo* r = regions;
        meshtastic_Config_LoRaConfig_RegionCode wanted_region = _config.lora_config.region;

        ESP_LOGD(TAG, "Looking for region: %d", wanted_region);
        // Find matching region
        for (; r->code != meshtastic_Config_LoRaConfig_RegionCode_UNSET && r->code != wanted_region; r++)
            ;

        if (r->code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        {
            // Region not found, use UNSET (which defaults to US-like)
            r = &regions[sizeof(regions) / sizeof(regions[0]) - 1]; // Last entry is UNSET
        }

        _my_region = r;
        ESP_LOGI(TAG,
                 "Initialized region: %s (freq: %.1f-%.1f MHz, power limit: %d dBm)",
                 r->name,
                 r->freq_start,
                 r->freq_end,
                 r->power_limit);
    }

    void MeshService::applyModemConfig()
    {
        if (!_radio || !_my_region)
        {
            ESP_LOGE(TAG, "Radio or region not initialized");
            return;
        }

        meshtastic_Config_LoRaConfig& loraConfig = _config.lora_config;
        bool validConfig = false;
        float regionSpanKHz = (_my_region->freq_end - _my_region->freq_start) * 1000.0f;

        // Apply modem configuration with validation loop
        while (!validConfig)
        {
            if (loraConfig.use_preset)
            {
                // Map modem preset to modulation parameters
                const ModemPresetInfo* pi = getModemPresetInfo(static_cast<int>(loraConfig.modem_preset));
                if (!pi)
                    pi = &modem_presets[0]; // default to LONG_FAST
                _bw = _my_region->wide_lora ? pi->bw_wide : pi->bw;
                _cr = pi->cr;
                _sf = pi->sf;
            }
            else
            {
                // Use manual settings
                _sf = loraConfig.spread_factor;
                _cr = loraConfig.coding_rate;
                _bw = loraConfig.bandwidth;

                // Handle special bandwidth values
                if (_bw == 31)
                    _bw = 31.25f;
                if (_bw == 62)
                    _bw = 62.5f;
                if (_bw == 200)
                    _bw = 203.125f;
                if (_bw == 400)
                    _bw = 406.25f;
                if (_bw == 800)
                    _bw = 812.5f;
                if (_bw == 1600)
                    _bw = 1625.0f;

                // Validate manual settings - if they're 0, fall back to default preset
                if (_sf == 0 || _cr == 0 || _bw == 0)
                {
                    ESP_LOGW(TAG, "Manual modem settings are zero, falling back to LONG_FAST preset");
                    loraConfig.use_preset = true;
                    loraConfig.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
                    continue; // Retry with preset
                }
            }

            // Validate bandwidth against region limits
            if (regionSpanKHz < _bw)
            {
                ESP_LOGW(TAG, "%s region too narrow for %.0f kHz bandwidth. Falling back to LongFast.", _my_region->name, _bw);
                loraConfig.use_preset = true;
                loraConfig.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
                continue; // Retry with fallback
            }

            validConfig = true;
        }

        // Apply power limits
        int8_t power = loraConfig.tx_power;
        if (power == 0 || power > _my_region->power_limit)
        {
            power = _my_region->power_limit;
        }
        if (power == 0)
        {
            power = 17; // Default if no valid power limit
        }
        loraConfig.tx_power = power;

        // Calculate channel number from channel name hash
        const char* channelName = getChannelNameForHash(_config);
        ESP_LOGI(TAG, "Primary channel name: %s", channelName);
        // Calculate number of channels
        uint32_t numChannels =
            (uint32_t)floor((_my_region->freq_end - _my_region->freq_start) / (_my_region->spacing + (_bw / 1000.0f)));

        // Generate channel number from hash
        // const char* presetName = loraConfig.use_preset ? getPresetName(loraConfig.modem_preset) : "Custom";
        uint32_t channel_num =
            (loraConfig.channel_num ? loraConfig.channel_num - 1 : hashChannelName(channelName)) % numChannels;
        // Calculate frequency (in MHz)
        float freq_mhz = _my_region->freq_start + (_bw / 2000.0f) + (channel_num * (_my_region->spacing + _bw / 1000.0f));

        // Override if verbatim frequency is set (override_frequency is in MHz)
        if (loraConfig.override_frequency > 0)
        {
            freq_mhz = loraConfig.override_frequency;
            channel_num = (uint32_t)-1;
        }

        // Apply frequency offset (in MHz)
        freq_mhz += loraConfig.frequency_offset;

        _saved_channel_num = channel_num;
        _saved_freq = freq_mhz;

        // Build radio configuration
        HAL::LoRaConfig config;
        config.frequency_hz = (uint32_t)(freq_mhz * 1000000.0f); // Convert MHz to Hz
        config.bandwidth_hz = (uint32_t)(_bw * 1000.0f);
        config.spreading_factor = _sf;
        config.coding_rate = _cr;
        config.tx_power_dbm = power;
        config.preamble_length = 8;
        config.sync_word = 0x2B; // Meshtastic sync word
        config.crc_enabled = true;
        config.implicit_header = false;
        config.iq_inverted = false;
        config.rx_boosted_gain = loraConfig.sx126x_rx_boosted_gain;

        ESP_LOGI(TAG,
                 "Applying modem config: region=%s, freq=%.3f MHz, BW=%.0f kHz, SF=%d, CR=4/%d, Power=%d dBm, ch=%lu",
                 _my_region->name,
                 freq_mhz,
                 _bw,
                 _sf,
                 _cr,
                 power,
                 (unsigned long)channel_num);
        ESP_LOGI(TAG,
                 "Region span: %.1f-%.1f MHz (%.0f kHz), numChannels=%lu",
                 _my_region->freq_start,
                 _my_region->freq_end,
                 regionSpanKHz,
                 (unsigned long)numChannels);

        _radio->setConfig(config);

        _slot_time_ms = computeSlotTimeMsec();
        ESP_LOGI(TAG, "Slot time: %lu ms", (unsigned long)_slot_time_ms);
    }

    uint32_t MeshService::generateNodeId()
    {
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);

        // Create node ID from last 4 bytes of MAC
        uint32_t node_id = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | ((uint32_t)mac[5]);

        // Ensure high bit is set (indicates locally administered)
        // node_id |= 0xс80000000;

        return node_id;
    }

    void MeshService::broadcastNodeInfo()
    {
        // CLIENT_HIDDEN role should not broadcast
        if (_config.role == meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN)
        {
            ESP_LOGD(TAG, "Skipping NodeInfo broadcast (CLIENT_HIDDEN role)");
            return;
        }
        sendNodeInfo(0xFFFFFFFF, 0, false);
    }

    uint32_t MeshService::encryptAndSend(const uint8_t* data_buf,
                                         size_t data_len,
                                         uint32_t dest,
                                         bool want_ack,
                                         uint8_t hop_limit,
                                         PacketPriority priority,
                                         meshtastic_PortNum port_num,
                                         uint8_t* out_raw_buf,
                                         size_t* out_raw_len)
    {
        uint8_t key[32] = {};
        size_t key_len = 0;
        bool no_crypto = false;
        if (!expandChannelPsk(_config.primary_channel.settings, key, key_len, no_crypto))
        {
            ESP_LOGE(TAG, "Failed to expand channel PSK");
            return 0;
        }

        uint8_t channel_hash = 0;
        computeChannelHash(_config, key, key_len, channel_hash);

        if (data_len > (MAX_LORA_PAYLOAD - MESHTASTIC_HEADER_LENGTH))
        {
            ESP_LOGE(TAG, "Payload too large for LoRa (%u bytes)", (unsigned)data_len);
            return 0;
        }

        uint32_t packet_id = _router.generatePacketId();
        uint8_t payload[MAX_LORA_PAYLOAD] = {};

        if (no_crypto)
        {
            memcpy(payload, data_buf, data_len);
        }
        else
        {
#if MESH_HAS_MBEDTLS
            mbedtls_aes_context ctx;
            mbedtls_aes_init(&ctx);
            if (mbedtls_aes_setkey_enc(&ctx, key, (int)(key_len * 8)) != 0)
            {
                mbedtls_aes_free(&ctx);
                ESP_LOGE(TAG, "Failed to set AES key");
                return 0;
            }

            uint8_t nonce[16] = {};
            writeU64Le(nonce, (uint64_t)packet_id);
            writeU32Le(nonce + 8, _config.node_id);

            size_t nc_off = 0;
            uint8_t stream_block[16] = {};
            if (mbedtls_aes_crypt_ctr(&ctx, data_len, &nc_off, nonce, stream_block, data_buf, payload) != 0)
            {
                mbedtls_aes_free(&ctx);
                ESP_LOGE(TAG, "AES-CTR encrypt failed");
                return 0;
            }
            mbedtls_aes_free(&ctx);
#else
            ESP_LOGE(TAG, "AES support not available");
            return 0;
#endif
        }

        PacketHeader header = {};
        header.to = dest;
        header.from = _config.node_id;
        header.id = packet_id;
        header.channel = channel_hash;
        header.next_hop = 0;
        header.relay_node = (uint8_t)(_config.node_id & 0xFFu);
        header.flags = (hop_limit & PACKET_FLAGS_HOP_LIMIT_MASK) |
                       ((hop_limit << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK) |
                       (want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0);

        uint8_t radio_buf[MAX_LORA_PAYLOAD] = {};
        memcpy(radio_buf, &header, sizeof(header));
        memcpy(radio_buf + sizeof(header), payload, data_len);
        const size_t radio_len = sizeof(header) + data_len;

        if (out_raw_buf)
            memcpy(out_raw_buf, radio_buf, radio_len);
        if (out_raw_len)
            *out_raw_len = radio_len;

        if (_router.enqueueTxRaw(radio_buf, (uint8_t)radio_len, priority, (uint8_t)port_num))
            return packet_id;

        return 0;
    }

    void MeshService::sendNodeInfo(uint32_t dest, uint8_t channel, bool want_response)
    {
        if (!_nodedb)
        {
            return;
        }

        // Create User message (NODEINFO_APP payload is User struct, not NodeInfo)
        meshtastic_User user = meshtastic_User_init_default;
        strncpy(user.short_name, _config.short_name, sizeof(user.short_name) - 1);
        strncpy(user.long_name, _config.long_name, sizeof(user.long_name) - 1);
        user.hw_model = meshtastic_HardwareModel_M5STACK_CARDPUTER_ADV;
        user.role = _config.role;
        // Convert node ID to hex string for id field
        snprintf(user.id, sizeof(user.id), "!%08lx", (unsigned long)_config.node_id);
        esp_efuse_mac_get_default(user.macaddr);
        // Public key
        if (_config.public_key_len == 32)
        {
            memcpy(user.public_key.bytes, _config.public_key, 32);
            user.public_key.size = 32;
        }
        user.has_is_unmessagable = true;
        user.is_unmessagable = _config.is_unmessagable;

        bool is_broadcast = (dest == 0xFFFFFFFF);
        if (is_broadcast)
        {
            ESP_LOGD(TAG,
                     "NodeID: !%08lx, Short name: %s, Long name: %s, BLE MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     (unsigned long)_config.node_id,
                     user.short_name,
                     user.long_name,
                     user.macaddr[0],
                     user.macaddr[1],
                     user.macaddr[2],
                     user.macaddr[3],
                     user.macaddr[4],
                     user.macaddr[5]);
        }

        // Encode User into a Data payload (NODEINFO_APP expects User, not NodeInfo)
        uint8_t user_buf[meshtastic_User_size + 16];
        pb_ostream_t user_stream = pb_ostream_from_buffer(user_buf, sizeof(user_buf));
        if (!pb_encode(&user_stream, meshtastic_User_fields, &user))
        {
            ESP_LOGE(TAG, "Failed to encode User for NodeInfo: %s", PB_GET_ERROR(&user_stream));
            return;
        }

        meshtastic_Data data = meshtastic_Data_init_default;
        data.portnum = meshtastic_PortNum_NODEINFO_APP;
        data.payload.size = user_stream.bytes_written;
        memcpy(data.payload.bytes, user_buf, data.payload.size);
        data.want_response = want_response;
        applyOkToMqtt(data);

        uint8_t data_buf[MAX_LORA_PAYLOAD] = {};
        pb_ostream_t data_stream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
        if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
        {
            ESP_LOGE(TAG, "Failed to encode NodeInfo data payload: %s", PB_GET_ERROR(&data_stream));
            return;
        }

        PacketPriority priority = is_broadcast ? PacketPriority::BACKGROUND : PacketPriority::DEFAULT;
        uint32_t pid = encryptAndSend(data_buf,
                                      data_stream.bytes_written,
                                      dest,
                                      false,
                                      _config.lora_config.hop_limit,
                                      priority,
                                      meshtastic_PortNum_NODEINFO_APP);
        if (pid)
        {
            if (is_broadcast)
                ESP_LOGI(TAG, "Broadcasting node info (node ID: 0x%08lX)", (unsigned long)_config.node_id);
            else
                ESP_LOGI(TAG, "Sending node info to 0x%08lX", (unsigned long)dest);
        }
        else
        {
            ESP_LOGW(TAG, "Failed to enqueue node info");
        }
    }

    void MeshService::sendNeighborInfo(uint32_t dest, uint8_t channel, bool want_response)
    {
        if (!_nodedb)
            return;

        meshtastic_NeighborInfo ni = meshtastic_NeighborInfo_init_zero;
        ni.node_id = _config.node_id;

        size_t count = _nodedb->getNodeCount();
        for (size_t i = 0; i < count && ni.neighbors_count < 10; i++)
        {
            NodeInfo node;
            if (!_nodedb->getNodeByIndex(i, node))
                continue;
            if (!node.info.has_user)
                continue;
            if (!node.info.has_hops_away || node.info.hops_away != 0)
                continue;

            auto& nb = ni.neighbors[ni.neighbors_count];
            nb.node_id = node.info.num;
            nb.snr = node.info.snr;
            nb.last_rx_time = node.info.last_heard;
            ni.neighbors_count++;
        }

        ESP_LOGI(TAG, "Sending NeighborInfo to 0x%08lX (%u neighbors)", (unsigned long)dest, (unsigned)ni.neighbors_count);

        uint8_t ni_buf[meshtastic_NeighborInfo_size + 16];
        pb_ostream_t ni_stream = pb_ostream_from_buffer(ni_buf, sizeof(ni_buf));
        if (!pb_encode(&ni_stream, meshtastic_NeighborInfo_fields, &ni))
        {
            ESP_LOGE(TAG, "Failed to encode NeighborInfo: %s", PB_GET_ERROR(&ni_stream));
            return;
        }

        meshtastic_Data data = meshtastic_Data_init_default;
        data.portnum = meshtastic_PortNum_NEIGHBORINFO_APP;
        data.payload.size = ni_stream.bytes_written;
        memcpy(data.payload.bytes, ni_buf, data.payload.size);
        data.want_response = want_response;
        applyOkToMqtt(data);

        uint8_t data_buf[MAX_LORA_PAYLOAD] = {};
        pb_ostream_t data_stream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
        if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
        {
            ESP_LOGE(TAG, "Failed to encode NeighborInfo data payload: %s", PB_GET_ERROR(&data_stream));
            return;
        }

        uint32_t pid = encryptAndSend(data_buf,
                                      data_stream.bytes_written,
                                      dest,
                                      false,
                                      _config.lora_config.hop_limit,
                                      PacketPriority::DEFAULT,
                                      meshtastic_PortNum_NEIGHBORINFO_APP);
        if (pid)
            ESP_LOGI(TAG, "NeighborInfo sent to 0x%08lX", (unsigned long)dest);
        else
            ESP_LOGW(TAG, "Failed to enqueue NeighborInfo");
    }

    bool MeshService::getNode(uint32_t node_id, NodeInfo& out) const
    {
        if (node_id == _config.node_id)
        {
            out = {};
            out.info.num = _config.node_id;
            out.info.has_user = true;
            strncpy(out.info.user.short_name, _config.short_name, sizeof(out.info.user.short_name) - 1);
            strncpy(out.info.user.long_name, _config.long_name, sizeof(out.info.user.long_name) - 1);
            snprintf(out.info.user.id, sizeof(out.info.user.id), "!%08lx", (unsigned long)_config.node_id);
            out.info.user.hw_model = meshtastic_HardwareModel_M5STACK_CARDPUTER_ADV;
            out.info.user.role = _config.role;
            esp_efuse_mac_get_default(out.info.user.macaddr);
            if (_config.public_key_len == 32)
            {
                memcpy(out.info.user.public_key.bytes, _config.public_key, 32);
                out.info.user.public_key.size = 32;
            }
            out.info.user.has_is_unmessagable = true;
            out.info.user.is_unmessagable = _config.is_unmessagable;
            out.last_rssi = 0;
            out.relay_node = 0;
            return true;
        }
        if (_nodedb)
        {
            return _nodedb->getNode(node_id, out);
        }
        return false;
    }

    bool MeshService::sendPosition(uint32_t dest, uint8_t channel, bool want_response)
    {
        meshtastic_Position position = meshtastic_Position_init_default;
        bool has_position = false;
        static uint32_t seq_number = 0;
        const uint32_t pf = _config.position_flags;

        // If fixed position is configured, always use it (privacy: never leak live GPS)
        if (_config.position == MeshConfig::POSITION_FIXED)
        {
            position.has_latitude_i = true;
            position.latitude_i = _config.fixed_latitude;
            position.has_longitude_i = true;
            position.longitude_i = _config.fixed_longitude;
            if (pf & meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE)
            {
                position.has_altitude = true;
                position.altitude = _config.fixed_altitude;
            }
            position.location_source = meshtastic_Position_LocSource_LOC_MANUAL;

            has_position = true;
            ESP_LOGI(TAG,
                     "Sending fixed position: lat=%ld lon=%ld alt=%ld flags=0x%08lx",
                     (long)position.latitude_i,
                     (long)position.longitude_i,
                     (long)position.altitude,
                     (unsigned long)pf);
        }
        // Otherwise use live GPS if available (POSITION_GPS mode)
        else if (_config.position == MeshConfig::POSITION_GPS && _gps && _gps->hasFix())
        {
            position.has_latitude_i = true;
            position.latitude_i = _gps->getLatitudeI();
            position.has_longitude_i = true;
            position.longitude_i = _gps->getLongitudeI();
            if (pf & meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE)
            {
                position.has_altitude = true;
                position.altitude = _gps->getAltitude();
            }
            if (pf & meshtastic_Config_PositionConfig_PositionFlags_SATINVIEW)
            {
                position.sats_in_view = _gps->getSatellites();
            }
            if (pf & meshtastic_Config_PositionConfig_PositionFlags_SEQ_NO)
            {
                position.seq_number = seq_number++;
            }
            if (pf & meshtastic_Config_PositionConfig_PositionFlags_DOP)
            {
                position.HDOP = _gps->getHDOP();
            }
            if (pf & meshtastic_Config_PositionConfig_PositionFlags_TIMESTAMP)
            {
                position.time = _gps->getTime();
            }
            position.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;

            if ((pf & meshtastic_Config_PositionConfig_PositionFlags_SPEED) && _gps->getGroundSpeed() > 0)
            {
                position.has_ground_speed = true;
                position.ground_speed = _gps->getGroundSpeed();
            }
            if ((pf & meshtastic_Config_PositionConfig_PositionFlags_HEADING) && _gps->getGroundTrack() > 0)
            {
                position.has_ground_track = true;
                position.ground_track = _gps->getGroundTrack();
            }

            has_position = true;
            ESP_LOGI(TAG,
                     "Sending GPS position: lat=%ld lon=%ld alt=%ld sats=%lu flags=0x%08lx",
                     (long)position.latitude_i,
                     (long)position.longitude_i,
                     (long)position.altitude,
                     (unsigned long)position.sats_in_view,
                     (unsigned long)pf);
        }

        if (!has_position)
        {
            ESP_LOGI(TAG, "No position available to send");
            return false;
        }
        // apply precision bits for channel
        if (dest == 0xFFFFFFFF)
        {
            auto ch = _nodedb->getChannel(channel);
            if (ch)
            {
                uint32_t precision = ch->settings.module_settings.position_precision;
                if (precision < 32 && precision > 0)
                {
                    ESP_LOGD(TAG, "Applying precision bits for channel %d: %d", channel, precision);
                    position.latitude_i = position.latitude_i & (UINT32_MAX << (32 - precision));
                    position.longitude_i = position.longitude_i & (UINT32_MAX << (32 - precision));
                    position.latitude_i += (1 << (31 - precision));
                    position.longitude_i += (1 << (31 - precision));
                    position.precision_bits = precision;
                }
            }
        }

        // 1. Encode Position protobuf into inner payload
        uint8_t pos_buf[meshtastic_Position_size];
        pb_ostream_t pos_stream = pb_ostream_from_buffer(pos_buf, sizeof(pos_buf));
        if (!pb_encode(&pos_stream, meshtastic_Position_fields, &position))
        {
            ESP_LOGE(TAG, "Failed to encode Position: %s", PB_GET_ERROR(&pos_stream));
            return false;
        }

        // 2. Wrap in meshtastic_Data envelope
        meshtastic_Data data = meshtastic_Data_init_default;
        data.portnum = meshtastic_PortNum_POSITION_APP;
        data.payload.size = pos_stream.bytes_written;
        memcpy(data.payload.bytes, pos_buf, data.payload.size);
        data.want_response = want_response;
        applyOkToMqtt(data);

        uint8_t data_buf[MAX_LORA_PAYLOAD] = {};
        pb_ostream_t data_stream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
        if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
        {
            ESP_LOGE(TAG, "Failed to encode Position data payload: %s", PB_GET_ERROR(&data_stream));
            return false;
        }

        bool is_broadcast = (dest == 0xFFFFFFFF);
        PacketPriority priority = is_broadcast ? PacketPriority::BACKGROUND : PacketPriority::DEFAULT;
        uint32_t pid = encryptAndSend(data_buf,
                                      data_stream.bytes_written,
                                      dest,
                                      false,
                                      _config.lora_config.hop_limit,
                                      priority,
                                      meshtastic_PortNum_POSITION_APP);
        if (pid)
        {
            ESP_LOGI(TAG, "Position packet queued to 0x%08lX", (unsigned long)dest);
            return true;
        }
        ESP_LOGW(TAG, "Failed to enqueue position packet");
        return false;
    }

    static void expandGreetingTemplate(const char* tmpl,
                                       char* out,
                                       size_t out_size,
                                       const Mesh::NodeInfo* node,
                                       uint32_t node_id,
                                       uint8_t hops,
                                       int16_t rssi,
                                       float snr)
    {
        size_t wi = 0;
        size_t len = strlen(tmpl);
        for (size_t i = 0; i < len && wi < out_size - 1; i++)
        {
            if (tmpl[i] == '#' && i + 1 < len)
            {
                const char* rest = tmpl + i;
                size_t remain = len - i;

                struct
                {
                    const char* tag;
                    size_t tag_len;
                } macros[] = {
                    {"#short", 6},
                    {"#long", 5},
                    {"#id", 3},
                    {"#hops", 5},
                    {"#rssi", 5},
                    {"#snr", 4},
                };

                bool matched = false;
                for (auto& m : macros)
                {
                    if (remain >= m.tag_len && strncmp(rest, m.tag, m.tag_len) == 0)
                    {
                        char val[32] = {};
                        if (m.tag[1] == 's' && m.tag[2] == 'h') // #short
                        {
                            if (node && node->info.user.short_name[0])
                                strncpy(val, node->info.user.short_name, sizeof(val) - 1);
                            else
                                snprintf(val, sizeof(val), "%04X", (unsigned)(node_id & 0xFFFF));
                        }
                        else if (m.tag[1] == 'l') // #long
                        {
                            if (node && node->info.user.long_name[0])
                                strncpy(val, node->info.user.long_name, sizeof(val) - 1);
                            else
                                snprintf(val, sizeof(val), "!%08lx", (unsigned long)node_id);
                        }
                        else if (m.tag[1] == 'i') // #id
                        {
                            snprintf(val, sizeof(val), "!%08lx", (unsigned long)node_id);
                        }
                        else if (m.tag[1] == 'h') // #hops
                        {
                            snprintf(val, sizeof(val), "%u", (unsigned)hops);
                        }
                        else if (m.tag[1] == 'r') // #rssi
                        {
                            snprintf(val, sizeof(val), "%d", (int)rssi);
                        }
                        else if (m.tag[1] == 's') // #snr
                        {
                            snprintf(val, sizeof(val), "%.1f", snr);
                        }

                        size_t vlen = strlen(val);
                        if (wi + vlen < out_size - 1)
                        {
                            memcpy(out + wi, val, vlen);
                            wi += vlen;
                        }
                        i += m.tag_len - 1; // -1 because loop increments
                        matched = true;
                        break;
                    }
                }
                if (!matched)
                    out[wi++] = tmpl[i];
            }
            else
            {
                out[wi++] = tmpl[i];
            }
        }
        out[wi] = '\0';
    }

    void MeshService::sendNewNodeGreeting(uint32_t node_id, uint8_t channel, uint8_t hops, int16_t rssi, float snr)
    {
        if (!_nodedb || channel >= 8)
            return;

        auto* ch = _nodedb->getChannel(channel);
        if (!ch || ch->role == meshtastic_Channel_Role_DISABLED)
            return;

        const auto& greeting = _nodedb->getGreeting(channel);

        NodeInfo node_info;
        const NodeInfo* node_ptr = getNode(node_id, node_info) ? &node_info : nullptr;

        char expanded[256];

        if (greeting.channel_text[0])
        {
            expandGreetingTemplate(greeting.channel_text, expanded, sizeof(expanded), node_ptr, node_id, hops, rssi, snr);
            uint32_t pid = sendText(expanded, 0xFFFFFFFF, channel);
            if (pid)
            {
                TextMessage msg;
                msg.id = pid;
                msg.from = _config.node_id;
                msg.to = 0xFFFFFFFF;
                msg.timestamp = (uint32_t)time(nullptr);
                msg.channel = channel;
                msg.is_direct = false;
                msg.read = true;
                msg.text = expanded;
                msg.status = TextMessage::Status::PENDING;
                MeshDataStore::getInstance().addMessage(msg);
            }
            ESP_LOGI(TAG, "Sent channel greeting on ch%d to new node 0x%08lX: %s", channel, (unsigned long)node_id, expanded);
        }

        if (greeting.dm_text[0])
        {
            expandGreetingTemplate(greeting.dm_text, expanded, sizeof(expanded), node_ptr, node_id, hops, rssi, snr);
            uint32_t pid = sendText(expanded, node_id, channel);
            if (pid)
            {
                TextMessage msg;
                msg.id = pid;
                msg.from = _config.node_id;
                msg.to = node_id;
                msg.timestamp = (uint32_t)time(nullptr);
                msg.channel = channel;
                msg.is_direct = true;
                msg.read = true;
                msg.text = expanded;
                msg.status = TextMessage::Status::PENDING;
                MeshDataStore::getInstance().addMessage(msg);
            }
            ESP_LOGI(TAG, "Sent DM greeting on ch%d to new node 0x%08lX: %s", channel, (unsigned long)node_id, expanded);
        }
    }

    bool MeshService::sendDeviceTelemetry(uint32_t dest, uint8_t channel)
    {
        // check role != CLIENT_HDDEN
        if (_config.role == meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN)
        {
            ESP_LOGI(TAG, "Skipping device telemetry broadcast due to role");
            return false;
        }
        // Build DeviceMetrics telemetry
        meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
        telemetry.time = (uint32_t)time(nullptr);
        telemetry.which_variant = meshtastic_Telemetry_device_metrics_tag;

        auto& dm = telemetry.variant.device_metrics;
        dm = meshtastic_DeviceMetrics_init_zero;

        // Uptime
        if (_config.telemetry_uptime)
        {
            dm.has_uptime_seconds = true;
            dm.uptime_seconds = (uint32_t)(millis() / 1000);
        }

        // Battery info from callback
        if (_battery_callback)
        {
            BatteryInfo bat = _battery_callback();
            if (_config.telemetry_bat_level)
            {
                dm.has_battery_level = true;
                dm.battery_level = bat.level;
            }
            if (_config.telemetry_voltage)
            {
                dm.has_voltage = true;
                dm.voltage = bat.voltage;
            }
        }
        // Channel utilization and TX airtime
        if (_config.telemetry_ch_util)
        {
            dm.has_channel_utilization = true;
            dm.channel_utilization = _getChannelUtilization();
        }
        if (_config.telemetry_air_util)
        {
            dm.has_air_util_tx = true;
            dm.air_util_tx = _getAirUtilTx();
        }

        ESP_LOGI(TAG,
                 "Sending device telemetry: battery=%lu%%, voltage=%.2fV, uptime=%lus, ch_util=%.1f%%, air_tx=%.1f%%",
                 (unsigned long)dm.battery_level,
                 dm.has_voltage ? dm.voltage : 0.0f,
                 (unsigned long)dm.uptime_seconds,
                 dm.channel_utilization,
                 dm.air_util_tx);

        // 1. Encode Telemetry protobuf
        uint8_t tel_buf[meshtastic_Telemetry_size];
        pb_ostream_t tel_stream = pb_ostream_from_buffer(tel_buf, sizeof(tel_buf));
        if (!pb_encode(&tel_stream, meshtastic_Telemetry_fields, &telemetry))
        {
            ESP_LOGE(TAG, "Failed to encode Telemetry: %s", PB_GET_ERROR(&tel_stream));
            return false;
        }

        // 2. Wrap in meshtastic_Data envelope
        meshtastic_Data data = meshtastic_Data_init_default;
        data.portnum = meshtastic_PortNum_TELEMETRY_APP;
        data.payload.size = tel_stream.bytes_written;
        memcpy(data.payload.bytes, tel_buf, data.payload.size);
        data.want_response = false;
        applyOkToMqtt(data);

        uint8_t data_buf[MAX_LORA_PAYLOAD] = {};
        pb_ostream_t data_stream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
        if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
        {
            ESP_LOGE(TAG, "Failed to encode Telemetry data payload: %s", PB_GET_ERROR(&data_stream));
            return false;
        }

        uint32_t pid = encryptAndSend(data_buf,
                                      data_stream.bytes_written,
                                      dest,
                                      false,
                                      _config.lora_config.hop_limit,
                                      PacketPriority::BACKGROUND,
                                      meshtastic_PortNum_TELEMETRY_APP);
        if (pid)
        {
            ESP_LOGI(TAG, "Telemetry packet queued");
            return true;
        }
        ESP_LOGW(TAG, "Failed to enqueue telemetry packet");
        return false;
    }

    uint8_t MeshService::getHopLimitForResponse(uint8_t hop_start, uint8_t hop_limit) const
    {
        if (hop_start != 0)
        {
            // Calculate hops used by the request
            uint8_t hops_used = (hop_start < hop_limit) ? _config.lora_config.hop_limit : (hop_start - hop_limit);

            if (hops_used > _config.lora_config.hop_limit)
            {
                return hops_used; // Use the same amount of hops if more were needed
            }
            else if ((uint8_t)(hops_used + 2) < _config.lora_config.hop_limit)
            {
                return hops_used + 2; // Use hops needed plus margin
            }
        }
        return _config.lora_config.hop_limit; // Default hop limit
    }

    bool MeshService::sendRouting(
        uint32_t to, uint32_t packet_id, uint8_t channel, uint8_t hop_limit, meshtastic_Routing_Error error_code)
    {
        const bool is_ack = (error_code == meshtastic_Routing_Error_NONE);
        if (is_ack)
            ESP_LOGI(TAG, "Sending ACK to 0x%08lX for packet 0x%08lX", (unsigned long)to, (unsigned long)packet_id);
        else
            ESP_LOGW(TAG,
                     "Sending NACK to 0x%08lX for packet 0x%08lX (error=%d)",
                     (unsigned long)to,
                     (unsigned long)packet_id,
                     (int)error_code);

        meshtastic_Routing routing = meshtastic_Routing_init_default;
        routing.which_variant = meshtastic_Routing_error_reason_tag;
        routing.error_reason = error_code;

        // Encode the Routing message
        uint8_t routing_buf[meshtastic_Routing_size];
        pb_ostream_t routing_stream = pb_ostream_from_buffer(routing_buf, sizeof(routing_buf));
        if (!pb_encode(&routing_stream, meshtastic_Routing_fields, &routing))
        {
            ESP_LOGE(TAG, "Failed to encode Routing reply: %s", PB_GET_ERROR(&routing_stream));
            return false;
        }

        // Create Data message
        meshtastic_Data data = meshtastic_Data_init_default;
        data.portnum = meshtastic_PortNum_ROUTING_APP;
        data.payload.size = routing_stream.bytes_written;
        memcpy(data.payload.bytes, routing_buf, data.payload.size);
        data.request_id = packet_id; // Reference the original packet ID
        data.want_response = false;
        applyOkToMqtt(data);

        uint8_t data_buf[MAX_LORA_PAYLOAD] = {};
        pb_ostream_t data_stream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
        if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
        {
            ESP_LOGE(TAG, "Failed to encode Data for routing reply: %s", PB_GET_ERROR(&data_stream));
            return false;
        }

        uint32_t pid = encryptAndSend(data_buf,
                                      data_stream.bytes_written,
                                      to,
                                      false,
                                      hop_limit,
                                      PacketPriority::ACK,
                                      meshtastic_PortNum_ROUTING_APP);
        if (pid)
        {
            ESP_LOGI(TAG, "Routing reply sent to 0x%08lX (error=%d)", (unsigned long)to, (int)error_code);
            return true;
        }
        ESP_LOGW(TAG, "Failed to enqueue routing reply");
        return false;
    }

    bool MeshService::sendAck(uint32_t to, uint32_t packet_id, uint8_t channel, uint8_t hop_limit)
    {
        return sendRouting(to, packet_id, channel, hop_limit, meshtastic_Routing_Error_NONE);
    }

    static meshtastic_Config_DeviceConfig_Role roleFromName(const std::string& name)
    {
        // String values must exactly match getRoleName() and the settings option list
        if (name == "Client")
            return meshtastic_Config_DeviceConfig_Role_CLIENT;
        if (name == "Client Mute")
            return meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE;
        if (name == "Client Hidden")
            return meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN;
        if (name == "Client Base")
            return meshtastic_Config_DeviceConfig_Role_CLIENT_BASE;
        if (name == "Router")
            return meshtastic_Config_DeviceConfig_Role_ROUTER;
        if (name == "Router Client")
            return meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT;
        if (name == "Router Late")
            return meshtastic_Config_DeviceConfig_Role_ROUTER_LATE;
        if (name == "Repeater")
            return meshtastic_Config_DeviceConfig_Role_REPEATER;
        if (name == "Tracker")
            return meshtastic_Config_DeviceConfig_Role_TRACKER;
        if (name == "Sensor")
            return meshtastic_Config_DeviceConfig_Role_SENSOR;
        if (name == "TAK")
            return meshtastic_Config_DeviceConfig_Role_TAK;
        if (name == "TAK Tracker")
            return meshtastic_Config_DeviceConfig_Role_TAK_TRACKER;
        if (name == "Lost&Found")
            return meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND;
        return meshtastic_Config_DeviceConfig_Role_CLIENT;
    }

    static meshtastic_Config_DeviceConfig_RebroadcastMode rebroadcastModeFromName(const std::string& name)
    {
        if (name == "All skip decode")
            return meshtastic_Config_DeviceConfig_RebroadcastMode_ALL_SKIP_DECODING;
        if (name == "Local only")
            return meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY;
        if (name == "Known only")
            return meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY;
        if (name == "None")
            return meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
        return meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
    }

    static meshtastic_Config_LoRaConfig_ModemPreset modemPresetFromName(const std::string& name)
    {
        if (name == "LongSlow")
            return meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
        if (name == "VeryLongSlow")
            return meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW;
        if (name == "MediumSlow")
            return meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW;
        if (name == "MediumFast")
            return meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
        if (name == "ShortSlow")
            return meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW;
        if (name == "ShortFast")
            return meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST;
        if (name == "LongModerate")
            return meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE;
        if (name == "ShortTurbo")
            return meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
        if (name == "LongTurbo")
            return meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO;
        return meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    }

    static uint32_t parseIntervalToMs(const std::string& val)
    {
        if (val == "off" || val.empty())
            return 0;
        if (val.back() == 'm')
            return std::stoul(val.substr(0, val.size() - 1)) * 60000;
        if (val.back() == 'h')
            return std::stoul(val.substr(0, val.size() - 1)) * 3600000;
        return 0;
    }

    void MeshService::loadConfigFromSettings(MeshConfig& config)
    {
        SETTINGS::Settings* _settings = _hal->settings();
        // Node info
        std::string short_name = _settings->getString("nodeinfo", "short_name");
        std::string long_name = _settings->getString("nodeinfo", "long_name");
        if (!short_name.empty())
        {
            strncpy(config.short_name, short_name.c_str(), 4);
            config.short_name[4] = '\0';
        }
        if (!long_name.empty())
        {
            strncpy(config.long_name, long_name.c_str(), sizeof(config.long_name) - 1);
            config.long_name[sizeof(config.long_name) - 1] = '\0';
        }
        config.is_unmessagable = _settings->getBool("nodeinfo", "unmessagable");
        config.role = roleFromName(_settings->getString("nodeinfo", "role"));
        config.rebroadcast_mode = rebroadcastModeFromName(_settings->getString("nodeinfo", "rebroadcast"));

        // LoRa
        config.lora_config.region = Mesh::MeshService::regionCodeFromName(_settings->getString("lora", "region"));
        const std::string modem_preset_name = _settings->getString("lora", "modem_preset");
        if (modem_preset_name == "Custom")
        {
            // Custom preset: drive the radio from the manual bandwidth/coding rate/spreading factor settings
            config.lora_config.use_preset = false;
            config.lora_config.bandwidth = _settings->getNumber("lora", "bandwidth");
            config.lora_config.coding_rate = _settings->getNumber("lora", "coding_rate");
            config.lora_config.spread_factor = _settings->getNumber("lora", "spread_factor");
        }
        else
        {
            config.lora_config.modem_preset = modemPresetFromName(modem_preset_name);
            config.lora_config.use_preset = true;
        }
        config.lora_config.tx_power = _settings->getNumber("lora", "tx_power");
        config.lora_config.override_duty_cycle = _settings->getBool("lora", "duty_ovr");
        config.lora_config.sx126x_rx_boosted_gain = _settings->getBool("lora", "rx_boost");
        config.lora_config.hop_limit = _settings->getNumber("lora", "hop_limit");
        config.lora_config.channel_num = _settings->getNumber("lora", "freq_slot");
        int32_t freq_ovr_khz = _settings->getNumber("lora", "freq_ovr");
        config.lora_config.override_frequency = (freq_ovr_khz > 0) ? (float)freq_ovr_khz / 1000.0f : 0.0f;
        config.lora_config.ignore_mqtt = !_settings->getBool("lora", "mqtt_rx");
        config.lora_config.config_ok_to_mqtt = _settings->getBool("lora", "mqtt_tx");

        // Security keys (decode from base64, leave unchanged if missing/invalid)
        std::string priv_b64 = _settings->getString("security", "private_key");
        std::string pub_b64 = _settings->getString("security", "public_key");
        if (!priv_b64.empty() && !pub_b64.empty())
        {
            size_t priv_len = 0, pub_len = 0;
            mbedtls_base64_decode(config.private_key, 32, &priv_len, (const unsigned char*)priv_b64.c_str(), priv_b64.size());
            mbedtls_base64_decode(config.public_key, 32, &pub_len, (const unsigned char*)pub_b64.c_str(), pub_b64.size());
            config.public_key_len = (priv_len == 32 && pub_len == 32) ? 32 : 0;
        }
        // Generate keypair if settings had none or invalid
        if (config.public_key_len != 32)
        {
            if (generateKeypair(config.private_key, config.public_key))
            {
                config.public_key_len = 32;
                unsigned char b64_buf[48] = {};
                size_t b64_len = 0;
                if (mbedtls_base64_encode(b64_buf, sizeof(b64_buf), &b64_len, config.private_key, 32) == 0)
                    _settings->setString("security", "private_key", std::string((char*)b64_buf, b64_len));
                b64_len = 0;
                if (mbedtls_base64_encode(b64_buf, sizeof(b64_buf), &b64_len, config.public_key, 32) == 0)
                    _settings->setString("security", "public_key", std::string((char*)b64_buf, b64_len));
                ESP_LOGI(TAG, "Generated and saved new X25519 keypair");
            }
            else
            {
                config.public_key_len = 0;
                ESP_LOGE(TAG, "X25519 keygen failed (PKC DMs will not work)");
            }
        }
        else
        {
            ESP_LOGI(TAG,
                     "Loaded X25519 keypair from settings pub=%02x%02x%02x%02x...",
                     config.public_key[0],
                     config.public_key[1],
                     config.public_key[2],
                     config.public_key[3]);
        }

        // Position
        const std::string location = _settings->getString("position", "location");
        if (location == "fixed")
            config.position = MeshConfig::POSITION_FIXED;
        else if (location == "gps")
            config.position = MeshConfig::POSITION_GPS;
        else
            config.position = MeshConfig::POSITION_OFF;
        config.fixed_latitude = _settings->getNumber("position", "latitude");
        config.fixed_longitude = _settings->getNumber("position", "longitude");
        config.fixed_altitude = _settings->getNumber("position", "altitude");

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
        config.position_flags = pf;
        if (config.position == MeshConfig::POSITION_OFF)
        {
            ESP_LOGI(TAG, "Position broadcasting disabled (location=off)");
        }
        else if (config.position == MeshConfig::POSITION_FIXED)
        {
            ESP_LOGI(TAG,
                     "Using fixed position: lat=%ld lon=%ld alt=%ld",
                     (long)config.fixed_latitude,
                     (long)config.fixed_longitude,
                     (long)config.fixed_altitude);
        }

        // Device telemetry field flags
        config.telemetry_bat_level = _settings->getBool("devmetrics", "bat_level");
        config.telemetry_voltage = _settings->getBool("devmetrics", "voltage");
        config.telemetry_ch_util = _settings->getBool("devmetrics", "ch_util");
        config.telemetry_air_util = _settings->getBool("devmetrics", "air_util");
        config.telemetry_uptime = _settings->getBool("devmetrics", "uptime");

        // Neighbor info module
        config.neighborinfo_enabled = _settings->getBool("neighborinfo", "enabled");
        config.neighborinfo_broadcast_interval_ms = parseIntervalToMs(_settings->getString("neighborinfo", "bcast_int"));

        // Broadcast intervals
        config.nodeinfo_broadcast_interval_ms = parseIntervalToMs(_settings->getString("nodeinfo", "bcast_int"));
        config.position_broadcast_interval_ms = parseIntervalToMs(_settings->getString("position", "bcast_int"));
        config.telemetry_broadcast_interval_ms = parseIntervalToMs(_settings->getString("devmetrics", "bcast_int"));
    }

    void MeshService::applyOkToMqtt(meshtastic_Data& data) const
    {
        if (_config.lora_config.config_ok_to_mqtt)
        {
            data.has_bitfield = true;
            data.bitfield |= 1;
        }
    }

    void MeshService::forceNodeInfoBroadcast()
    {
        ESP_LOGI(TAG, "Forcing node info broadcast on next update cycle");
        _last_nodeinfo_broadcast_ms = 0;
        _force_nodeinfo_broadcast = true;
    }

} // namespace Mesh
