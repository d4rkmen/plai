/**
 * @file mesh_service.h
 * @author d4rkmen
 * @brief Meshtastic protocol service for BLE and Radio coordination
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef MESH_SERVICE_H
#define MESH_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <functional>
#include <map>
#include <string>
#include "hal/hal.h"
#include "hal/radio/radio_interface.h"
#include "hal/ble/ble_peripheral.h"
#include "packet_router.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/module_config.pb.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/telemetry.pb.h"

// Forward declarations
namespace HAL
{
    class GPS;
}

namespace Mesh
{
    class NodeDB;
}

namespace Mesh
{

    /**
     * @brief Region information for regulatory compliance
     */
    struct RegionInfo
    {
        meshtastic_Config_LoRaConfig_RegionCode code;
        float freq_start;         // Start frequency in MHz
        float freq_end;           // End frequency in MHz
        uint8_t duty_cycle;       // Duty cycle percentage (0-100)
        float spacing;            // Channel spacing in MHz
        uint8_t power_limit;      // Maximum power in dBm
        bool audio_permitted;     // Audio transmission permitted
        bool frequency_switching; // Frequency hopping required
        bool wide_lora;           // Wide LoRa mode (2.4GHz)
        const char* name;         // Region name
    };

    /**
     * @brief Mesh service state
     */
    enum class MeshState
    {
        UNINITIALIZED,
        STARTING,
        READY,
        ERROR
    };

    /**
     * @brief Message received callback
     */
    using MessageCallback = std::function<void(const meshtastic_MeshPacket& packet)>;

    /**
     * @brief Connection state callback
     */
    using ConnectionCallback = std::function<void(bool connected)>;

    /**
     * @brief Traceroute UI notification callback (called after result is stored)
     * @param target_node_id  The traceroute destination node
     * @param result_index    Index of the updated record in the file store
     */
    using TraceRouteCallback = std::function<void(uint32_t target_node_id, uint32_t result_index)>;

    /**
     * @brief Battery info callback (returns battery_level 0-100, 101=USB powered, voltage in mV)
     */
    struct BatteryInfo
    {
        uint32_t level; // 0-100 percent, 101 = USB powered
        float voltage;  // Battery voltage in volts
    };
    using BatteryCallback = std::function<BatteryInfo()>;

    /**
     * @brief Mesh service configuration
     */
    struct MeshConfig
    {
        uint32_t node_id;   // Our node ID (usually derived from MAC)
        char short_name[5]; // 4 char short name
        char long_name[40]; // Long name
        meshtastic_Config_LoRaConfig lora_config;
        meshtastic_Channel primary_channel;
        uint32_t ble_pin;                                                // BLE pairing PIN (0 = disabled)
        meshtastic_Config_DeviceConfig_Role role;                        // Device role (affects behavior)
        meshtastic_Config_DeviceConfig_RebroadcastMode rebroadcast_mode; // Rebroadcast mode

        bool is_unmessagable; // Node does not accept messages

        // X25519 keypair (32 bytes each)
        uint8_t public_key[32];
        uint8_t private_key[32];
        size_t public_key_len; // 0 = no key, 32 = valid

        // Position configuration
        enum PositionMode
        {
            POSITION_OFF,
            POSITION_FIXED,
            POSITION_GPS
        };
        PositionMode position = POSITION_OFF; // Position source: off, fixed coords, or live GPS
        int32_t fixed_latitude;   // Fixed latitude (degrees * 1e7)
        int32_t fixed_longitude; // Fixed longitude (degrees * 1e7)
        int32_t fixed_altitude;  // Fixed altitude MSL (meters)
        uint32_t position_flags; // Bitwise OR of meshtastic_Config_PositionConfig_PositionFlags

        // Broadcast intervals (milliseconds, 0 = disabled)
        uint32_t nodeinfo_broadcast_interval_ms;
        uint32_t position_broadcast_interval_ms;
        uint32_t telemetry_broadcast_interval_ms;

        // Device telemetry field flags
        bool telemetry_bat_level;
        bool telemetry_voltage;
        bool telemetry_ch_util;
        bool telemetry_air_util;
        bool telemetry_uptime;
    };
    /**
     * @brief Main mesh protocol service
     */
    class MeshService
    {
    public:
        MeshService(HAL::Hal* hal);
        ~MeshService();

        /**
         * @brief Look up a region enum code by name string (e.g. "EU_433")
         * @param name Region name matching RegionInfo::name
         * @return Region code, or UNSET if not found
         */
        static meshtastic_Config_LoRaConfig_RegionCode regionCodeFromName(const std::string& name);

        /**
         * @brief Generate a new X25519 keypair
         * @param out_private Output buffer for 32-byte raw private key
         * @param out_public Output buffer for 32-byte raw public key
         * @return true if successful
         */
        static bool generateKeypair(uint8_t* out_private, uint8_t* out_public);

        /**
         * @brief Initialize the mesh service
         * @param radio Radio interface
         * @param nodedb Node database
         * @param config Mesh configuration
         * @return true on success
         */
        bool init(HAL::RadioInterface* radio, NodeDB* nodedb, const MeshConfig& config);

        /**
         * @brief Start the mesh service (starts BLE, radio)
         * @return true on success
         */
        bool start();

        /**
         * @brief Stop the mesh service
         */
        void stop();

        /**
         * @brief Process mesh events (call from main loop)
         */
        void update();

        /**
         * @brief Get current state
         * @return Current mesh state
         */
        MeshState getState() const { return _state; }

        /**
         * @brief Send a text message
         * @param text Message text
         * @param dest Destination node ID (BROADCAST = 0xFFFFFFFF)
         * @param channel Channel index
         * @return Packet ID on success, 0 on failure
         */
        uint32_t sendText(const char* text, uint32_t dest = 0xFFFFFFFF, uint8_t channel = 0);

        /**
         * @brief Send a data packet
         * @param data Data bytes
         * @param len Data length
         * @param port_num Meshtastic port number
         * @param dest Destination node ID
         * @return true if queued
         */
        bool sendData(const uint8_t* data, size_t len, meshtastic_PortNum port_num, uint32_t dest = 0xFFFFFFFF);

        /**
         * @brief Set message received callback
         * @param callback Callback function
         */
        void setMessageCallback(MessageCallback callback);

        /**
         * @brief Set connection state callback
         * @param callback Callback function
         */
        void setConnectionCallback(ConnectionCallback callback);

        /**
         * @brief Set traceroute result callback
         * @param callback Called when a traceroute response is received
         */
        void setTraceRouteCallback(TraceRouteCallback callback) { _traceroute_callback = callback; }

        /**
         * @brief Set GPS driver pointer (for live GPS position)
         * @param gps Pointer to GPS driver (may be nullptr)
         */
        void setGps(HAL::GPS* gps) { _gps = gps; }

        /**
         * @brief Set battery info callback (for device telemetry)
         * @param callback Callback returning BatteryInfo
         */
        void setBatteryCallback(BatteryCallback callback) { _battery_callback = callback; }

        /**
         * @brief Get our node ID
         * @return Node ID
         */
        uint32_t getNodeId() const { return _config.node_id; }

        /**
         * @brief Get mesh configuration
         * @return Config reference
         */
        const MeshConfig& getConfig() const { return _config; }

        /**
         * @brief Update mesh configuration
         * @param config New configuration
         * @return true on success
         */
        bool setConfig(const MeshConfig& config);

        /**
         * @brief Get number of known nodes
         * @return Node count
         */
        size_t getNodeCount() const;

        /**
         * @brief Check if BLE client is connected
         * @return true if connected
         */
        bool isBleConnected() const;

        /**
         * @brief Get current radio frequency in MHz
         * @return Frequency in MHz (0 if not configured)
         */
        float getFrequency() const { return _saved_freq; }

        /**
         * @brief Get channel utilization percentage (TX + RX)
         */
        float getChannelUtilization() const { return _getChannelUtilization(); }

        /**
         * @brief Get air utilization TX-only percentage
         */
        float getAirUtilTx() const { return _getAirUtilTx(); }

        /**
         * @brief Get packet router
         * @return Router reference
         */
        PacketRouter& getRouter() { return _router; }

        /**
         * @brief Send a traceroute request to a node
         * @param dest Destination node ID
         * @param channel Channel index
         * @return true if queued
         */
        bool sendTraceRoute(uint32_t dest, uint8_t channel = 0);

        /**
         * @brief Load configuration from settings
         */
        void loadConfigFromSettings(MeshConfig& config);

        /**
         * @brief Force an immediate node info broadcast on the next update() cycle
         * Resets the broadcast timer so the interval check fires immediately
         */
        void forceNodeInfoBroadcast();

    private:
        HAL::Hal* _hal;
        // BLE callbacks
        static void onBleConnect(ble_client_t* client);
        static void onBleDisconnect(ble_client_t* client);
        static void onBleToRadio(ble_client_t* client, const uint8_t* data, uint16_t len);
        static uint16_t onBleFromRadio(ble_client_t* client, uint8_t* data, uint16_t max_len);

        // Radio callback
        void onRadioEvent(HAL::RadioEvent event);
        void onPacketReceived(const meshtastic_MeshPacket& packet, int16_t rssi, float snr);

        // Protocol handlers
        void handleToRadio(const meshtastic_ToRadio& toRadio);
        bool buildFromRadio(meshtastic_FromRadio& fromRadio);
        void handleMeshPacket(const meshtastic_MeshPacket& packet);
        void handleAdminMessage(const meshtastic_MeshPacket& packet);
        void handleNodeInfoPacket(const meshtastic_MeshPacket& packet);
        void handlePositionPacket(const meshtastic_MeshPacket& packet);
        void handleTelemetryPacket(const meshtastic_MeshPacket& packet);
        void handleTraceRoutePacket(const meshtastic_MeshPacket& packet, float snr);
        bool decodeMeshPacket(const meshtastic_MeshPacket& packet, meshtastic_MeshPacket& decoded) const;

        // ACK handling
        bool sendAck(uint32_t to, uint32_t packet_id, uint8_t channel, uint8_t hop_limit);
        uint8_t getHopLimitForResponse(uint8_t hop_start, uint8_t hop_limit) const;

        // Configuration helpers
        void initRegion();
        void applyModemConfig();
        uint32_t generateNodeId();

        // NodeInfo broadcast and reply
        void broadcastNodeInfo();
        void sendNodeInfo(uint32_t dest, uint8_t channel, bool want_response = false);

        // Position reply
        bool sendPosition(uint32_t dest, uint8_t channel);

        // Device telemetry broadcast
        bool sendDeviceTelemetry(uint32_t dest = 0xFFFFFFFF, uint8_t channel = 0);

        // New-node greeting
        void sendNewNodeGreeting(uint32_t node_id, uint8_t channel, uint8_t hops, int16_t rssi, float snr);

        // Region and modem state
        const RegionInfo* _my_region;
        float _bw;                   // Bandwidth in kHz
        uint8_t _sf;                 // Spreading factor
        uint8_t _cr;                 // Coding rate
        float _saved_freq;           // Saved frequency
        uint32_t _saved_channel_num; // Saved channel number

        // Member variables
        HAL::RadioInterface* _radio;
        HAL::GPS* _gps;
        NodeDB* _nodedb;
        PacketRouter _router;
        MeshConfig _config;
        MeshState _state;

        // Callbacks
        MessageCallback _message_callback;
        ConnectionCallback _connection_callback;
        BatteryCallback _battery_callback;
        TraceRouteCallback _traceroute_callback;

        // FromRadio state machine for BLE reads
        enum class FromRadioState
        {
            IDLE,
            SEND_MY_INFO,
            SEND_CONFIG,
            SEND_CHANNELS,
            SEND_NODES,
            SEND_COMPLETE
        };
        FromRadioState _fromradio_state;
        uint32_t _fromradio_config_id;
        size_t _fromradio_node_index;
        size_t _fromradio_channel_index;

        // Periodic node info broadcast
        uint32_t _last_nodeinfo_broadcast_ms;
        bool _force_nodeinfo_broadcast;

        // Periodic position broadcast
        uint32_t _last_position_broadcast_ms;

        // Periodic device telemetry broadcast
        uint32_t _last_telemetry_broadcast_ms;

        // TX watchdog (recovery if TX_DONE never arrives)
        bool _tx_in_progress;
        uint32_t _last_tx_start_ms;
        static constexpr uint32_t TX_WATCHDOG_TIMEOUT_MS = 4000;

        // Last received packet SNR (for traceroute)
        int16_t _last_rx_rssi;
        float _last_rx_snr;

        // Airtime tracking helpers
        uint32_t _estimateAirtimeMs(size_t payload_len) const;
        void _recordAirtime(uint32_t ms, bool is_tx);
        float _getChannelUtilization() const;
        float _getAirUtilTx() const;

        // Airtime tracking (sliding window for channel utilization)
        static constexpr uint32_t AIRTIME_WINDOW_MS = 3600000; // 1 hour
        uint32_t _airtime_window_start_ms;
        uint32_t _airtime_tx_ms;      // TX airtime in current window
        uint32_t _airtime_rx_ms;      // RX airtime in current window
        uint32_t _airtime_tx_ms_prev; // TX airtime in previous window
        uint32_t _airtime_rx_ms_prev; // RX airtime in previous window

        // Pending ACK tracking for message delivery status and retries
        static constexpr uint8_t MAX_TX_RETRIES = 3;
        struct PendingAck
        {
            uint32_t send_time_ms;              // When the (re)send was initiated
            uint8_t retries_left;               // Retries remaining (0 = final attempt)
            uint8_t raw_data[MAX_LORA_PAYLOAD]; // Raw on-air packet for retransmission
            uint8_t raw_len;                    // Length of raw_data
        };
        std::map<uint32_t, PendingAck> _pending_acks; // packet_id -> PendingAck
        void checkPendingAcks();
        static constexpr uint32_t ACK_TIMEOUT_MS = 30000; // 30 seconds
        static constexpr size_t MAX_PENDING_ACKS = 16;

        // Singleton instance for static callbacks
        static MeshService* _instance;
    };

} // namespace Mesh

#endif // MESH_SERVICE_H
