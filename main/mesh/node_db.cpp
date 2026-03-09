/**
 * @file node_db.cpp
 * @author d4rkmen
 * @brief Node database implementation with lazy loading from storage
 * @version 2.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "node_db.h"
#include "mesh_data.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <algorithm>
#include <stdio.h>
#include <time.h>
#include <format>

static const char* TAG = "NODE_DB";

static inline uint32_t millis() { return xTaskGetTickCount() * portTICK_PERIOD_MS; }

namespace Mesh
{

    NodeDB::NodeDB()
        : _current_sort_order(SortOrder::LAST_HEARD), _sort_valid(false), _our_node_id(0), _dirty(false), _last_save_ms(0),
          _initialized(false), _change_counter(0)
    {
        memset(&_local_config, 0, sizeof(_local_config));
        memset(&_local_module_config, 0, sizeof(_local_module_config));
        memset(_channels, 0, sizeof(_channels));
        _index.reserve(MAX_NODES);
    }

    NodeDB::~NodeDB()
    {
        if (_dirty)
        {
            save();
        }
    }

    bool NodeDB::init(uint32_t our_node_id)
    {
        ESP_LOGI(TAG, "Initializing node database for node 0x%08lX", (unsigned long)our_node_id);

        _our_node_id = our_node_id;

        // Create storage directories
        if (!createDirectories())
        {
            ESP_LOGW(TAG, "Failed to create storage directories, continuing without persistence");
        }

        // Check for legacy format and migrate if needed
        struct stat st;
        if (stat(NODEDB_FILE, &st) == 0)
        {
            ESP_LOGI(TAG, "Found legacy nodedb.pb, migrating to new format...");
            if (migrateFromLegacy())
            {
                ESP_LOGI(TAG, "Migration successful");
                // Remove legacy file after successful migration
                remove(NODEDB_FILE);
            }
            else
            {
                ESP_LOGW(TAG, "Migration failed, starting fresh");
            }
        }

        // Try to load existing index
        if (!loadIndex())
        {
            // No index found, try to rebuild from node files
            rebuildIndex();
        }

        // Load preferences, channels and greetings
        loadPrefs();
        loadChannels();
        loadGreetings();

        // Sort index by default order
        sortIndex(SortOrder::LAST_HEARD);

        _initialized = true;
        ESP_LOGI(TAG, "Node database initialized with %d nodes", _index.size());
        return true;
    }

    bool NodeDB::createDirectories()
    {
        struct stat st;

        // Create main mesh directory
        if (stat(MESH_DIR, &st) != 0)
        {
            ESP_LOGI(TAG, "Creating directory: %s", MESH_DIR);
            if (mkdir(MESH_DIR, 0755) != 0)
            {
                ESP_LOGE(TAG, "Failed to create directory: %s", MESH_DIR);
                return false;
            }
        }

        // Create nodes subdirectory
        if (stat(NODES_DIR, &st) != 0)
        {
            ESP_LOGI(TAG, "Creating directory: %s", NODES_DIR);
            if (mkdir(NODES_DIR, 0755) != 0)
            {
                ESP_LOGE(TAG, "Failed to create directory: %s", NODES_DIR);
                return false;
            }
        }

        return true;
    }

    //--------------------------------------------------------------------------
    // Individual Node File Operations
    //--------------------------------------------------------------------------

    std::string NodeDB::getNodeFilePath(uint32_t node_id) const
    {
        // char path[64];
        // snprintf(path, sizeof(path), "%s/%08lx.pb", NODES_DIR, (unsigned long)node_id);
        return std::format("{}/{:08x}.pb", NODES_DIR, node_id);
    }

    bool NodeDB::loadNodeFromFile(uint32_t node_id, NodeInfo& out) const
    {
        std::string path = getNodeFilePath(node_id);
        FILE* file = fopen(path.c_str(), "rb");
        if (!file)
        {
            ESP_LOGD(TAG, "Node file not found: %s", path.c_str());
            return false;
        }

        bool success = false;
        uint8_t buffer[meshtastic_NodeInfo_size + 16];

        // Read length prefix
        uint16_t len = 0;
        if (fread(&len, sizeof(len), 1, file) == 1 && len <= sizeof(buffer))
        {
            if (fread(buffer, 1, len, file) == len)
            {
                out = {};
                pb_istream_t stream = pb_istream_from_buffer(buffer, len);
                if (pb_decode(&stream, meshtastic_NodeInfo_fields, &out.info))
                {
                    // Read RSSI and relay_node (not in protobuf)
                    fread(&out.last_rssi, sizeof(out.last_rssi), 1, file);
                    fread(&out.relay_node, sizeof(out.relay_node), 1, file);
                    success = true;
                }
                else
                {
                    ESP_LOGW(TAG, "Failed to decode node from %s", path.c_str());
                }
            }
        }

        fclose(file);
        return success;
    }

    bool NodeDB::saveNodeToFile(const NodeInfo& node)
    {
        std::string path = getNodeFilePath(node.info.num);
        FILE* file = fopen(path.c_str(), "wb");
        if (!file)
        {
            ESP_LOGE(TAG, "Failed to open %s for writing", path.c_str());
            return false;
        }

        bool success = false;
        uint8_t buffer[meshtastic_NodeInfo_size + 16];

        pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
        if (pb_encode(&stream, meshtastic_NodeInfo_fields, &node.info))
        {
            // Write length prefix
            uint16_t len = stream.bytes_written;
            fwrite(&len, sizeof(len), 1, file);
            fwrite(buffer, 1, len, file);

            // Write RSSI and relay_node (not in protobuf)
            fwrite(&node.last_rssi, sizeof(node.last_rssi), 1, file);
            fwrite(&node.relay_node, sizeof(node.relay_node), 1, file);
            success = true;
        }

        fclose(file);

        if (success)
        {
            ESP_LOGD(TAG, "Saved node 0x%08lX to %s", (unsigned long)node.info.num, path.c_str());
        }

        return success;
    }

    bool NodeDB::deleteNodeFile(uint32_t node_id)
    {
        std::string path = getNodeFilePath(node_id);
        if (remove(path.c_str()) == 0)
        {
            ESP_LOGI(TAG, "Deleted node file: %s", path.c_str());
            return true;
        }
        ESP_LOGW(TAG, "Failed to delete node file: %s", path.c_str());
        return false;
    }

    //--------------------------------------------------------------------------
    // Index Management
    //--------------------------------------------------------------------------

    bool NodeDB::loadIndex()
    {
        FILE* file = fopen(MANIFEST_FILE, "rb");
        if (!file)
        {
            ESP_LOGW(TAG, "No manifest file found");
            return false;
        }

        bool success = false;

        // Read and verify header
        uint32_t magic = 0, version = 0, count = 0;
        if (fread(&magic, sizeof(magic), 1, file) == 1 && fread(&version, sizeof(version), 1, file) == 1 &&
            fread(&count, sizeof(count), 1, file) == 1)
        {
            if (magic == MANIFEST_MAGIC && version == MANIFEST_VERSION)
            {
                if (count > MAX_NODES)
                {
                    ESP_LOGW(TAG, "Index count %lu exceeds max, truncating", (unsigned long)count);
                    count = MAX_NODES;
                }

                _index.clear();
                _index.reserve(count);

                for (uint32_t i = 0; i < count; i++)
                {
                    NodeIndexEntry entry = {};

                    if (fread(&entry.node_id, sizeof(entry.node_id), 1, file) != 1)
                        break;
                    if (fread(&entry.last_heard, sizeof(entry.last_heard), 1, file) != 1)
                        break;
                    if (fread(&entry.last_rssi, sizeof(entry.last_rssi), 1, file) != 1)
                        break;
                    if (fread(&entry.is_favorite, sizeof(entry.is_favorite), 1, file) != 1)
                        break;
                    if (fread(&entry.short_name, sizeof(entry.short_name), 1, file) != 1)
                        break;
                    if (fread(&entry.long_name, sizeof(entry.long_name), 1, file) != 1)
                        break;
                    if (fread(&entry.role, sizeof(entry.role), 1, file) != 1)
                        break;
                    if (fread(&entry.hops_away, sizeof(entry.hops_away), 1, file) != 1)
                        break;
                    if (fread(&entry.snr, sizeof(entry.snr), 1, file) != 1)
                        break;

                    // Verify file exists
                    std::string path = getNodeFilePath(entry.node_id);
                    struct stat st;
                    entry.exists = (stat(path.c_str(), &st) == 0);

                    if (entry.exists)
                    {
                        _index.push_back(entry);
                    }
                }

                success = true;
                _sort_valid = false;
                ESP_LOGI(TAG, "Loaded index with %d entries", _index.size());
            }
            else
            {
                ESP_LOGW(TAG,
                         "Invalid manifest header (magic=0x%08lX, version=%lu)",
                         (unsigned long)magic,
                         (unsigned long)version);
            }
        }

        fclose(file);
        return success;
    }

    bool NodeDB::saveIndex()
    {
        FILE* file = fopen(MANIFEST_FILE, "wb");
        if (!file)
        {
            ESP_LOGE(TAG, "Failed to open %s for writing", MANIFEST_FILE);
            return false;
        }

        // Write header
        uint32_t magic = MANIFEST_MAGIC;
        uint32_t version = MANIFEST_VERSION;
        uint32_t count = _index.size();

        fwrite(&magic, sizeof(magic), 1, file);
        fwrite(&version, sizeof(version), 1, file);
        fwrite(&count, sizeof(count), 1, file);

        // Write entries
        for (const auto& entry : _index)
        {
            fwrite(&entry.node_id, sizeof(entry.node_id), 1, file);
            fwrite(&entry.last_heard, sizeof(entry.last_heard), 1, file);
            fwrite(&entry.last_rssi, sizeof(entry.last_rssi), 1, file);
            fwrite(&entry.is_favorite, sizeof(entry.is_favorite), 1, file);
            fwrite(&entry.short_name, sizeof(entry.short_name), 1, file);
            fwrite(&entry.long_name, sizeof(entry.long_name), 1, file);
            fwrite(&entry.role, sizeof(entry.role), 1, file);
            fwrite(&entry.hops_away, sizeof(entry.hops_away), 1, file);
            fwrite(&entry.snr, sizeof(entry.snr), 1, file);
        }

        fclose(file);
        ESP_LOGD(TAG, "Saved index with %d entries", _index.size());
        return true;
    }

    bool NodeDB::rebuildIndex()
    {
        ESP_LOGI(TAG, "Rebuilding index from node files...");
        _index.clear();

        DIR* dir = opendir(NODES_DIR);
        if (!dir)
        {
            ESP_LOGW(TAG, "Cannot open nodes directory");
            return false;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            // Check if file matches pattern xxxxxxxx.pb
            size_t len = strlen(entry->d_name);
            if (len == 11 && strcmp(entry->d_name + 8, ".pb") == 0)
            {
                // Parse node ID from filename
                uint32_t node_id = 0;
                if (sscanf(entry->d_name, "%08lx", (unsigned long*)&node_id) == 1)
                {
                    // Load node to get index data
                    NodeInfo node;
                    if (loadNodeFromFile(node_id, node))
                    {
                        updateIndexEntry(node);
                    }
                }
            }
        }

        closedir(dir);
        _sort_valid = false;

        ESP_LOGI(TAG, "Rebuilt index with %d entries", _index.size());
        return true;
    }

    NodeIndexEntry* NodeDB::findIndexEntry(uint32_t node_id)
    {
        for (auto& entry : _index)
        {
            if (entry.node_id == node_id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    const NodeIndexEntry* NodeDB::findIndexEntry(uint32_t node_id) const
    {
        for (const auto& entry : _index)
        {
            if (entry.node_id == node_id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    std::string NodeDB::getLabel(const NodeInfo& node)
    {
        std::string label = node.info.user.short_name;
        if (label.empty())
        {
            label = std::format("{:04x}", node.info.num & 0xFFFF);
        }
        return label;
    }

    const char* NodeDB::getRoleName(meshtastic_Config_DeviceConfig_Role role)
    {
        switch (role)
        {
        case meshtastic_Config_DeviceConfig_Role_CLIENT:        return "Client";
        case meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE:   return "Client Mute";
        case meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN: return "Client Hidden";
        case meshtastic_Config_DeviceConfig_Role_CLIENT_BASE:   return "Client Base";
        case meshtastic_Config_DeviceConfig_Role_ROUTER:        return "Router";
        case meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT: return "Router Client";
        case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:   return "Router Late";
        case meshtastic_Config_DeviceConfig_Role_REPEATER:      return "Repeater";
        case meshtastic_Config_DeviceConfig_Role_TRACKER:       return "Tracker";
        case meshtastic_Config_DeviceConfig_Role_SENSOR:        return "Sensor";
        case meshtastic_Config_DeviceConfig_Role_TAK:           return "TAK";
        case meshtastic_Config_DeviceConfig_Role_TAK_TRACKER:   return "TAK Tracker";
        case meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND: return "Lost&Found";
        default:                                                return "Unknown";
        }
    }

    void NodeDB::fillIndexEntryFromNode(NodeIndexEntry& entry, const NodeInfo& node)
    {
        entry.node_id = node.info.num;
        entry.last_heard = node.info.last_heard;
        entry.last_rssi = node.last_rssi;
        entry.is_favorite = node.info.is_favorite;
        entry.exists = true;
        // Sort-relevant fields
        memset(entry.short_name, 0, sizeof(entry.short_name));
        memset(entry.long_name, 0, sizeof(entry.long_name));
        if (node.info.has_user)
        {
            strncpy(entry.short_name, node.info.user.short_name, sizeof(entry.short_name) - 1);
            strncpy(entry.long_name, node.info.user.long_name, sizeof(entry.long_name) - 1);
            entry.role = (uint8_t)node.info.user.role;
        }
        else
        {
            entry.role = 0;
        }
        entry.hops_away = node.info.has_hops_away ? (uint8_t)node.info.hops_away : 0;
        entry.snr = node.info.snr;
    }

    void NodeDB::updateIndexEntry(const NodeInfo& node)
    {
        NodeIndexEntry* entry = findIndexEntry(node.info.num);
        if (entry)
        {
            fillIndexEntryFromNode(*entry, node);
        }
        else
        {
            // Add new entry
            if (_index.size() >= MAX_NODES)
            {
                // Remove oldest non-favorite entry
                auto oldest = std::min_element(_index.begin(),
                                               _index.end(),
                                               [](const NodeIndexEntry& a, const NodeIndexEntry& b)
                                               {
                                                   if (a.is_favorite != b.is_favorite)
                                                       return !a.is_favorite;
                                                   return a.last_heard < b.last_heard;
                                               });
                if (oldest != _index.end())
                {
                    deleteNodeFile(oldest->node_id);
                    _index.erase(oldest);
                }
            }

            NodeIndexEntry new_entry = {};
            fillIndexEntryFromNode(new_entry, node);
            _index.push_back(new_entry);
        }
        _sort_valid = false;
    }

    void NodeDB::removeIndexEntry(uint32_t node_id)
    {
        auto it =
            std::find_if(_index.begin(), _index.end(), [node_id](const NodeIndexEntry& e) { return e.node_id == node_id; });
        if (it != _index.end())
        {
            _index.erase(it);
            _sort_valid = false;
        }
    }

    //--------------------------------------------------------------------------
    // Legacy Migration
    //--------------------------------------------------------------------------

    bool NodeDB::loadLegacyNodeDb(std::vector<NodeInfo>& nodes)
    {
        FILE* file = fopen(NODEDB_FILE, "rb");
        if (!file)
        {
            return false;
        }

        // Read number of nodes
        uint32_t count = 0;
        if (fread(&count, sizeof(count), 1, file) != 1)
        {
            fclose(file);
            return false;
        }

        if (count > MAX_NODES)
        {
            count = MAX_NODES;
        }

        nodes.clear();
        uint8_t buffer[meshtastic_NodeInfo_size + 16];

        for (uint32_t i = 0; i < count; i++)
        {
            uint16_t len = 0;
            if (fread(&len, sizeof(len), 1, file) != 1)
                break;

            if (len > sizeof(buffer))
                break;

            if (fread(buffer, 1, len, file) != len)
                break;

            NodeInfo node = {};
            pb_istream_t stream = pb_istream_from_buffer(buffer, len);
            if (!pb_decode(&stream, meshtastic_NodeInfo_fields, &node.info))
            {
                continue;
            }

            // Read legacy metadata and migrate to protobuf fields
            uint32_t legacy_last_heard;
            int16_t legacy_rssi;
            float legacy_snr;
            bool legacy_favorite;
            fread(&legacy_last_heard, sizeof(legacy_last_heard), 1, file);
            fread(&legacy_rssi, sizeof(legacy_rssi), 1, file);
            fread(&legacy_snr, sizeof(legacy_snr), 1, file);
            fread(&legacy_favorite, sizeof(legacy_favorite), 1, file);

            // Migrate to new format
            node.info.last_heard = legacy_last_heard;
            node.info.snr = legacy_snr;
            node.info.is_favorite = legacy_favorite;
            node.last_rssi = legacy_rssi;

            nodes.push_back(node);
        }

        fclose(file);
        return !nodes.empty();
    }

    bool NodeDB::migrateFromLegacy()
    {
        std::vector<NodeInfo> legacy_nodes;
        if (!loadLegacyNodeDb(legacy_nodes))
        {
            return false;
        }

        ESP_LOGI(TAG, "Migrating %d nodes from legacy format", legacy_nodes.size());

        _index.clear();
        for (const auto& node : legacy_nodes)
        {
            if (saveNodeToFile(node))
            {
                updateIndexEntry(node);
            }
        }

        saveIndex();
        return true;
    }

    //--------------------------------------------------------------------------
    // Sorting
    //--------------------------------------------------------------------------

    void NodeDB::sortIndex(SortOrder order)
    {
        if (_sort_valid && _current_sort_order == order)
        {
            return;
        }

        _sorted_indices.clear();
        _sorted_indices.reserve(_index.size());
        for (size_t i = 0; i < _index.size(); i++)
        {
            _sorted_indices.push_back(i);
        }

        switch (order)
        {
        case SortOrder::NONE:
            // No sorting - keep insertion order
            break;

        case SortOrder::SHORT_NAME:
            std::sort(_sorted_indices.begin(),
                      _sorted_indices.end(),
                      [this](size_t a, size_t b)
                      { return strncasecmp(_index[a].short_name, _index[b].short_name, sizeof(_index[0].short_name)) < 0; });
            break;

        case SortOrder::LONG_NAME:
            std::sort(_sorted_indices.begin(),
                      _sorted_indices.end(),
                      [this](size_t a, size_t b)
                      { return strncasecmp(_index[a].long_name, _index[b].long_name, sizeof(_index[0].long_name)) < 0; });
            break;

        case SortOrder::ROLE:
            std::sort(_sorted_indices.begin(),
                      _sorted_indices.end(),
                      [this](size_t a, size_t b)
                      {
                          if (_index[a].role != _index[b].role)
                              return _index[a].role < _index[b].role;
                          return _index[a].last_heard > _index[b].last_heard;
                      });
            break;

        case SortOrder::SIGNAL:
            std::sort(_sorted_indices.begin(),
                      _sorted_indices.end(),
                      [this](size_t a, size_t b) { return _index[a].last_rssi > _index[b].last_rssi; });
            break;

        case SortOrder::HOPS_AWAY:
            std::sort(_sorted_indices.begin(),
                      _sorted_indices.end(),
                      [this](size_t a, size_t b)
                      {
                          if (_index[a].hops_away != _index[b].hops_away)
                              return _index[a].hops_away < _index[b].hops_away;
                          return _index[a].last_heard > _index[b].last_heard;
                      });
            break;

        case SortOrder::LAST_HEARD:
            std::sort(_sorted_indices.begin(),
                      _sorted_indices.end(),
                      [this](size_t a, size_t b) { return _index[a].last_heard > _index[b].last_heard; });
            break;

        case SortOrder::FAVORITES_FIRST:
            std::sort(_sorted_indices.begin(),
                      _sorted_indices.end(),
                      [this](size_t a, size_t b)
                      {
                          if (_index[a].is_favorite != _index[b].is_favorite)
                          {
                              return _index[a].is_favorite;
                          }
                          return _index[a].last_heard > _index[b].last_heard;
                      });
            break;
        }

        _current_sort_order = order;
        _sort_valid = true;
    }

    //--------------------------------------------------------------------------
    // Public API
    //--------------------------------------------------------------------------

    bool NodeDB::load()
    {
        ESP_LOGI(TAG, "Loading node database from storage");

        bool success = loadIndex();
        // success &= loadPrefs();
        success &= loadChannels();

        return success;
    }

    bool NodeDB::save()
    {
        ESP_LOGD(TAG, "Saving node database to storage");

        bool success = saveIndex();
        // success &= savePrefs();
        success &= saveChannels();

        if (success)
        {
            _dirty = false;
            _last_save_ms = millis();
        }

        return success;
    }

    size_t NodeDB::getNodeCount() const { return _index.size(); }

    bool NodeDB::getNodeByIndex(size_t index, NodeInfo& out) const
    {
        // Ensure sorted
        if (!_sort_valid)
        {
            const_cast<NodeDB*>(this)->sortIndex(_current_sort_order);
        }

        if (index >= _sorted_indices.size())
        {
            return false;
        }

        size_t actual_index = _sorted_indices[index];
        if (actual_index >= _index.size())
        {
            return false;
        }

        return loadNodeFromFile(_index[actual_index].node_id, out);
    }

    size_t NodeDB::getNodesInRange(size_t offset, size_t count, std::vector<NodeInfo>& out, SortOrder order) const
    {
        out.clear();

        // Ensure sorted with requested order
        if (!_sort_valid || _current_sort_order != order)
        {
            const_cast<NodeDB*>(this)->sortIndex(order);
        }

        if (offset >= _sorted_indices.size())
        {
            return 0;
        }

        size_t end = std::min(offset + count, _sorted_indices.size());
        out.reserve(end - offset);

        for (size_t i = offset; i < end; i++)
        {
            NodeInfo node;
            size_t actual_index = _sorted_indices[i];
            if (actual_index < _index.size() && loadNodeFromFile(_index[actual_index].node_id, node))
            {
                out.push_back(node);
            }
        }

        return out.size();
    }

    bool NodeDB::getNode(uint32_t node_id, NodeInfo& out) const
    {
        const NodeIndexEntry* entry = findIndexEntry(node_id);
        if (!entry || !entry->exists)
        {
            return false;
        }
        return loadNodeFromFile(node_id, out);
    }

    const NodeIndexEntry* NodeDB::getNodeIndex(uint32_t node_id) const { return findIndexEntry(node_id); }

    uint32_t NodeDB::findNodeByRelayByte(uint8_t relay_byte) const
    {
        uint32_t best_id = 0;
        uint8_t best_hops = 0xFF;
        for (const auto& entry : _index)
        {
            if ((entry.node_id & 0xFF) == relay_byte && entry.hops_away < best_hops)
            {
                best_id = entry.node_id;
                best_hops = entry.hops_away;
            }
        }
        return best_id;
    }

    int NodeDB::getSortedIndexForNode(uint32_t node_id, SortOrder order) const
    {
        if (!_sort_valid || _current_sort_order != order)
        {
            const_cast<NodeDB*>(this)->sortIndex(order);
        }
        for (size_t i = 0; i < _sorted_indices.size(); i++)
        {
            size_t idx = _sorted_indices[i];
            if (idx < _index.size() && _index[idx].node_id == node_id)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool NodeDB::updateNode(const meshtastic_NodeInfo& node, int16_t rssi, float snr, uint8_t relay_node)
    {
        NodeInfo full_node = {};
        bool exists = loadNodeFromFile(node.num, full_node);

        if (exists)
        {
            // Update existing node - preserve is_favorite
            bool was_favorite = full_node.info.is_favorite;
            full_node.info = node;
            full_node.info.is_favorite = was_favorite;
            full_node.info.last_heard = (uint32_t)time(nullptr);
            if (rssi != -1)
            {
                full_node.last_rssi = rssi;
                full_node.info.snr = snr;
                full_node.relay_node = relay_node;
            }
        }
        else
        {
            // New node
            full_node.info = node;
            full_node.info.last_heard = (uint32_t)time(nullptr);
            full_node.last_rssi = (rssi != -1) ? rssi : 0;
            full_node.info.snr = snr;
            full_node.info.is_favorite = false;
            full_node.relay_node = relay_node;
        }

        if (saveNodeToFile(full_node))
        {
            updateIndexEntry(full_node);
            _dirty = true;
            ESP_LOGD(TAG, "%s node 0x%08lX", exists ? "Updated" : "Added", (unsigned long)node.num);
            return true;
        }

        return false;
    }

    bool NodeDB::removeNode(uint32_t node_id)
    {
        if (deleteNodeFile(node_id))
        {
            Mesh::MeshDataStore::getInstance().removeNodeData(node_id);
            removeIndexEntry(node_id);
            _dirty = true;
            ESP_LOGI(TAG, "Removed node 0x%08lX", (unsigned long)node_id);
            return true;
        }

        return false;
    }

    void NodeDB::clearNodes()
    {
        for (const auto& entry : _index)
        {
            Mesh::MeshDataStore::getInstance().removeNodeData(entry.node_id);
            deleteNodeFile(entry.node_id);
        }

        _index.clear();
        _sort_valid = false;
        _dirty = true;
        ESP_LOGI(TAG, "Cleared all nodes");
    }

    bool NodeDB::updatePosition(uint32_t node_id, const meshtastic_Position& position)
    {
        NodeInfo node = {};
        bool exists = loadNodeFromFile(node_id, node);

        if (!exists)
        {
            // Create new node with just position
            node.info = meshtastic_NodeInfo_init_default;
            node.info.num = node_id;
            node.info.last_heard = (uint32_t)time(nullptr);
        }

        node.info.has_position = true;
        node.info.position = position;
        node.info.last_heard = (uint32_t)time(nullptr);

        if (saveNodeToFile(node))
        {
            updateIndexEntry(node);
            _dirty = true;
            return true;
        }
        return false;
    }

    bool NodeDB::updateUser(uint32_t node_id, const meshtastic_User& user)
    {
        NodeInfo node = {};
        bool exists = loadNodeFromFile(node_id, node);

        if (!exists)
        {
            // Create new node with just user info
            node.info = meshtastic_NodeInfo_init_default;
            node.info.num = node_id;
            node.info.last_heard = (uint32_t)time(nullptr);
        }

        node.info.has_user = true;
        node.info.user = user;
        node.info.last_heard = (uint32_t)time(nullptr);

        if (saveNodeToFile(node))
        {
            updateIndexEntry(node);
            _dirty = true;
            return true;
        }
        return false;
    }

    bool NodeDB::setFavorite(uint32_t node_id, bool favorite)
    {
        NodeInfo node = {};
        if (!loadNodeFromFile(node_id, node))
        {
            return false;
        }

        node.info.is_favorite = favorite;

        if (saveNodeToFile(node))
        {
            NodeIndexEntry* entry = findIndexEntry(node_id);
            if (entry)
            {
                entry->is_favorite = favorite;
                _sort_valid = false;
            }
            _dirty = true;
            return true;
        }
        return false;
    }

    //--------------------------------------------------------------------------
    // Preferences and Channels (unchanged)
    //--------------------------------------------------------------------------

    bool NodeDB::savePrefs()
    {
        FILE* file = fopen(PREFS_FILE, "wb");
        if (!file)
        {
            ESP_LOGE(TAG, "Failed to open %s for writing", PREFS_FILE);
            return false;
        }

        // Save LocalConfig
        uint8_t buffer[meshtastic_LocalConfig_size];
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
        if (pb_encode(&stream, meshtastic_LocalConfig_fields, &_local_config))
        {
            uint16_t len = stream.bytes_written;
            fwrite(&len, sizeof(len), 1, file);
            fwrite(buffer, 1, len, file);
        }

        // Save LocalModuleConfig
        stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
        if (pb_encode(&stream, meshtastic_LocalModuleConfig_fields, &_local_module_config))
        {
            uint16_t len = stream.bytes_written;
            fwrite(&len, sizeof(len), 1, file);
            fwrite(buffer, 1, len, file);
        }

        fclose(file);
        return true;
    }

    bool NodeDB::loadPrefs()
    {
        FILE* file = fopen(PREFS_FILE, "rb");
        if (!file)
        {
            ESP_LOGW(TAG, "No existing prefs file");
            return false;
        }

        uint8_t buffer[meshtastic_LocalConfig_size];

        // Load LocalConfig
        uint16_t len = 0;
        if (fread(&len, sizeof(len), 1, file) == 1 && len <= sizeof(buffer))
        {
            if (fread(buffer, 1, len, file) == len)
            {
                pb_istream_t stream = pb_istream_from_buffer(buffer, len);
                pb_decode(&stream, meshtastic_LocalConfig_fields, &_local_config);
            }
        }

        // Load LocalModuleConfig
        if (fread(&len, sizeof(len), 1, file) == 1 && len <= sizeof(buffer))
        {
            if (fread(buffer, 1, len, file) == len)
            {
                pb_istream_t stream = pb_istream_from_buffer(buffer, len);
                pb_decode(&stream, meshtastic_LocalModuleConfig_fields, &_local_module_config);
            }
        }

        fclose(file);
        return true;
    }

    bool NodeDB::saveChannels()
    {
        FILE* file = fopen(CHANNELS_FILE, "wb");
        if (!file)
        {
            ESP_LOGE(TAG, "Failed to open %s for writing", CHANNELS_FILE);
            return false;
        }

        uint8_t buffer[meshtastic_Channel_size];

        for (int i = 0; i < 8; i++)
        {
            pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
            if (pb_encode(&stream, meshtastic_Channel_fields, &_channels[i]))
            {
                uint16_t len = stream.bytes_written;
                fwrite(&len, sizeof(len), 1, file);
                fwrite(buffer, 1, len, file);
            }
        }

        fclose(file);
        return true;
    }

    bool NodeDB::loadChannels()
    {
        FILE* file = fopen(CHANNELS_FILE, "rb");
        if (!file)
        {
            ESP_LOGW(TAG, "No existing channels file");
            return false;
        }

        uint8_t buffer[meshtastic_Channel_size];

        for (int i = 0; i < 8; i++)
        {
            uint16_t len = 0;
            if (fread(&len, sizeof(len), 1, file) != 1)
                break;

            if (len > sizeof(buffer))
                break;

            if (fread(buffer, 1, len, file) != len)
                break;

            pb_istream_t stream = pb_istream_from_buffer(buffer, len);
            pb_decode(&stream, meshtastic_Channel_fields, &_channels[i]);
        }

        fclose(file);
        return true;
    }

    meshtastic_Channel* NodeDB::getChannel(uint8_t index)
    {
        if (index >= 8)
        {
            return nullptr;
        }
        return &_channels[index];
    }

    bool NodeDB::setChannel(uint8_t index, const meshtastic_Channel& channel)
    {
        if (index >= 8)
        {
            return false;
        }
        _channels[index] = channel;
        _channels[index].index = index;
        _dirty = true;
        return true;
    }

    bool NodeDB::deleteChannel(uint8_t index)
    {
        if (index >= 8)
        {
            return false;
        }
        _channels[index] = meshtastic_Channel_init_default;
        _greetings[index] = {};
        _dirty = true;
        return true;
    }

    const ChannelGreeting& NodeDB::getGreeting(uint8_t index) const
    {
        static const ChannelGreeting empty = {};
        if (index >= 8)
            return empty;
        return _greetings[index];
    }

    void NodeDB::setGreeting(uint8_t index, const ChannelGreeting& greeting)
    {
        if (index >= 8)
            return;
        _greetings[index] = greeting;
    }

    bool NodeDB::saveGreetings()
    {
        FILE* file = fopen(GREETINGS_FILE, "wb");
        if (!file)
        {
            ESP_LOGE(TAG, "Failed to open %s for writing", GREETINGS_FILE);
            return false;
        }
        fwrite(_greetings, sizeof(_greetings), 1, file);
        fclose(file);
        return true;
    }

    bool NodeDB::loadGreetings()
    {
        FILE* file = fopen(GREETINGS_FILE, "rb");
        if (!file)
        {
            ESP_LOGD(TAG, "No existing greetings file");
            return false;
        }
        size_t read = fread(_greetings, 1, sizeof(_greetings), file);
        fclose(file);
        if (read != sizeof(_greetings))
        {
            ESP_LOGW(TAG, "Greetings file size mismatch, resetting");
            memset(_greetings, 0, sizeof(_greetings));
            return false;
        }
        return true;
    }

    void NodeDB::checkSave()
    {
        if (!_dirty)
        {
            return;
        }

        uint32_t now = millis();
        if ((now - _last_save_ms) >= SAVE_INTERVAL_MS)
        {
            save();
        }
    }

} // namespace Mesh
