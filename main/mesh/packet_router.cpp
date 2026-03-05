/**
 * @file packet_router.cpp
 * @author d4rkmen
 * @brief Packet routing and queue management implementation
 * @version 2.1
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "packet_router.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>

static const char* TAG = "PACKET_ROUTER";

static inline uint32_t millis()
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

namespace Mesh
{

    // =========================================================================
    // Hop-limit helpers (packed byte: [5:3]=ourTxHopLimit, [2:0]=highestHopLimit)
    // =========================================================================

    uint8_t PacketRouter::getHighestHopLimit(const PacketRecord& r)
    {
        return r.hop_limit & HOP_LIMIT_HIGHEST_MASK;
    }

    void PacketRouter::setHighestHopLimit(PacketRecord& r, uint8_t hopLimit)
    {
        r.hop_limit = (r.hop_limit & ~HOP_LIMIT_HIGHEST_MASK) | (hopLimit & HOP_LIMIT_HIGHEST_MASK);
    }

    uint8_t PacketRouter::getOurTxHopLimit(const PacketRecord& r)
    {
        return (r.hop_limit & HOP_LIMIT_OUR_TX_MASK) >> HOP_LIMIT_OUR_TX_SHIFT;
    }

    void PacketRouter::setOurTxHopLimit(PacketRecord& r, uint8_t hopLimit)
    {
        r.hop_limit = (r.hop_limit & ~HOP_LIMIT_OUR_TX_MASK) | ((hopLimit << HOP_LIMIT_OUR_TX_SHIFT) & HOP_LIMIT_OUR_TX_MASK);
    }

    // =========================================================================
    // Relayer helpers
    // =========================================================================

    bool PacketRouter::isRelayer(uint8_t relayer, const PacketRecord& r, bool* wasSole)
    {
        bool found = false;
        bool other_present = false;

        for (uint8_t i = 0; i < NUM_RELAYERS; ++i)
        {
            if (r.relayed_by[i] == relayer)
                found = true;
            else if (r.relayed_by[i] != 0)
                other_present = true;
        }

        if (wasSole)
            *wasSole = (found && !other_present);

        return found;
    }

    // =========================================================================
    // Constructor / Destructor
    // =========================================================================

    PacketRouter::PacketRouter()
        : _tx_queue(nullptr), _tx_queue_cb{}, _tx_queue_buf{}, _rx_queue(nullptr), _rx_queue_cb{}, _rx_queue_buf{},
          _rx_callback(nullptr), _packet_id_counter(0), _history{}, _our_node_id(0), _our_relay_id(0)
    {
    }

    PacketRouter::~PacketRouter()
    {
        // Static queues don't need deletion — their storage is part of the object
    }

    // =========================================================================
    // Init
    // =========================================================================

    bool PacketRouter::init(uint32_t our_node_id)
    {
        ESP_LOGI(TAG, "Initializing packet router");

        _tx_queue = xQueueCreateStatic(TX_QUEUE_SIZE, sizeof(QueuedPacket), _tx_queue_buf, &_tx_queue_cb);
        _rx_queue = xQueueCreateStatic(RX_QUEUE_SIZE, sizeof(QueuedPacket), _rx_queue_buf, &_rx_queue_cb);

        if (!_tx_queue || !_rx_queue)
        {
            ESP_LOGE(TAG, "Failed to create static queues");
            return false;
        }

        _packet_id_counter = esp_random();

        memset(_history, 0, sizeof(_history));

        setNodeId(our_node_id);

        ESP_LOGI(TAG, "Packet router ready (TX=%u, RX=%u, history=%u, static=%u bytes)",
                 (unsigned)TX_QUEUE_SIZE, (unsigned)RX_QUEUE_SIZE, (unsigned)PACKET_HISTORY_SIZE,
                 (unsigned)sizeof(PacketRouter));
        return true;
    }

    void PacketRouter::setNodeId(uint32_t node_id)
    {
        _our_node_id = node_id;
        _our_relay_id = (uint8_t)(node_id & 0xFF);
    }

    // =========================================================================
    // Packet History: find / insert  (ported from Meshtastic PacketHistory)
    // =========================================================================

    PacketRecord* PacketRouter::historyFind(uint32_t sender, uint32_t id)
    {
        if (sender == 0 || id == 0)
            return nullptr;

        for (auto& slot : _history)
        {
            if (slot.id == id && slot.sender == sender)
                return &slot;
        }
        return nullptr;
    }

    void PacketRouter::historyInsert(const PacketRecord& r)
    {
        uint32_t now_ms = millis();
        uint32_t oldest_age = 0;
        PacketRecord* target = nullptr;

        for (auto& slot : _history)
        {
            if (slot.id == 0 && slot.sender == 0)
            {
                target = &slot;
                break;
            }
            else if (slot.id == r.id && slot.sender == r.sender)
            {
                target = &slot;
                break;
            }
            else
            {
                uint32_t age = now_ms - slot.rxTimeMsec;
                if (age > oldest_age)
                {
                    oldest_age = age;
                    target = &slot;
                }
            }
        }

        if (!target)
        {
            ESP_LOGE(TAG, "Packet history: no slot available");
            return;
        }

        if (r.rxTimeMsec == 0)
            return;

        *target = r;
    }

    // =========================================================================
    // wasSeenRecently  (ported from Meshtastic PacketHistory::wasSeenRecently)
    // =========================================================================

    bool PacketRouter::wasSeenRecently(const meshtastic_MeshPacket* p, bool withUpdate,
                                       bool* wasFallback, bool* weWereNextHop,
                                       bool* wasUpgraded)
    {
        if (p->id == 0)
            return false;

        PacketRecord r;
        memset(&r, 0, sizeof(PacketRecord));

        uint32_t sender = p->from ? p->from : _our_node_id;

        r.id = p->id;
        r.sender = sender;
        r.next_hop = p->next_hop;
        setHighestHopLimit(r, p->hop_limit);

        bool weWillRelay = false;
        if (p->relay_node == _our_relay_id)
        {
            weWillRelay = true;
            setOurTxHopLimit(r, p->hop_limit);
            r.relayed_by[0] = p->relay_node;
        }

        r.rxTimeMsec = millis();
        if (r.rxTimeMsec == 0)
            r.rxTimeMsec = 1;

        PacketRecord* found = historyFind(r.sender, r.id);
        bool seenRecently = (found != nullptr);

        // Check for hop_limit upgrade
        if (seenRecently && wasUpgraded && getHighestHopLimit(*found) < p->hop_limit)
        {
            ESP_LOGD(TAG, "Hop limit upgrade: packet 0x%08lX from %u to %u",
                     (unsigned long)p->id, getHighestHopLimit(*found), p->hop_limit);
            *wasUpgraded = true;
        }
        else if (wasUpgraded)
        {
            *wasUpgraded = false;
        }

        if (seenRecently)
        {
            // Fallback-to-flooding detection
            if (wasFallback)
            {
                if (found->sender != _our_node_id &&
                    found->next_hop != NO_NEXT_HOP_PREFERENCE &&
                    found->next_hop != _our_relay_id &&
                    p->next_hop == NO_NEXT_HOP_PREFERENCE &&
                    isRelayer(p->relay_node, *found) &&
                    !isRelayer(_our_relay_id, *found) &&
                    !isRelayer(found->next_hop, *found))
                {
                    *wasFallback = true;
                }
            }

            // Were we the designated next hop?
            if (weWereNextHop)
                *weWereNextHop = (found->next_hop == _our_relay_id);
        }

        if (withUpdate)
        {
            if (found != nullptr)
            {
                uint8_t startIdx = weWillRelay ? 1 : 0;
                if (!weWillRelay)
                {
                    bool weWereRelayer = isRelayer(_our_relay_id, *found);
                    if (weWereRelayer &&
                        (p->hop_limit == getOurTxHopLimit(*found) ||
                         p->hop_limit == getOurTxHopLimit(*found) - 1))
                    {
                        r.relayed_by[0] = p->relay_node;
                        startIdx = 1;
                    }
                    setOurTxHopLimit(r, getOurTxHopLimit(*found));
                }

                // Preserve highest hop_limit ever seen
                if (getHighestHopLimit(*found) > getHighestHopLimit(r))
                    setHighestHopLimit(r, getHighestHopLimit(*found));

                // Merge relayed_by, avoiding duplicates
                for (uint8_t i = 0; i < (NUM_RELAYERS - startIdx); i++)
                {
                    if (found->relayed_by[i] == 0)
                        continue;

                    bool exists = false;
                    for (uint8_t j = 0; j < NUM_RELAYERS; j++)
                    {
                        if (r.relayed_by[j] == found->relayed_by[i])
                        {
                            exists = true;
                            break;
                        }
                    }

                    if (!exists)
                        r.relayed_by[i + startIdx] = found->relayed_by[i];
                }

                r.next_hop = found->next_hop; // preserve original next_hop
            }
            historyInsert(r);
        }

        return seenRecently;
    }

    // =========================================================================
    // Public relayer helpers
    // =========================================================================

    bool PacketRouter::wasRelayer(uint8_t relayer, uint32_t id, uint32_t sender, bool* wasSole)
    {
        if (relayer == 0)
            return false;

        const PacketRecord* found = historyFind(sender, id);
        if (!found)
            return false;

        return isRelayer(relayer, *found, wasSole);
    }

    void PacketRouter::removeRelayer(uint8_t relayer, uint32_t id, uint32_t sender)
    {
        PacketRecord* found = historyFind(sender, id);
        if (!found)
            return;

        uint8_t j = 0;
        for (uint8_t i = 0; i < NUM_RELAYERS; i++)
        {
            if (found->relayed_by[i] != relayer)
            {
                found->relayed_by[j] = found->relayed_by[i];
                j++;
            }
            else
            {
                found->relayed_by[i] = 0;
            }
        }
        for (; j < NUM_RELAYERS; j++)
            found->relayed_by[j] = 0;
    }

    // =========================================================================
    // Mark sent packet as seen (Meshtastic FloodingRouter::send pattern)
    // =========================================================================

    void PacketRouter::markSentPacket(const meshtastic_MeshPacket& p)
    {
        meshtastic_MeshPacket copy = p;
        if (copy.relay_node == 0)
            copy.relay_node = _our_relay_id;
        wasSeenRecently(&copy);
    }

    // =========================================================================
    // TX queue (FreeRTOS static queue — built-in thread safety)
    // =========================================================================

    bool PacketRouter::enqueueTx(const meshtastic_MeshPacket& packet, PacketPriority priority, bool reliable,
                                 uint8_t port_hint)
    {
        // Mark as seen before sending (Meshtastic FloodingRouter::send)
        markSentPacket(packet);

        QueuedPacket qp = {};
        qp.packet = packet;
        qp.priority = priority;
        qp.port_hint = port_hint;
        qp.retries_left = reliable ? 3 : 0;
        qp.tx_time_ms = millis();

        // Build on-air format: PacketHeader + encrypted payload
        PacketHeader header = {};
        header.to = packet.to;
        header.from = packet.from;
        header.id = packet.id;
        header.channel = (uint8_t)packet.channel;
        header.next_hop = (uint8_t)packet.next_hop;
        header.relay_node = packet.relay_node;
        header.flags = (packet.hop_limit & PACKET_FLAGS_HOP_LIMIT_MASK) |
                       ((packet.hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK) |
                       (packet.want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0) |
                       (packet.via_mqtt ? PACKET_FLAGS_VIA_MQTT_MASK : 0);

        memcpy(qp.raw_data, &header, sizeof(header));

        if (packet.which_payload_variant == meshtastic_MeshPacket_encrypted_tag && packet.encrypted.size > 0)
        {
            if (sizeof(header) + packet.encrypted.size > sizeof(qp.raw_data))
            {
                ESP_LOGE(TAG, "Packet payload too large (%u bytes)", (unsigned)packet.encrypted.size);
                return false;
            }
            memcpy(qp.raw_data + sizeof(header), packet.encrypted.bytes, packet.encrypted.size);
            qp.raw_len = sizeof(header) + packet.encrypted.size;
        }
        else
        {
            qp.raw_len = sizeof(header);
        }

        if (xQueueSend(_tx_queue, &qp, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGW(TAG, "TX queue full");
            return false;
        }

        ESP_LOGD(TAG, "Enqueued TX packet, queue size: %u", (unsigned)uxQueueMessagesWaiting(_tx_queue));
        return true;
    }

    bool PacketRouter::enqueueTxRaw(const uint8_t* data, uint8_t len, PacketPriority priority, uint8_t port_hint)
    {
        if (!data || len == 0 || len > MAX_LORA_PAYLOAD)
            return false;

        // Extract header to mark packet as seen before sending
        if (len >= MESHTASTIC_HEADER_LENGTH)
        {
            PacketHeader header = {};
            memcpy(&header, data, sizeof(header));

            meshtastic_MeshPacket p = meshtastic_MeshPacket_init_default;
            p.from = header.from;
            p.id = header.id;
            p.relay_node = header.relay_node;
            p.hop_limit = header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
            p.hop_start = (header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
            p.next_hop = header.next_hop;
            markSentPacket(p);
        }

        QueuedPacket qp = {};
        memcpy(qp.raw_data, data, len);
        qp.raw_len = len;
        qp.priority = priority;
        qp.port_hint = port_hint;
        qp.retries_left = 0;
        qp.tx_time_ms = millis();

        return xQueueSend(_tx_queue, &qp, pdMS_TO_TICKS(100)) == pdTRUE;
    }

    bool PacketRouter::dequeueTx(QueuedPacket& packet)
    {
        return xQueueReceive(_tx_queue, &packet, 0) == pdTRUE;
    }

    bool PacketRouter::hasTxPackets() const
    {
        return uxQueueMessagesWaiting(_tx_queue) > 0;
    }

    size_t PacketRouter::getTxQueueSize() const
    {
        return (size_t)uxQueueMessagesWaiting(_tx_queue);
    }

    // =========================================================================
    // RX queue (FreeRTOS static queue — built-in thread safety)
    // =========================================================================

    bool PacketRouter::enqueueRx(const uint8_t* data, uint8_t len, int16_t rssi, float snr)
    {
        if (!data || len == 0)
            return false;

        if (len < MESHTASTIC_HEADER_LENGTH)
        {
            ESP_LOGW(TAG, "RX packet too short (%u bytes)", len);
            return false;
        }

        QueuedPacket qp = {};
        memcpy(qp.raw_data, data, len);
        qp.raw_len = len;
        qp.tx_time_ms = millis();

        PacketHeader header = {};
        memcpy(&header, data, sizeof(header));

        qp.packet = meshtastic_MeshPacket_init_default;
        qp.packet.from = header.from;
        qp.packet.to = header.to;
        qp.packet.id = header.id;
        qp.packet.channel = header.channel;
        qp.packet.next_hop = header.next_hop;
        qp.packet.relay_node = header.relay_node;
        qp.packet.hop_limit = header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
        qp.packet.want_ack = (header.flags & PACKET_FLAGS_WANT_ACK_MASK) != 0;
        qp.packet.via_mqtt = (header.flags & PACKET_FLAGS_VIA_MQTT_MASK) != 0;
        qp.packet.hop_start = (header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;

        const uint8_t* payload = data + MESHTASTIC_HEADER_LENGTH;
        uint8_t payload_len = len - MESHTASTIC_HEADER_LENGTH;
        if (payload_len > sizeof(qp.packet.encrypted.bytes))
        {
            ESP_LOGW(TAG, "RX payload too large (%u bytes)", payload_len);
            payload_len = sizeof(qp.packet.encrypted.bytes);
        }

        qp.packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
        qp.packet.encrypted.size = payload_len;
        memcpy(qp.packet.encrypted.bytes, payload, payload_len);

        qp.packet.rx_rssi = rssi;
        qp.packet.rx_snr = snr;

        if (xQueueSend(_rx_queue, &qp, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGW(TAG, "RX queue full, dropping packet");
            return false;
        }

        ESP_LOGD(TAG, "Enqueued RX packet, RSSI=%d, SNR=%.1f, queue size: %u",
                 rssi, snr, (unsigned)uxQueueMessagesWaiting(_rx_queue));
        return true;
    }

    void PacketRouter::processRxQueue()
    {
        if (!_rx_callback)
            return;

        QueuedPacket qp;
        while (xQueueReceive(_rx_queue, &qp, 0) == pdTRUE)
        {
            bool wasUpgraded = false;
            bool seenRecently = wasSeenRecently(&qp.packet, true, nullptr, nullptr, &wasUpgraded);

            if (seenRecently)
            {
                // Own packet reflected by relay → pass through for implicit ACK
                if (qp.packet.from == _our_node_id)
                {
                    ESP_LOGD(TAG, "Own packet 0x%08lX reflected by relay 0x%02X",
                             (unsigned long)qp.packet.id, qp.packet.relay_node);
                    _rx_callback(qp.packet, qp.packet.rx_rssi, qp.packet.rx_snr);
                    continue;
                }

                ESP_LOGD(TAG, "Dropping duplicate packet 0x%08lX from 0x%08lX",
                         (unsigned long)qp.packet.id, (unsigned long)qp.packet.from);
                continue;
            }

            _rx_callback(qp.packet, qp.packet.rx_rssi, qp.packet.rx_snr);
        }
    }

    void PacketRouter::setPacketReceivedCallback(PacketReceivedCallback callback) { _rx_callback = callback; }

    // =========================================================================
    // Misc
    // =========================================================================

    void PacketRouter::clearQueues()
    {
        xQueueReset(_tx_queue);
        xQueueReset(_rx_queue);
        memset(_history, 0, sizeof(_history));
    }

    uint32_t PacketRouter::generatePacketId() { return ++_packet_id_counter; }

} // namespace Mesh
