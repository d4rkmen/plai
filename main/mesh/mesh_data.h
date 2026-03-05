/**
 * @file mesh_data.h
 * @author d4rkmen
 * @brief Shared data structures for Meshtastic UI widgets with file-based storage
 * @version 2.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef MESH_DATA_H
#define MESH_DATA_H

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include "meshtastic/channel.pb.h"

namespace Mesh
{
    /**
     * @brief Maximum entries in packet log
     */
    /**
     * @brief Maximum messages to store per conversation
     */
    constexpr size_t MAX_MESSAGES_PER_CONV = 50;

    /**
     * @brief Storage paths for messages
     */
    constexpr const char* MESSAGES_DIR = "/sdcard/meshtastic/messages";
    constexpr const char* MSG_INDEX_FILE = "/sdcard/meshtastic/messages/index.idx";

    /**
     * @brief Storage path for traceroute results
     */
    constexpr const char* TRACEROUTE_DIR = "/sdcard/meshtastic/traceroute";

    /**
     * @brief Message file magic number
     */
    constexpr uint32_t MSG_FILE_MAGIC = 0x4753534D; // "MSSG" in little-endian
    constexpr uint32_t MSG_FILE_VERSION = 1;
    constexpr uint32_t MSG_INDEX_MAGIC = 0x5844494D; // "MIDX" in little-endian

    /**
     * @brief Traceroute file magic and limits
     */
    constexpr uint32_t TRC_FILE_MAGIC = 0x43525454;   // "TTRC"
    constexpr uint32_t TRC_FILE_VERSION = 2;
    constexpr size_t MAX_TRACEROUTES_PER_NODE = 50;
    constexpr size_t TRC_FILE_HEADER_SIZE = 12;        // magic(4) + version(4) + count(4)

    /**
     * @brief Fixed-size on-disk traceroute record (95 bytes)
     */
    struct __attribute__((packed)) TraceRouteRecord
    {
        uint32_t target_node_id;
        uint32_t timestamp;
        uint16_t duration_sec;   // Round-trip time in seconds (0 if pending/unknown)
        uint8_t status;          // 0=PENDING, 1=SUCCESS, 2=FAILED
        uint8_t route_to_count;
        uint8_t route_back_count;
        int8_t dest_snr_q4;     // SNR at destination * 4
        int8_t origin_snr_q4;   // SNR at origin (return) * 4
        struct __attribute__((packed))
        {
            uint32_t node_id;
            int8_t snr_q4;       // SNR * 4
        } route_to[8];
        struct __attribute__((packed))
        {
            uint32_t node_id;
            int8_t snr_q4;
        } route_back[8];
    };
    static_assert(sizeof(TraceRouteRecord) == 95, "TraceRouteRecord must be 95 bytes");

    /**
     * @brief Text message storage (used by both Nodes DM and Channels)
     */
    struct TextMessage
    {
        uint32_t id;        // Message ID
        uint32_t from;      // Sender node ID
        uint32_t to;        // Destination node ID (0 for broadcast/channel)
        uint32_t timestamp; // Unix timestamp
        uint8_t channel;    // Channel index for group messages
        bool is_direct;     // true = P2P DM, false = channel broadcast
        bool read;          // Read status
        std::string text;   // Message content

        // Message delivery status
        enum class Status
        {
            PENDING,
            SENT,
            ACK,
            NACK,
            DELIVERED,
            FAILED
        } status = Status::PENDING;
    };

    /**
     * @brief Single hop in a traceroute path
     */
    struct TraceRouteHop
    {
        uint32_t node_id; // Node that relayed
        float snr;        // SNR in dB at this hop
    };

    /**
     * @brief Result of a single traceroute attempt
     */
    struct TraceRouteResult
    {
        enum class Status
        {
            PENDING,
            SUCCESS,
            FAILED
        };

        uint32_t target_node_id;               // Destination node
        uint32_t timestamp;                     // Unix timestamp when sent
        uint16_t duration_sec;                  // Round-trip time in seconds (0 if pending/unknown)
        Status status;                          // Current state
        float dest_snr;                         // SNR at destination (last entry in snr_towards)
        float origin_snr;                       // SNR at origin on return (last entry in snr_back)
        std::vector<TraceRouteHop> route_to;    // Hops towards destination
        std::vector<TraceRouteHop> route_back;  // Hops on the way back
    };

    /**
     * @brief Packet log entry for radio monitoring (fixed-size, no heap)
     */
    struct PacketLogEntry
    {
        uint32_t timestamp_ms; // esp_timer millis when captured
        uint32_t from;         // Source node ID
        uint32_t to;           // Destination node ID
        uint32_t id;           // Packet ID
        uint16_t size;         // Raw radio payload size in bytes
        int16_t rssi;          // RSSI (RX only, 0 for TX)
        float snr;             // SNR in dB (RX only, 0 for TX)
        uint8_t port;          // Decoded portnum (0 if undecoded)
        uint8_t hop_limit;     // Current hop limit
        uint8_t hop_start;     // Original hop start
        uint8_t channel;       // Channel hash
        uint8_t rx_snr_raw;    // Raw SNR from radio * 4 (RX only)
        bool is_tx;            // true = TX, false = RX
        bool decoded;          // true if payload was successfully decoded
        bool want_ack;         // Want ACK flag
        bool crc_error;        // true if RX failed due to CRC (from/to may be partial)
    };

    constexpr size_t PACKET_LOG_SIZE = 50;

    /**
     * @brief Fixed-size ring buffer for packet log (no heap allocations)
     */
    template <typename T, size_t N>
    class RingBuffer
    {
    public:
        void push(const T& item)
        {
            _buf[_head] = item;
            _head = (_head + 1) % N;
            if (_count < N)
                _count++;
            _generation++;
        }
        void clear()
        {
            _head = 0;
            _count = 0;
            _generation++;
        }
        size_t size() const { return _count; }
        bool empty() const { return _count == 0; }
        uint32_t generation() const { return _generation; }

        const T& operator[](size_t i) const
        {
            size_t start = (_head + N - _count) % N;
            return _buf[(start + i) % N];
        }

    private:
        T _buf[N] = {};
        size_t _head = 0;
        size_t _count = 0;
        uint32_t _generation = 0;
    };

    /**
     * @brief Mesh network statistics
     */
    struct MeshStats
    {
        uint32_t uptime_ms = 0;
        uint32_t tx_packets = 0;
        uint32_t rx_packets = 0;
        uint32_t tx_errors = 0;
        uint32_t rx_errors = 0;
        uint32_t messages_sent = 0;
        uint32_t messages_received = 0;
        uint32_t packets_forwarded = 0;
        float duty_cycle_percent = 0.0f;
        uint32_t air_time_ms = 0;
        uint32_t ble_bytes_tx = 0;
        uint32_t ble_bytes_rx = 0;
        bool ble_connected = false;
    };

    /**
     * @brief Graph data point for time-series graphs
     */
    struct GraphPoint
    {
        uint32_t timestamp_ms;
        float value;
    };

    /**
     * @brief Channel info for UI display
     */
    struct ChannelInfo
    {
        meshtastic_Channel channel;
        uint32_t unread_count;
    };

    /**
     * @brief Lightweight message index entry (kept in RAM)
     * Tracks unread counts and message presence without loading full messages
     */
    struct MessageIndexEntry
    {
        uint32_t node_id;        // Node ID for DMs (0 for channels)
        uint8_t channel;         // Channel index (only used when node_id == 0)
        bool is_direct;          // true = DM, false = channel
        uint32_t message_count;  // Total messages in conversation
        uint32_t unread_count;   // Unread message count
        uint32_t last_timestamp; // Timestamp of last message
    };

    /**
     * @brief Mesh data store - singleton for UI access
     * Messages are stored in individual files per conversation
     */
    class MeshDataStore
    {
    public:
        static MeshDataStore& getInstance()
        {
            static MeshDataStore instance;
            return instance;
        }

        /**
         * @brief Initialize the message store (create directories, load index)
         * @return true on success
         */
        bool init();

        // Packet log (static ring buffer, newest at size()-1)
        void addPacketLogEntry(const PacketLogEntry& entry);
        const RingBuffer<PacketLogEntry, PACKET_LOG_SIZE>& getPacketLog() const { return _packet_log; }
        void clearPacketLog() { _packet_log.clear(); }

        /**
         * @brief Monotonic counter incremented on every message mutation.
         * UI can compare against a cached value to detect changes.
         */
        uint32_t getChangeCounter() const { return _change_counter; }

        // Messages - now file-backed
        void addMessage(const TextMessage& msg);
        std::vector<TextMessage> getDirectMessages(uint32_t node_id) const;
        std::vector<TextMessage> getChannelMessages(uint8_t channel) const;
        uint32_t getUnreadDMCount(uint32_t node_id) const;
        uint32_t getUnreadChannelCount(uint8_t channel) const;
        void markMessagesRead(uint32_t node_id, bool is_channel = false);
        void clearMessages();
        void clearConversation(uint32_t node_id, bool is_channel = false);

        /**
         * @brief Update the delivery status of a message by its packet ID.
         * Scans DM files to find the message and rewrites the file with updated status.
         * @param packet_id  The message ID (packet ID used during send)
         * @param new_status The new status to set
         * @return true if the message was found and updated
         */
        bool updateMessageStatus(uint32_t packet_id, TextMessage::Status new_status);

        /**
         * @brief Get total message count for a DM conversation without loading messages
         */
        uint32_t getDMMessageCount(uint32_t node_id) const;

        /**
         * @brief Load a single message by index from a DM conversation file
         * @param node_id  The conversation node ID
         * @param index    0-based message index (0 = oldest)
         * @param out      Output message
         * @return true if message was loaded successfully
         */
        bool getDMMessageByIndex(uint32_t node_id, uint32_t index, TextMessage& out) const;

        /**
         * @brief Load a range of messages from a DM conversation file
         * @param node_id  The conversation node ID
         * @param start    First message index (0-based)
         * @param count    Number of messages to load
         * @param out      Output vector (appended)
         * @return Number of messages actually loaded
         */
        uint32_t getDMMessageRange(uint32_t node_id, uint32_t start, uint32_t count, std::vector<TextMessage>& out) const;

        /**
         * @brief Get text lengths of all messages in a DM conversation (lightweight scan)
         * Used to compute total wrapped line count without loading full message text.
         * @param node_id  The conversation node ID
         * @param text_lengths  Output vector of text lengths
         * @return Total message count
         */
        uint32_t getDMTextLengths(uint32_t node_id, std::vector<uint16_t>& text_lengths) const;

        /**
         * @brief Iterate over all DM messages sequentially, loading one at a time.
         * Callback receives the message index and a reference to the message.
         * Message memory is reused between calls - do not store the reference.
         * @param node_id  The conversation node ID
         * @param callback Function called for each message (index, message). Return false to stop.
         * @return Number of messages iterated
         */
        uint32_t forEachDMMessage(uint32_t node_id,
                                   std::function<bool(uint32_t index, const TextMessage& msg)> callback) const;

        /**
         * @brief Get total message count for a channel conversation without loading messages
         */
        uint32_t getChannelMessageCount(uint8_t channel) const;

        /**
         * @brief Load a range of messages from a channel conversation file
         * @param channel  The channel index
         * @param start    First message index (0-based)
         * @param count    Number of messages to load
         * @param out      Output vector (appended)
         * @return Number of messages actually loaded
         */
        uint32_t
        getChannelMessageRange(uint8_t channel, uint32_t start, uint32_t count, std::vector<TextMessage>& out) const;

        /**
         * @brief Iterate over all channel messages sequentially, loading one at a time.
         * Callback receives the message index and a reference to the message.
         * Message memory is reused between calls - do not store the reference.
         * @param channel  The channel index
         * @param callback Function called for each message (index, message). Return false to stop.
         * @return Number of messages iterated
         */
        uint32_t forEachChannelMessage(uint8_t channel,
                                        std::function<bool(uint32_t index, const TextMessage& msg)> callback) const;

        /**
         * @brief Get list of conversations with unread messages
         * @return Vector of node IDs with unread DMs
         */
        std::vector<uint32_t> getConversationsWithUnread() const;

        /**
         * @brief Get total unread DM count across all conversations
         * @return Total unread count
         */
        uint32_t getTotalUnreadDMCount() const;

        /**
         * @brief Get total unread channel message count across all channels
         * @return Total unread count
         */
        uint32_t getTotalUnreadChannelCount() const;

        // Traceroute storage - file-backed, one file per target node
        /**
         * @brief Get total traceroute attempt count for a target node
         */
        uint32_t getTraceRouteCount(uint32_t node_id) const;

        /**
         * @brief Load a range of traceroute results (for rendering visible items only)
         * @param node_id  Target node
         * @param start    First record index (0-based, 0 = oldest)
         * @param count    Max records to load
         * @param out      Output vector (appended)
         * @return Number of records actually loaded
         */
        uint32_t getTraceRouteRange(uint32_t node_id, uint32_t start, uint32_t count,
                                     std::vector<TraceRouteResult>& out) const;

        /**
         * @brief Load a single traceroute result by index
         */
        bool getTraceRouteByIndex(uint32_t node_id, uint32_t index, TraceRouteResult& out) const;

        /**
         * @brief Append a new traceroute result. Trims oldest if over limit.
         * @return Index of the appended record
         */
        uint32_t addTraceRoute(uint32_t node_id, const TraceRouteResult& result);

        /**
         * @brief Update an existing traceroute record in-place (e.g. PENDING -> SUCCESS)
         */
        bool updateTraceRoute(uint32_t node_id, uint32_t index, const TraceRouteResult& result);

        /**
         * @brief Remove all stored data files for a node (DMs + traceroutes)
         */
        void removeNodeData(uint32_t node_id);

        // Statistics
        MeshStats& getStats() { return _stats; }
        const MeshStats& getStats() const { return _stats; }
        void resetStats();

        // Graph data
        void addBatteryPoint(float voltage);
        void addChannelActivityPoint(float packets_per_min);
        void addRssiPoint(uint32_t node_id, int16_t rssi);
        const std::vector<GraphPoint>& getBatteryHistory() const { return _battery_history; }
        const std::vector<GraphPoint>& getChannelActivityHistory() const { return _channel_activity; }
        std::vector<GraphPoint> getRssiHistory(uint32_t node_id) const;

    private:
        MeshDataStore() : _initialized(false), _change_counter(0) {}
        ~MeshDataStore() = default;
        MeshDataStore(const MeshDataStore&) = delete;
        MeshDataStore& operator=(const MeshDataStore&) = delete;

        // Directory and file management
        bool createDirectories();
        std::string getDMFilePath(uint32_t node_id) const;
        std::string getChannelFilePath(uint8_t channel) const;
        std::string getTraceRouteFilePath(uint32_t node_id) const;

        // Traceroute file helpers
        static void trResultToRecord(const TraceRouteResult& src, TraceRouteRecord& dst);
        static void trRecordToResult(const TraceRouteRecord& src, TraceRouteResult& dst);

        // File I/O for messages
        bool loadMessagesFromFile(const std::string& path, std::vector<TextMessage>& out) const;
        bool saveMessagesToFile(const std::string& path, const std::vector<TextMessage>& messages);
        bool appendMessageToFile(const std::string& path, const TextMessage& msg);

        // Index management
        bool loadIndex();
        bool saveIndex();
        bool rebuildIndex();
        MessageIndexEntry* findIndexEntry(uint32_t node_id, bool is_direct);
        const MessageIndexEntry* findIndexEntry(uint32_t node_id, bool is_direct) const;
        void updateIndexEntry(
            uint32_t node_id, bool is_direct, uint8_t channel, int count_delta, int unread_delta, uint32_t timestamp);

        // Lightweight index kept in RAM
        std::vector<MessageIndexEntry> _message_index;
        bool _initialized;
        uint32_t _change_counter;

        // Other data (kept in RAM as before)
        RingBuffer<PacketLogEntry, PACKET_LOG_SIZE> _packet_log;
        MeshStats _stats;
        std::vector<GraphPoint> _battery_history;
        std::vector<GraphPoint> _channel_activity;
        std::map<uint32_t, std::vector<GraphPoint>> _rssi_history;

        static constexpr size_t MAX_GRAPH_POINTS = 60; // 1 hour at 1 point/min
    };

} // namespace Mesh

#endif // MESH_DATA_H
