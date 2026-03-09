/**
 * @file node_db.h
 * @author d4rkmen
 * @brief Node database with SD card persistence and lazy loading
 * @version 2.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef NODE_DB_H
#define NODE_DB_H

#include <stdint.h>
#include <stdbool.h>
#include <vector>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "meshtastic/mesh.pb.h"
#include "meshtastic/deviceonly.pb.h"
#include "meshtastic/localonly.pb.h"

namespace Mesh
{

    /**
     * @brief Maximum number of nodes to store
     */
    constexpr size_t MAX_NODES = 1000;

    /**
     * @brief Storage paths on SD card
     */
    constexpr const char* MESH_DIR = "/sdcard/meshtastic";
    constexpr const char* NODES_DIR = "/sdcard/meshtastic/nodes";
    constexpr const char* NODEDB_FILE = "/sdcard/meshtastic/nodedb.pb"; // Legacy file for migration
    constexpr const char* MANIFEST_FILE = "/sdcard/meshtastic/nodes/manifest.idx";
    constexpr const char* PREFS_FILE = "/sdcard/meshtastic/prefs.pb";
    constexpr const char* CHANNELS_FILE = "/sdcard/meshtastic/channels.pb";
    constexpr const char* GREETINGS_FILE = "/sdcard/meshtastic/greetings.dat";

    static constexpr size_t GREETING_MAX_LEN = 200;

    struct ChannelGreeting
    {
        char channel_text[GREETING_MAX_LEN]; // broadcast to channel, empty = disabled
        char dm_text[GREETING_MAX_LEN];      // DM to new node, empty = disabled
        char ping_text[GREETING_MAX_LEN];    // auto-reply when #ping seen on channel, empty = disabled
    };

    /**
     * @brief Manifest file magic number
     */
    constexpr uint32_t MANIFEST_MAGIC = 0x5844494E; // "NIDX" in little-endian
    constexpr uint32_t MANIFEST_VERSION = 2;

    /**
     * @brief Sort order for node listing
     */
    enum class SortOrder
    {
        NONE,           // No sorting (insertion order)
        SHORT_NAME,     // Alphabetical by short name
        LONG_NAME,      // Alphabetical by long name
        ROLE,           // By device role
        SIGNAL,         // Best signal first (RSSI)
        HOPS_AWAY,      // Fewest hops first
        LAST_HEARD,     // Most recently heard first (default)
        FAVORITES_FIRST // Favorites at top, then by last heard
    };

    /**
     * @brief Node information with metadata (full data, loaded on demand)
     */
    struct NodeInfo
    {
        meshtastic_NodeInfo info; // Contains snr, last_heard, is_favorite, etc.
        int16_t last_rssi;        // Last RSSI value (not in protobuf)
        uint8_t relay_node;       // Last relay node ID (low byte from packet header)
    };

    /**
     * @brief Lightweight index entry for sorting (kept in RAM)
     * ~20 bytes per node instead of ~331 bytes
     */
    struct NodeIndexEntry
    {
        uint32_t node_id;    // 4 bytes - Node identifier
        uint32_t last_heard; // 4 bytes - Unix epoch seconds (or uptime seconds pre-sync)
        int16_t last_rssi;   // 2 bytes - For display without full load
        bool is_favorite;    // 1 byte  - For sorting favorites first
        bool exists;         // 1 byte  - File exists on disk
        char short_name[5];  // 5 bytes - Short name (4 chars + null)
        char long_name[9];   // 9 bytes - Long name prefix (8 chars + null)
        uint8_t role;        // 1 byte  - Device role enum
        uint8_t hops_away;   // 1 byte  - Hops away from us
        float snr;           // 4 bytes  - Last SNR value
    };

    /**
     * @brief Node database for storing mesh network state
     * Uses individual files per node with a lightweight index for memory efficiency
     */
    class NodeDB
    {
    public:
        NodeDB();
        ~NodeDB();

        /**
         * @brief Initialize the node database
         * @param our_node_id Our node ID
         * @return true on success
         */
        bool init(uint32_t our_node_id);

        /**
         * @brief Load database from SD card
         * @return true if loaded successfully
         */
        bool load();

        /**
         * @brief Save database to SD card (saves index/manifest)
         * @return true if saved successfully
         */
        bool save();

        /**
         * @brief Get a display label for a node: short_name if set, otherwise lower-16-bit hex,
         *        truncated to 4 UTF-8 characters.
         * @param node Node to derive the label from
         * @return Display label string
         */
        static std::string getLabel(const NodeInfo& node);

        /**
         * @brief Get a display label for a node: long_name if set, otherwise short_name if set, otherwise lower-16-bit hex,
         *        truncated to 4 UTF-8 characters.
         * @param node Node to derive the label from
         * @return Display label string
         */
        static std::string getLongLabel(const NodeInfo& node);

        /**
         * @brief Human-readable name for a device role enum value.
         * @param role Role enum from meshtastic_Config_DeviceConfig_Role
         * @return Short display string (e.g. "Router", "Tracker")
         */
        static const char* getRoleName(meshtastic_Config_DeviceConfig_Role role);

        /**
         * @brief Get number of nodes in database
         * @return Node count
         */
        size_t getNodeCount() const;

        /**
         * @brief Get node by index (sorted order)
         * @param index Node index in sorted order
         * @param out Output NodeInfo to fill
         * @return true if found and loaded
         */
        bool getNodeByIndex(size_t index, NodeInfo& out) const;

        /**
         * @brief Get nodes in a range for UI display
         * @param offset Starting index in sorted order
         * @param count Number of nodes to retrieve
         * @param out Output vector to fill with nodes
         * @param order Sort order to use
         * @return Number of nodes actually retrieved
         */
        size_t
        getNodesInRange(size_t offset, size_t count, std::vector<NodeInfo>& out, SortOrder order = SortOrder::LAST_HEARD) const;

        /**
         * @brief Get node by node ID (loads from file)
         * @param node_id Node ID
         * @param out Output NodeInfo to fill
         * @return true if found and loaded
         */
        bool getNode(uint32_t node_id, NodeInfo& out) const;

        /**
         * @brief Get index entry by node ID (lightweight, from RAM)
         * @param node_id Node ID
         * @return Pointer to index entry or nullptr
         */
        const NodeIndexEntry* getNodeIndex(uint32_t node_id) const;

        /**
         * @brief Find a node ID by its low byte (relay_node field)
         * @param relay_byte Low byte to match against (node_id & 0xFF)
         * @return Full node_id of the first match, or 0 if not found
         */
        uint32_t findNodeByRelayByte(uint8_t relay_byte) const;

        /**
         * @brief Find the sorted index for a given node ID
         * @param node_id Node ID to find
         * @param order Sort order to use (ensures index is sorted with this order)
         * @return Sorted index, or -1 if not found
         */
        int getSortedIndexForNode(uint32_t node_id, SortOrder order) const;

        /**
         * @brief Update or add a node
         * @param node Node info to update
         * @param rssi RSSI value (-1 to keep existing)
         * @param snr SNR value
         * @return true if updated/added
         */
        bool updateNode(const meshtastic_NodeInfo& node, int16_t rssi = -1, float snr = 0.0f, uint8_t relay_node = 0);

        /**
         * @brief Remove a node from database
         * @param node_id Node ID to remove
         * @return true if removed
         */
        bool removeNode(uint32_t node_id);

        /**
         * @brief Clear all nodes except our own
         */
        void clearNodes();

        /**
         * @brief Update position for a node
         * @param node_id Node ID
         * @param position Position data
         * @return true if updated
         */
        bool updatePosition(uint32_t node_id, const meshtastic_Position& position);

        /**
         * @brief Update user info for a node
         * @param node_id Node ID
         * @param user User data
         * @return true if updated
         */
        bool updateUser(uint32_t node_id, const meshtastic_User& user);

        /**
         * @brief Set favorite status for a node
         * @param node_id Node ID
         * @param favorite Favorite status
         * @return true if updated
         */
        bool setFavorite(uint32_t node_id, bool favorite);

        /**
         * @brief Get local preferences
         * @return Local config reference
         */
        meshtastic_LocalConfig& getLocalConfig() { return _local_config; }

        /**
         * @brief Get local module config
         * @return Local module config reference
         */
        meshtastic_LocalModuleConfig& getLocalModuleConfig() { return _local_module_config; }

        /**
         * @brief Get channel by index
         * @param index Channel index
         * @return Channel pointer or nullptr
         */
        meshtastic_Channel* getChannel(uint8_t index);

        /**
         * @brief Set channel configuration
         * @param index Channel index
         * @param channel Channel data
         * @return true if set successfully
         */
        bool setChannel(uint8_t index, const meshtastic_Channel& channel);

        /**
         * @brief Delete channel by resetting it to disabled default
         * @param index Channel index
         * @return true if deleted successfully
         */
        bool deleteChannel(uint8_t index);

        /**
         * @brief Mark database as dirty (needs save)
         */
        void markDirty()
        {
            _dirty = true;
            _change_counter++;
        }

        /**
         * @brief Check if database needs saving
         * @return true if dirty
         */
        bool isDirty() const { return _dirty; }

        /**
         * @brief Monotonic counter incremented on every mutation.
         * UI can compare against a cached value to detect changes.
         */
        uint32_t getChangeCounter() const { return _change_counter; }

        /**
         * @brief Periodic save if dirty (call from main loop)
         */
        void checkSave();

        /**
         * @brief Sort the index by specified order
         * @param order Sort order to apply
         */
        void sortIndex(SortOrder order = SortOrder::LAST_HEARD);
        /**
         * @brief
         *
         * @return true if saved successfully
         */
        bool saveChannels();

        /**
         * @brief Load channels from storage
         * @return true if loaded successfully
         */
        bool loadChannels();

        const ChannelGreeting& getGreeting(uint8_t index) const;
        void setGreeting(uint8_t index, const ChannelGreeting& greeting);
        bool saveGreetings();
        bool loadGreetings();

    private:
        // Directory management
        bool createDirectories();

        // Individual node file operations
        bool loadNodeFromFile(uint32_t node_id, NodeInfo& out) const;
        bool saveNodeToFile(const NodeInfo& node);
        bool deleteNodeFile(uint32_t node_id);
        std::string getNodeFilePath(uint32_t node_id) const;

        // Index management
        bool loadIndex();
        bool saveIndex();
        bool rebuildIndex();

        // Legacy migration
        bool migrateFromLegacy();
        bool loadLegacyNodeDb(std::vector<NodeInfo>& nodes);

        // Preferences and channels (unchanged)
        bool savePrefs();
        bool loadPrefs();

        // Index entry management
        NodeIndexEntry* findIndexEntry(uint32_t node_id);
        const NodeIndexEntry* findIndexEntry(uint32_t node_id) const;
        void fillIndexEntryFromNode(NodeIndexEntry& entry, const NodeInfo& node);
        void updateIndexEntry(const NodeInfo& node);
        void removeIndexEntry(uint32_t node_id);

        // Lightweight index kept in RAM (~12 bytes per node)
        std::vector<NodeIndexEntry> _index;

        // Sorted indices for different sort orders (just indices into _index)
        mutable std::vector<size_t> _sorted_indices;
        mutable SortOrder _current_sort_order;
        mutable bool _sort_valid;

        // Config and channels (kept in RAM as before)
        meshtastic_LocalConfig _local_config;
        meshtastic_LocalModuleConfig _local_module_config;
        meshtastic_Channel _channels[8];
        ChannelGreeting _greetings[8] = {};

        uint32_t _our_node_id;
        bool _dirty;
        uint32_t _last_save_ms;
        bool _initialized;
        uint32_t _change_counter;

        static constexpr uint32_t SAVE_INTERVAL_MS = 30000; // Save every 30 seconds if dirty
    };

} // namespace Mesh

#endif // NODE_DB_H
