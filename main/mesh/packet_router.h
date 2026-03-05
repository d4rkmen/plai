/**
 * @file packet_router.h
 * @author d4rkmen
 * @brief Packet routing and queue management for Meshtastic mesh
 * @version 2.1
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef PACKET_ROUTER_H
#define PACKET_ROUTER_H

#include <stdint.h>
#include <stdbool.h>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "meshtastic/mesh.pb.h"

namespace Mesh
{

    constexpr size_t MAX_LORA_PAYLOAD = 255;

    constexpr size_t MESHTASTIC_HEADER_LENGTH = 16;
    constexpr uint8_t PACKET_FLAGS_HOP_LIMIT_MASK = 0x07;
    constexpr uint8_t PACKET_FLAGS_WANT_ACK_MASK = 0x08;
    constexpr uint8_t PACKET_FLAGS_VIA_MQTT_MASK = 0x10;
    constexpr uint8_t PACKET_FLAGS_HOP_START_MASK = 0xE0;
    constexpr uint8_t PACKET_FLAGS_HOP_START_SHIFT = 5;

    struct __attribute__((packed)) PacketHeader
    {
        uint32_t to;
        uint32_t from;
        uint32_t id;
        uint8_t flags;
        uint8_t channel;
        uint8_t next_hop;
        uint8_t relay_node;
    };

    static_assert(sizeof(PacketHeader) == MESHTASTIC_HEADER_LENGTH, "PacketHeader size must be 16 bytes");

    constexpr size_t TX_QUEUE_SIZE = 16;
    constexpr size_t RX_QUEUE_SIZE = 32;

    enum class PacketPriority
    {
        ACK = 0,
        ROUTING = 1,
        ADMIN = 2,
        RELIABLE = 3,
        DEFAULT = 4,
        BACKGROUND = 5
    };

    struct QueuedPacket
    {
        meshtastic_MeshPacket packet;
        uint32_t tx_time_ms;
        uint8_t retries_left;
        PacketPriority priority;
        uint8_t raw_data[MAX_LORA_PAYLOAD];
        uint8_t raw_len;
        uint8_t port_hint; // portnum for TX logging (raw packets lack decoded info)
    };

    using PacketReceivedCallback = std::function<void(const meshtastic_MeshPacket& packet, int16_t rssi, float snr)>;

    // --- Packet History (matches Meshtastic PacketHistory) ---

    constexpr uint8_t NUM_RELAYERS = 3;
    constexpr uint8_t NO_NEXT_HOP_PREFERENCE = 0;

    // hop_limit byte packing: [5:3] = ourTxHopLimit, [2:0] = highestHopLimit
    constexpr uint8_t HOP_LIMIT_HIGHEST_MASK = 0x07;
    constexpr uint8_t HOP_LIMIT_OUR_TX_MASK = 0x38;
    constexpr uint8_t HOP_LIMIT_OUR_TX_SHIFT = 3;

    struct PacketRecord
    {
        uint32_t id;
        uint32_t sender;
        uint8_t next_hop;
        uint8_t hop_limit; // packed: highestHopLimit | (ourTxHopLimit << 3)
        uint8_t relayed_by[NUM_RELAYERS];
        uint32_t rxTimeMsec;
    };

    constexpr size_t PACKET_HISTORY_SIZE = 100;

    class PacketRouter
    {
    public:
        PacketRouter();
        ~PacketRouter();

        bool init(uint32_t our_node_id = 0);

        void setNodeId(uint32_t node_id);

        // --- TX/RX queue interface ---

        bool enqueueTx(const meshtastic_MeshPacket& packet,
                       PacketPriority priority = PacketPriority::DEFAULT,
                       bool reliable = false,
                       uint8_t port_hint = 0);

        bool enqueueTxRaw(const uint8_t* data, uint8_t len, PacketPriority priority = PacketPriority::DEFAULT,
                          uint8_t port_hint = 0);

        bool dequeueTx(QueuedPacket& packet);
        bool hasTxPackets() const;
        size_t getTxQueueSize() const;

        bool enqueueRx(const uint8_t* data, uint8_t len, int16_t rssi, float snr);
        void processRxQueue();
        void setPacketReceivedCallback(PacketReceivedCallback callback);

        void clearQueues();
        uint32_t generatePacketId();

        // --- Packet History (Meshtastic-compatible) ---

        /**
         * @brief Check and record recently seen packet (matches Meshtastic PacketHistory::wasSeenRecently)
         * @param p            Packet to check
         * @param withUpdate   If true, insert/update the record in history
         * @param wasFallback  [out] Set true if fallback-to-flooding detected
         * @param weWereNextHop [out] Set true if we were the designated next hop
         * @param wasUpgraded  [out] Set true if hop_limit was upgraded
         * @return true if the packet was already in the history (duplicate)
         */
        bool wasSeenRecently(const meshtastic_MeshPacket* p, bool withUpdate = true,
                             bool* wasFallback = nullptr, bool* weWereNextHop = nullptr,
                             bool* wasUpgraded = nullptr);

        bool wasRelayer(uint8_t relayer, uint32_t id, uint32_t sender, bool* wasSole = nullptr);
        void removeRelayer(uint8_t relayer, uint32_t id, uint32_t sender);

    private:
        // FreeRTOS static queues (no heap allocation)
        QueueHandle_t _tx_queue;
        StaticQueue_t _tx_queue_cb;
        uint8_t _tx_queue_buf[TX_QUEUE_SIZE * sizeof(QueuedPacket)];

        QueueHandle_t _rx_queue;
        StaticQueue_t _rx_queue_cb;
        uint8_t _rx_queue_buf[RX_QUEUE_SIZE * sizeof(QueuedPacket)];

        PacketReceivedCallback _rx_callback;
        uint32_t _packet_id_counter;

        // Packet history (static flat array, Meshtastic style)
        PacketRecord _history[PACKET_HISTORY_SIZE];
        uint32_t _our_node_id;
        uint8_t _our_relay_id;

        PacketRecord* historyFind(uint32_t sender, uint32_t id);
        void historyInsert(const PacketRecord& r);

        static bool isRelayer(uint8_t relayer, const PacketRecord& r, bool* wasSole = nullptr);

        static uint8_t getHighestHopLimit(const PacketRecord& r);
        static void setHighestHopLimit(PacketRecord& r, uint8_t hopLimit);
        static uint8_t getOurTxHopLimit(const PacketRecord& r);
        static void setOurTxHopLimit(PacketRecord& r, uint8_t hopLimit);

        void markSentPacket(const meshtastic_MeshPacket& p);
    };

} // namespace Mesh

#endif // PACKET_ROUTER_H
