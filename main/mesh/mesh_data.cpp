/**
 * @file mesh_data.cpp
 * @author d4rkmen
 * @brief Shared data structures for Meshtastic UI widgets with file-based storage
 * @version 2.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "mesh_data.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <map>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

static const char* TAG = "MESH_DATA";

namespace Mesh
{

    //--------------------------------------------------------------------------
    // Initialization
    //--------------------------------------------------------------------------

    bool MeshDataStore::init()
    {
        if (_initialized)
        {
            return true;
        }

        ESP_LOGI(TAG, "Initializing message store...");

        if (!createDirectories())
        {
            ESP_LOGW(TAG, "Failed to create message directories");
            // Continue anyway - we'll work without persistence
        }

        // Try to load existing index
        if (!loadIndex())
        {
            // No index found, rebuild from message files
            rebuildIndex();
        }

        _initialized = true;
        ESP_LOGI(TAG, "Message store initialized with %d conversations", _message_index.size());
        return true;
    }

    bool MeshDataStore::createDirectories()
    {
        struct stat st;

        // Create parent meshtastic directory first
        const char* parent_dir = "/sdcard/meshtastic";
        if (stat(parent_dir, &st) != 0)
        {
            ESP_LOGD(TAG, "Creating directory: %s", parent_dir);
            if (mkdir(parent_dir, 0755) != 0)
            {
                ESP_LOGE(TAG, "Failed to create directory: %s", parent_dir);
                return false;
            }
        }

        // Create messages directory
        if (stat(MESSAGES_DIR, &st) != 0)
        {
            ESP_LOGD(TAG, "Creating directory: %s", MESSAGES_DIR);
            if (mkdir(MESSAGES_DIR, 0755) != 0)
            {
                ESP_LOGE(TAG, "Failed to create directory: %s", MESSAGES_DIR);
                return false;
            }
        }

        // Create traceroute directory
        if (stat(TRACEROUTE_DIR, &st) != 0)
        {
            ESP_LOGD(TAG, "Creating directory: %s", TRACEROUTE_DIR);
            if (mkdir(TRACEROUTE_DIR, 0755) != 0)
            {
                ESP_LOGE(TAG, "Failed to create directory: %s", TRACEROUTE_DIR);
                return false;
            }
        }

        return true;
    }

    //--------------------------------------------------------------------------
    // File Path Helpers
    //--------------------------------------------------------------------------

    std::string MeshDataStore::getDMFilePath(uint32_t node_id) const
    {
        char path[64];
        snprintf(path, sizeof(path), "%s/%08lx.msg", MESSAGES_DIR, (unsigned long)node_id);
        return std::string(path);
    }

    std::string MeshDataStore::getChannelFilePath(uint8_t channel) const
    {
        char path[64];
        snprintf(path, sizeof(path), "%s/ch_%d.msg", MESSAGES_DIR, channel);
        return std::string(path);
    }

    std::string MeshDataStore::getTraceRouteFilePath(uint32_t node_id) const
    {
        char path[80];
        snprintf(path, sizeof(path), "%s/%08lx.trc", TRACEROUTE_DIR, (unsigned long)node_id);
        return std::string(path);
    }

    //--------------------------------------------------------------------------
    // Message File I/O
    //--------------------------------------------------------------------------

    bool MeshDataStore::loadMessagesFromFile(const std::string& path, std::vector<TextMessage>& out) const
    {
        FILE* file = fopen(path.c_str(), "rb");
        if (!file)
        {
            ESP_LOGD(TAG, "Message file not found: %s", path.c_str());
            return false;
        }

        // Read and verify header
        uint32_t magic = 0, version = 0, count = 0;
        if (fread(&magic, sizeof(magic), 1, file) != 1 || fread(&version, sizeof(version), 1, file) != 1 ||
            fread(&count, sizeof(count), 1, file) != 1)
        {
            fclose(file);
            return false;
        }

        if (magic != MSG_FILE_MAGIC || version != MSG_FILE_VERSION)
        {
            ESP_LOGW(TAG, "Invalid message file header: %s", path.c_str());
            fclose(file);
            return false;
        }

        out.clear();
        out.reserve(count);

        for (uint32_t i = 0; i < count; i++)
        {
            TextMessage msg = {};

            // Read fixed fields
            if (fread(&msg.id, sizeof(msg.id), 1, file) != 1)
                break;
            if (fread(&msg.from, sizeof(msg.from), 1, file) != 1)
                break;
            if (fread(&msg.to, sizeof(msg.to), 1, file) != 1)
                break;
            if (fread(&msg.timestamp, sizeof(msg.timestamp), 1, file) != 1)
                break;
            if (fread(&msg.channel, sizeof(msg.channel), 1, file) != 1)
                break;

            uint8_t flags = 0;
            if (fread(&flags, sizeof(flags), 1, file) != 1)
                break;
            msg.is_direct = (flags & 0x01) != 0;
            msg.read = (flags & 0x02) != 0;

            uint8_t status = 0;
            if (fread(&status, sizeof(status), 1, file) != 1)
                break;
            msg.status = static_cast<TextMessage::Status>(status);

            // Read text length and content
            uint16_t text_len = 0;
            if (fread(&text_len, sizeof(text_len), 1, file) != 1)
                break;

            if (text_len > 0 && text_len < 1024)
            {
                std::vector<char> text_buf(text_len + 1, 0);
                if (fread(text_buf.data(), 1, text_len, file) != text_len)
                    break;
                msg.text = std::string(text_buf.data());
            }

            out.push_back(msg);
        }

        fclose(file);
        ESP_LOGD(TAG, "Loaded %d messages from %s", out.size(), path.c_str());
        return true;
    }

    bool MeshDataStore::saveMessagesToFile(const std::string& path, const std::vector<TextMessage>& messages)
    {
        FILE* file = fopen(path.c_str(), "wb");
        if (!file)
        {
            ESP_LOGE(TAG, "Failed to open %s for writing", path.c_str());
            return false;
        }

        // Write header
        uint32_t magic = MSG_FILE_MAGIC;
        uint32_t version = MSG_FILE_VERSION;
        uint32_t count = messages.size();

        fwrite(&magic, sizeof(magic), 1, file);
        fwrite(&version, sizeof(version), 1, file);
        fwrite(&count, sizeof(count), 1, file);

        // Write each message
        for (const auto& msg : messages)
        {
            fwrite(&msg.id, sizeof(msg.id), 1, file);
            fwrite(&msg.from, sizeof(msg.from), 1, file);
            fwrite(&msg.to, sizeof(msg.to), 1, file);
            fwrite(&msg.timestamp, sizeof(msg.timestamp), 1, file);
            fwrite(&msg.channel, sizeof(msg.channel), 1, file);

            uint8_t flags = (msg.is_direct ? 0x01 : 0) | (msg.read ? 0x02 : 0);
            fwrite(&flags, sizeof(flags), 1, file);

            uint8_t status = static_cast<uint8_t>(msg.status);
            fwrite(&status, sizeof(status), 1, file);

            uint16_t text_len = msg.text.length();
            fwrite(&text_len, sizeof(text_len), 1, file);
            if (text_len > 0)
            {
                fwrite(msg.text.c_str(), 1, text_len, file);
            }
        }

        fclose(file);
        ESP_LOGD(TAG, "Saved %d messages to %s", messages.size(), path.c_str());
        return true;
    }

    bool MeshDataStore::appendMessageToFile(const std::string& path, const TextMessage& msg)
    {
        // Load existing messages (may be empty if file doesn't exist)
        std::vector<TextMessage> messages;
        loadMessagesFromFile(path, messages);

        ESP_LOGD(TAG, "Appending message to %s (existing: %d messages)", path.c_str(), messages.size());

        // Add new message
        messages.push_back(msg);

        // Limit messages per conversation
        while (messages.size() > MAX_MESSAGES_PER_CONV)
        {
            messages.erase(messages.begin());
        }

        // Save back
        bool success = saveMessagesToFile(path, messages);
        if (!success)
        {
            ESP_LOGE(TAG, "Failed to save messages to %s", path.c_str());
        }
        return success;
    }

    //--------------------------------------------------------------------------
    // Message status update (in-place file modification)
    //--------------------------------------------------------------------------

    bool MeshDataStore::updateMessageStatus(uint32_t packet_id, TextMessage::Status new_status)
    {
        // Scan all DM files to find the message by ID and update its status byte in-place
        // Status byte offset within each message record:
        //   id(4) + from(4) + to(4) + timestamp(4) + channel(1) + flags(1) = 18 bytes
        DIR* dir = opendir(MESSAGES_DIR);
        if (!dir)
            return false;

        struct dirent* entry;
        bool found = false;
        while (!found && (entry = readdir(dir)) != nullptr)
        {
            std::string filename(entry->d_name);
            if (filename.length() < 4 || filename.substr(filename.length() - 4) != ".msg")
                continue;

            std::string path = std::string(MESSAGES_DIR) + "/" + filename;

            // Open for read+write
            FILE* file = fopen(path.c_str(), "r+b");
            if (!file)
                continue;

            // Read and verify header
            uint32_t magic = 0, version = 0, count = 0;
            if (fread(&magic, 4, 1, file) != 1 || fread(&version, 4, 1, file) != 1 || fread(&count, 4, 1, file) != 1 ||
                magic != MSG_FILE_MAGIC || version != MSG_FILE_VERSION)
            {
                fclose(file);
                continue;
            }

            // Scan messages for matching ID
            for (uint32_t i = 0; i < count; i++)
            {
                long msg_start = ftell(file);
                uint32_t msg_id = 0;
                if (fread(&msg_id, 4, 1, file) != 1)
                    break;

                if (msg_id == packet_id)
                {
                    // Found it! Seek to status byte: msg_start + 18
                    fseek(file, msg_start + 18, SEEK_SET);
                    uint8_t status_byte = static_cast<uint8_t>(new_status);
                    fwrite(&status_byte, 1, 1, file);
                    fflush(file);
                    found = true;
                    ESP_LOGD(TAG,
                             "Updated message 0x%08lX status to %d in %s",
                             (unsigned long)packet_id,
                             (int)new_status,
                             path.c_str());
                    break;
                }

                // Skip to next message: already read id(4), skip from(4)+to(4)+timestamp(4)+channel(1)+flags(1)+status(1)=15
                if (fseek(file, 15, SEEK_CUR) != 0)
                    break;
                uint16_t text_len = 0;
                if (fread(&text_len, 2, 1, file) != 1)
                    break;
                if (text_len > 0 && fseek(file, text_len, SEEK_CUR) != 0)
                    break;
            }

            fclose(file);
        }
        closedir(dir);

        if (found)
        {
            _change_counter++;
        }
        else
        {
            ESP_LOGW(TAG, "Message 0x%08lX not found for status update", (unsigned long)packet_id);
        }

        return found;
    }

    //--------------------------------------------------------------------------
    // Lazy-load message access (read from file without loading all into RAM)
    //--------------------------------------------------------------------------

    // Per-message fixed fields: id(4) + from(4) + to(4) + timestamp(4) + channel(1) + flags(1) + status(1) + text_len(2) = 21
    static constexpr size_t MSG_FIXED_SIZE = 21;
    static constexpr size_t MSG_FILE_HEADER_SIZE = 12; // magic(4) + version(4) + count(4)

    /**
     * Open a DM file, verify header, return file handle and message count.
     * Caller must fclose the returned FILE*.
     */
    static FILE* openMessageFileAndReadCount(const char* path, uint32_t& out_count)
    {
        out_count = 0;
        FILE* file = fopen(path, "rb");
        if (!file)
            return nullptr;

        uint32_t magic = 0, version = 0, count = 0;
        if (fread(&magic, 4, 1, file) != 1 || fread(&version, 4, 1, file) != 1 || fread(&count, 4, 1, file) != 1 ||
            magic != MSG_FILE_MAGIC || version != MSG_FILE_VERSION)
        {
            fclose(file);
            return nullptr;
        }
        out_count = count;
        return file;
    }

    /**
     * Read one message from the current file position. Returns true on success.
     */
    static bool readOneMessage(FILE* file, TextMessage& msg)
    {
        msg = {};
        if (fread(&msg.id, 4, 1, file) != 1)
            return false;
        if (fread(&msg.from, 4, 1, file) != 1)
            return false;
        if (fread(&msg.to, 4, 1, file) != 1)
            return false;
        if (fread(&msg.timestamp, 4, 1, file) != 1)
            return false;
        if (fread(&msg.channel, 1, 1, file) != 1)
            return false;

        uint8_t flags = 0;
        if (fread(&flags, 1, 1, file) != 1)
            return false;
        msg.is_direct = (flags & 0x01) != 0;
        msg.read = (flags & 0x02) != 0;

        uint8_t status = 0;
        if (fread(&status, 1, 1, file) != 1)
            return false;
        msg.status = static_cast<TextMessage::Status>(status);

        uint16_t text_len = 0;
        if (fread(&text_len, 2, 1, file) != 1)
            return false;

        if (text_len > 0 && text_len < 1024)
        {
            std::vector<char> buf(text_len + 1, 0);
            if (fread(buf.data(), 1, text_len, file) != text_len)
                return false;
            msg.text = std::string(buf.data());
        }
        return true;
    }

    /**
     * Skip one message at the current file position (read only text_len, seek past text).
     * Optionally returns the text_len.
     */
    static bool skipOneMessage(FILE* file, uint16_t* out_text_len = nullptr)
    {
        // Skip fixed fields (id + from + to + timestamp + channel + flags + status = 19 bytes)
        if (fseek(file, 19, SEEK_CUR) != 0)
            return false;
        uint16_t text_len = 0;
        if (fread(&text_len, 2, 1, file) != 1)
            return false;
        if (out_text_len)
            *out_text_len = text_len;
        if (text_len > 0)
        {
            if (fseek(file, text_len, SEEK_CUR) != 0)
                return false;
        }
        return true;
    }

    uint32_t MeshDataStore::getDMMessageCount(uint32_t node_id) const
    {
        std::string path = getDMFilePath(node_id);
        uint32_t count = 0;
        FILE* file = openMessageFileAndReadCount(path.c_str(), count);
        if (file)
            fclose(file);
        return count;
    }

    bool MeshDataStore::hasDMMessages(uint32_t node_id) const
    {
        const MessageIndexEntry* entry = findIndexEntry(node_id, true);
        return entry && entry->message_count > 0;
    }

    bool MeshDataStore::getDMMessageByIndex(uint32_t node_id, uint32_t index, TextMessage& out) const
    {
        std::string path = getDMFilePath(node_id);
        uint32_t count = 0;
        FILE* file = openMessageFileAndReadCount(path.c_str(), count);
        if (!file || index >= count)
        {
            if (file)
                fclose(file);
            return false;
        }

        // Skip messages before the target index
        for (uint32_t i = 0; i < index; i++)
        {
            if (!skipOneMessage(file))
            {
                fclose(file);
                return false;
            }
        }

        bool ok = readOneMessage(file, out);
        fclose(file);
        return ok;
    }

    uint32_t
    MeshDataStore::getDMMessageRange(uint32_t node_id, uint32_t start, uint32_t count, std::vector<TextMessage>& out) const
    {
        std::string path = getDMFilePath(node_id);
        uint32_t total = 0;
        FILE* file = openMessageFileAndReadCount(path.c_str(), total);
        if (!file || start >= total)
        {
            if (file)
                fclose(file);
            return 0;
        }

        // Skip messages before start
        for (uint32_t i = 0; i < start; i++)
        {
            if (!skipOneMessage(file))
            {
                fclose(file);
                return (uint32_t)out.size();
            }
        }

        // Read requested range
        uint32_t to_read = std::min(count, total - start);
        uint32_t loaded = 0;
        for (uint32_t i = 0; i < to_read; i++)
        {
            TextMessage msg;
            if (!readOneMessage(file, msg))
                break;
            out.push_back(std::move(msg));
            loaded++;
        }

        fclose(file);
        return loaded;
    }

    uint32_t MeshDataStore::getDMTextLengths(uint32_t node_id, std::vector<uint16_t>& text_lengths) const
    {
        std::string path = getDMFilePath(node_id);
        uint32_t count = 0;
        FILE* file = openMessageFileAndReadCount(path.c_str(), count);
        if (!file)
            return 0;

        text_lengths.clear();
        text_lengths.reserve(count);

        for (uint32_t i = 0; i < count; i++)
        {
            uint16_t tlen = 0;
            if (!skipOneMessage(file, &tlen))
                break;
            text_lengths.push_back(tlen);
        }

        fclose(file);
        return (uint32_t)text_lengths.size();
    }

    uint32_t MeshDataStore::forEachDMMessage(uint32_t node_id,
                                             std::function<bool(uint32_t index, const TextMessage& msg)> callback) const
    {
        std::string path = getDMFilePath(node_id);
        uint32_t count = 0;
        FILE* file = openMessageFileAndReadCount(path.c_str(), count);
        if (!file)
            return 0;

        uint32_t iterated = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            TextMessage msg;
            if (!readOneMessage(file, msg))
                break;
            iterated++;
            if (!callback(i, msg))
                break;
        }

        fclose(file);
        return iterated;
    }

    //--------------------------------------------------------------------------
    // Channel lazy-loading (mirrors DM methods using getChannelFilePath)
    //--------------------------------------------------------------------------

    uint32_t MeshDataStore::getChannelMessageCount(uint8_t channel) const
    {
        std::string path = getChannelFilePath(channel);
        uint32_t count = 0;
        FILE* file = openMessageFileAndReadCount(path.c_str(), count);
        if (file)
            fclose(file);
        return count;
    }

    uint32_t
    MeshDataStore::getChannelMessageRange(uint8_t channel, uint32_t start, uint32_t count, std::vector<TextMessage>& out) const
    {
        std::string path = getChannelFilePath(channel);
        uint32_t total = 0;
        FILE* file = openMessageFileAndReadCount(path.c_str(), total);
        if (!file || start >= total)
        {
            if (file)
                fclose(file);
            return 0;
        }

        for (uint32_t i = 0; i < start; i++)
        {
            if (!skipOneMessage(file))
            {
                fclose(file);
                return (uint32_t)out.size();
            }
        }

        uint32_t to_read = std::min(count, total - start);
        uint32_t loaded = 0;
        for (uint32_t i = 0; i < to_read; i++)
        {
            TextMessage msg;
            if (!readOneMessage(file, msg))
                break;
            out.push_back(std::move(msg));
            loaded++;
        }

        fclose(file);
        return loaded;
    }

    uint32_t MeshDataStore::forEachChannelMessage(uint8_t channel,
                                                   std::function<bool(uint32_t index, const TextMessage& msg)> callback) const
    {
        std::string path = getChannelFilePath(channel);
        uint32_t count = 0;
        FILE* file = openMessageFileAndReadCount(path.c_str(), count);
        if (!file)
            return 0;

        uint32_t iterated = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            TextMessage msg;
            if (!readOneMessage(file, msg))
                break;
            iterated++;
            if (!callback(i, msg))
                break;
        }

        fclose(file);
        return iterated;
    }

    //--------------------------------------------------------------------------
    // Index Management
    //--------------------------------------------------------------------------

    bool MeshDataStore::loadIndex()
    {
        FILE* file = fopen(MSG_INDEX_FILE, "rb");
        if (!file)
        {
            ESP_LOGW(TAG, "No message index file found");
            return false;
        }

        // Read and verify header
        uint32_t magic = 0, version = 0, count = 0;
        if (fread(&magic, sizeof(magic), 1, file) != 1 || fread(&version, sizeof(version), 1, file) != 1 ||
            fread(&count, sizeof(count), 1, file) != 1)
        {
            fclose(file);
            return false;
        }

        if (magic != MSG_INDEX_MAGIC || version != MSG_FILE_VERSION)
        {
            ESP_LOGW(TAG, "Invalid message index header");
            fclose(file);
            return false;
        }

        _message_index.clear();
        _message_index.reserve(count);

        for (uint32_t i = 0; i < count; i++)
        {
            MessageIndexEntry entry = {};

            if (fread(&entry.node_id, sizeof(entry.node_id), 1, file) != 1)
                break;
            if (fread(&entry.channel, sizeof(entry.channel), 1, file) != 1)
                break;

            uint8_t flags = 0;
            if (fread(&flags, sizeof(flags), 1, file) != 1)
                break;
            entry.is_direct = (flags & 0x01) != 0;

            if (fread(&entry.message_count, sizeof(entry.message_count), 1, file) != 1)
                break;
            if (fread(&entry.unread_count, sizeof(entry.unread_count), 1, file) != 1)
                break;
            if (fread(&entry.last_timestamp, sizeof(entry.last_timestamp), 1, file) != 1)
                break;

            _message_index.push_back(entry);
        }

        fclose(file);
        ESP_LOGD(TAG, "Loaded message index with %d entries", _message_index.size());
        return true;
    }

    bool MeshDataStore::saveIndex()
    {
        FILE* file = fopen(MSG_INDEX_FILE, "wb");
        if (!file)
        {
            ESP_LOGE(TAG, "Failed to open %s for writing", MSG_INDEX_FILE);
            return false;
        }

        // Write header
        uint32_t magic = MSG_INDEX_MAGIC;
        uint32_t version = MSG_FILE_VERSION;
        uint32_t count = _message_index.size();

        fwrite(&magic, sizeof(magic), 1, file);
        fwrite(&version, sizeof(version), 1, file);
        fwrite(&count, sizeof(count), 1, file);

        // Write entries
        for (const auto& entry : _message_index)
        {
            fwrite(&entry.node_id, sizeof(entry.node_id), 1, file);
            fwrite(&entry.channel, sizeof(entry.channel), 1, file);

            uint8_t flags = entry.is_direct ? 0x01 : 0;
            fwrite(&flags, sizeof(flags), 1, file);

            fwrite(&entry.message_count, sizeof(entry.message_count), 1, file);
            fwrite(&entry.unread_count, sizeof(entry.unread_count), 1, file);
            fwrite(&entry.last_timestamp, sizeof(entry.last_timestamp), 1, file);
        }

        fclose(file);
        ESP_LOGD(TAG, "Saved message index with %d entries", _message_index.size());
        return true;
    }

    bool MeshDataStore::rebuildIndex()
    {
        ESP_LOGD(TAG, "Rebuilding message index from files...");
        _message_index.clear();

        DIR* dir = opendir(MESSAGES_DIR);
        if (!dir)
        {
            ESP_LOGW(TAG, "Cannot open messages directory");
            return false;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            std::string filename(entry->d_name);

            // Check for DM files (xxxxxxxx.msg)
            if (filename.length() == 12 && filename.substr(8) == ".msg")
            {
                uint32_t node_id = 0;
                if (sscanf(filename.c_str(), "%08lx", (unsigned long*)&node_id) == 1)
                {
                    std::string path = getDMFilePath(node_id);
                    std::vector<TextMessage> messages;
                    if (loadMessagesFromFile(path, messages) && !messages.empty())
                    {
                        MessageIndexEntry idx = {};
                        idx.node_id = node_id;
                        idx.is_direct = true;
                        idx.message_count = messages.size();
                        idx.unread_count = 0;
                        idx.last_timestamp = messages.back().timestamp;

                        for (const auto& msg : messages)
                        {
                            if (!msg.read && msg.from == node_id)
                            {
                                idx.unread_count++;
                            }
                        }

                        _message_index.push_back(idx);
                    }
                }
            }
            // Check for channel files (ch_X.msg)
            else if (filename.length() >= 7 && filename.substr(0, 3) == "ch_")
            {
                int channel = -1;
                if (sscanf(filename.c_str(), "ch_%d.msg", &channel) == 1 && channel >= 0 && channel < 8)
                {
                    std::string path = getChannelFilePath((uint8_t)channel);
                    std::vector<TextMessage> messages;
                    if (loadMessagesFromFile(path, messages) && !messages.empty())
                    {
                        MessageIndexEntry idx = {};
                        idx.node_id = 0;
                        idx.channel = (uint8_t)channel;
                        idx.is_direct = false;
                        idx.message_count = messages.size();
                        idx.unread_count = 0;
                        idx.last_timestamp = messages.back().timestamp;

                        for (const auto& msg : messages)
                        {
                            if (!msg.read)
                            {
                                idx.unread_count++;
                            }
                        }

                        _message_index.push_back(idx);
                    }
                }
            }
        }

        closedir(dir);
        saveIndex();

        ESP_LOGD(TAG, "Rebuilt message index with %d conversations", _message_index.size());
        return true;
    }

    MessageIndexEntry* MeshDataStore::findIndexEntry(uint32_t node_id, bool is_direct)
    {
        for (auto& entry : _message_index)
        {
            if (is_direct)
            {
                if (entry.is_direct && entry.node_id == node_id)
                {
                    return &entry;
                }
            }
            else
            {
                if (!entry.is_direct && entry.channel == (uint8_t)node_id)
                {
                    return &entry;
                }
            }
        }
        return nullptr;
    }

    const MessageIndexEntry* MeshDataStore::findIndexEntry(uint32_t node_id, bool is_direct) const
    {
        for (const auto& entry : _message_index)
        {
            if (is_direct)
            {
                if (entry.is_direct && entry.node_id == node_id)
                {
                    return &entry;
                }
            }
            else
            {
                if (!entry.is_direct && entry.channel == (uint8_t)node_id)
                {
                    return &entry;
                }
            }
        }
        return nullptr;
    }

    void MeshDataStore::updateIndexEntry(
        uint32_t node_id, bool is_direct, uint8_t channel, int count_delta, int unread_delta, uint32_t timestamp)
    {
        MessageIndexEntry* entry = findIndexEntry(is_direct ? node_id : channel, is_direct);

        if (entry)
        {
            entry->message_count += count_delta;
            entry->unread_count += unread_delta;
            if (entry->unread_count > entry->message_count)
            {
                entry->unread_count = entry->message_count;
            }
            if (timestamp > entry->last_timestamp)
            {
                entry->last_timestamp = timestamp;
            }
        }
        else
        {
            // Create new entry
            MessageIndexEntry new_entry = {};
            new_entry.node_id = is_direct ? node_id : 0;
            new_entry.channel = channel;
            new_entry.is_direct = is_direct;
            new_entry.message_count = (count_delta > 0) ? count_delta : 0;
            new_entry.unread_count = (unread_delta > 0) ? unread_delta : 0;
            new_entry.last_timestamp = timestamp;
            _message_index.push_back(new_entry);
        }

        // Save index after update
        saveIndex();
    }

    //--------------------------------------------------------------------------
    // Public Message API
    //--------------------------------------------------------------------------

    void MeshDataStore::addMessage(const TextMessage& msg)
    {
        // Auto-initialize if not already done
        if (!_initialized)
        {
            init();
        }

        std::string path;
        uint32_t index_id;

        if (msg.is_direct)
        {
            // For DMs, use the other party's node ID
            // If we sent it (read=true), use destination; if received (read=false), use sender
            index_id = (msg.from != 0 && msg.to != 0) ? (msg.read ? msg.to : msg.from) : (msg.from != 0 ? msg.from : msg.to);
            path = getDMFilePath(index_id);
            ESP_LOGI(TAG,
                     "Adding DM: from=0x%08lX to=0x%08lX -> file for node 0x%08lX",
                     (unsigned long)msg.from,
                     (unsigned long)msg.to,
                     (unsigned long)index_id);
        }
        else
        {
            index_id = msg.channel;
            path = getChannelFilePath(msg.channel);
            ESP_LOGI(TAG, "Adding channel message: channel=%d", msg.channel);
        }

        // Append to file
        if (appendMessageToFile(path, msg))
        {
            // Update index - increment unread count if message is unread
            int unread_delta = msg.read ? 0 : 1;
            updateIndexEntry(index_id, msg.is_direct, msg.channel, 1, unread_delta, msg.timestamp);
            _change_counter++;

            ESP_LOGD(TAG, "Message saved to %s (unread=%d)", path.c_str(), !msg.read);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to save message to %s", path.c_str());
        }
    }

    std::vector<TextMessage> MeshDataStore::getDirectMessages(uint32_t node_id) const
    {
        std::string path = getDMFilePath(node_id);
        std::vector<TextMessage> messages;
        loadMessagesFromFile(path, messages);

        // Sort by timestamp
        // std::sort(messages.begin(),
        //           messages.end(),
        //           [](const TextMessage& a, const TextMessage& b) { return a.timestamp < b.timestamp; });

        return messages;
    }

    std::vector<TextMessage> MeshDataStore::getChannelMessages(uint8_t channel) const
    {
        std::string path = getChannelFilePath(channel);
        std::vector<TextMessage> messages;
        loadMessagesFromFile(path, messages);

        // Sort by timestamp
        // std::sort(messages.begin(),
        //           messages.end(),
        //           [](const TextMessage& a, const TextMessage& b) { return a.timestamp < b.timestamp; });

        return messages;
    }

    uint32_t MeshDataStore::getUnreadDMCount(uint32_t node_id) const
    {
        const MessageIndexEntry* entry = findIndexEntry(node_id, true);
        return entry ? entry->unread_count : 0;
    }

    uint32_t MeshDataStore::getUnreadChannelCount(uint8_t channel) const
    {
        const MessageIndexEntry* entry = findIndexEntry(channel, false);
        return entry ? entry->unread_count : 0;
    }

    void MeshDataStore::markMessagesRead(uint32_t id, bool is_channel)
    {
        std::string path;
        if (is_channel)
        {
            path = getChannelFilePath((uint8_t)id);
        }
        else
        {
            path = getDMFilePath(id);
        }

        // Load messages
        std::vector<TextMessage> messages;
        if (!loadMessagesFromFile(path, messages))
        {
            return;
        }

        // Mark all as read
        bool changed = false;
        for (auto& msg : messages)
        {
            if (!msg.read)
            {
                msg.read = true;
                changed = true;
            }
        }

        if (changed)
        {
            // Save back
            saveMessagesToFile(path, messages);

            // Update index
            MessageIndexEntry* entry = findIndexEntry(id, !is_channel);
            if (entry)
            {
                entry->unread_count = 0;
                saveIndex();
            }
        }
    }

    void MeshDataStore::clearMessages()
    {
        // Delete all message files
        DIR* dir = opendir(MESSAGES_DIR);
        if (dir)
        {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr)
            {
                std::string filename(entry->d_name);
                if (filename.find(".msg") != std::string::npos)
                {
                    std::string path = std::string(MESSAGES_DIR) + "/" + filename;
                    remove(path.c_str());
                }
            }
            closedir(dir);
        }

        // Clear index
        _message_index.clear();
        saveIndex();
        _change_counter++;

        ESP_LOGI(TAG, "Cleared all messages");
    }

    void MeshDataStore::clearConversation(uint32_t node_id, bool is_channel)
    {
        std::string path;
        if (is_channel)
        {
            path = getChannelFilePath((uint8_t)node_id);
        }
        else
        {
            path = getDMFilePath(node_id);
        }

        // Delete the file
        remove(path.c_str());

        // Remove from index
        auto it = std::find_if(_message_index.begin(),
                               _message_index.end(),
                               [node_id, is_channel](const MessageIndexEntry& e)
                               {
                                   if (is_channel)
                                   {
                                       return !e.is_direct && e.channel == (uint8_t)node_id;
                                   }
                                   return e.is_direct && e.node_id == node_id;
                               });

        if (it != _message_index.end())
        {
            _message_index.erase(it);
            saveIndex();
        }
        _change_counter++;

        ESP_LOGI(TAG, "Cleared conversation: %s", path.c_str());
    }

    std::vector<uint32_t> MeshDataStore::getConversationsWithUnread() const
    {
        std::vector<uint32_t> result;
        for (const auto& entry : _message_index)
        {
            if (entry.is_direct && entry.unread_count > 0)
            {
                result.push_back(entry.node_id);
            }
        }
        return result;
    }

    uint32_t MeshDataStore::getTotalUnreadDMCount() const
    {
        uint32_t total = 0;
        for (const auto& entry : _message_index)
        {
            if (entry.is_direct)
            {
                total += entry.unread_count;
            }
        }
        return total;
    }

    uint32_t MeshDataStore::getTotalUnreadChannelCount() const
    {
        uint32_t total = 0;
        for (const auto& entry : _message_index)
        {
            if (!entry.is_direct)
            {
                total += entry.unread_count;
            }
        }
        return total;
    }

    //--------------------------------------------------------------------------
    // Packet Log
    //--------------------------------------------------------------------------

    void MeshDataStore::addPacketLogEntry(const PacketLogEntry& entry) { _packet_log.push(entry); }

    //--------------------------------------------------------------------------
    //--------------------------------------------------------------------------
    // Node data removal
    //--------------------------------------------------------------------------

    void MeshDataStore::removeNodeData(uint32_t node_id)
    {
        std::string dm_path = getDMFilePath(node_id);
        if (remove(dm_path.c_str()) == 0)
            ESP_LOGI(TAG, "Removed DM file for 0x%08lX", (unsigned long)node_id);

        std::string tr_path = getTraceRouteFilePath(node_id);
        if (remove(tr_path.c_str()) == 0)
            ESP_LOGI(TAG, "Removed traceroute file for 0x%08lX", (unsigned long)node_id);
    }

    // Statistics
    //--------------------------------------------------------------------------

    void MeshDataStore::resetStats()
    {
        bool ble_state = _stats.ble_connected;
        _stats = MeshStats();
        _stats.ble_connected = ble_state;
    }

    //--------------------------------------------------------------------------
    // Graph Data
    //--------------------------------------------------------------------------

    void MeshDataStore::addBatteryPoint(float voltage)
    {
        GraphPoint point;
        point.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
        point.value = voltage;
        _battery_history.push_back(point);

        while (_battery_history.size() > MAX_GRAPH_POINTS)
        {
            _battery_history.erase(_battery_history.begin());
        }
    }

    void MeshDataStore::addChannelActivityPoint(float packets_per_min)
    {
        GraphPoint point;
        point.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
        point.value = packets_per_min;
        _channel_activity.push_back(point);

        while (_channel_activity.size() > MAX_GRAPH_POINTS)
        {
            _channel_activity.erase(_channel_activity.begin());
        }
    }

    void MeshDataStore::addRssiPoint(uint32_t node_id, int16_t rssi)
    {
        GraphPoint point;
        point.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
        point.value = (float)rssi;

        auto& history = _rssi_history[node_id];
        history.push_back(point);

        while (history.size() > MAX_GRAPH_POINTS)
        {
            history.erase(history.begin());
        }
    }

    std::vector<GraphPoint> MeshDataStore::getRssiHistory(uint32_t node_id) const
    {
        auto it = _rssi_history.find(node_id);
        if (it != _rssi_history.end())
        {
            return it->second;
        }
        return {};
    }

    //--------------------------------------------------------------------------
    // Traceroute File Storage
    //--------------------------------------------------------------------------

    void MeshDataStore::trResultToRecord(const TraceRouteResult& src, TraceRouteRecord& dst)
    {
        memset(&dst, 0, sizeof(dst));
        dst.target_node_id = src.target_node_id;
        dst.timestamp = src.timestamp;
        dst.duration_sec = src.duration_sec;
        dst.status = static_cast<uint8_t>(src.status);
        dst.dest_snr_q4 = (int8_t)(src.dest_snr * 4.0f);
        dst.origin_snr_q4 = (int8_t)(src.origin_snr * 4.0f);
        dst.route_to_count = std::min((size_t)src.route_to.size(), (size_t)8);
        dst.route_back_count = std::min((size_t)src.route_back.size(), (size_t)8);
        for (uint8_t i = 0; i < dst.route_to_count; i++)
        {
            dst.route_to[i].node_id = src.route_to[i].node_id;
            dst.route_to[i].snr_q4 = (int8_t)(src.route_to[i].snr * 4.0f);
        }
        for (uint8_t i = 0; i < dst.route_back_count; i++)
        {
            dst.route_back[i].node_id = src.route_back[i].node_id;
            dst.route_back[i].snr_q4 = (int8_t)(src.route_back[i].snr * 4.0f);
        }
    }

    void MeshDataStore::trRecordToResult(const TraceRouteRecord& src, TraceRouteResult& dst)
    {
        dst.target_node_id = src.target_node_id;
        dst.timestamp = src.timestamp;
        dst.duration_sec = src.duration_sec;
        dst.status = static_cast<TraceRouteResult::Status>(src.status);
        dst.dest_snr = (float)src.dest_snr_q4 / 4.0f;
        dst.origin_snr = (float)src.origin_snr_q4 / 4.0f;
        dst.route_to.clear();
        dst.route_back.clear();
        for (uint8_t i = 0; i < src.route_to_count && i < 8; i++)
            dst.route_to.push_back({src.route_to[i].node_id, (float)src.route_to[i].snr_q4 / 4.0f});
        for (uint8_t i = 0; i < src.route_back_count && i < 8; i++)
            dst.route_back.push_back({src.route_back[i].node_id, (float)src.route_back[i].snr_q4 / 4.0f});
    }

    uint32_t MeshDataStore::getTraceRouteCount(uint32_t node_id) const
    {
        std::string path = getTraceRouteFilePath(node_id);
        FILE* file = fopen(path.c_str(), "rb");
        if (!file)
            return 0;

        uint32_t magic = 0, version = 0, count = 0;
        if (fread(&magic, 4, 1, file) != 1 || fread(&version, 4, 1, file) != 1 ||
            fread(&count, 4, 1, file) != 1)
        {
            fclose(file);
            return 0;
        }
        fclose(file);

        if (magic != TRC_FILE_MAGIC || version != TRC_FILE_VERSION)
            return 0;
        return count;
    }

    bool MeshDataStore::getTraceRouteByIndex(uint32_t node_id, uint32_t index, TraceRouteResult& out) const
    {
        std::string path = getTraceRouteFilePath(node_id);
        FILE* file = fopen(path.c_str(), "rb");
        if (!file)
            return false;

        uint32_t magic = 0, version = 0, count = 0;
        if (fread(&magic, 4, 1, file) != 1 || fread(&version, 4, 1, file) != 1 ||
            fread(&count, 4, 1, file) != 1 || magic != TRC_FILE_MAGIC ||
            version != TRC_FILE_VERSION || index >= count)
        {
            fclose(file);
            return false;
        }

        long offset = (long)TRC_FILE_HEADER_SIZE + (long)index * (long)sizeof(TraceRouteRecord);
        if (fseek(file, offset, SEEK_SET) != 0)
        {
            fclose(file);
            return false;
        }

        TraceRouteRecord rec;
        if (fread(&rec, sizeof(rec), 1, file) != 1)
        {
            fclose(file);
            return false;
        }
        fclose(file);
        trRecordToResult(rec, out);
        return true;
    }

    uint32_t MeshDataStore::getTraceRouteRange(uint32_t node_id, uint32_t start, uint32_t count,
                                                std::vector<TraceRouteResult>& out) const
    {
        std::string path = getTraceRouteFilePath(node_id);
        FILE* file = fopen(path.c_str(), "rb");
        if (!file)
            return 0;

        uint32_t magic = 0, version = 0, total = 0;
        if (fread(&magic, 4, 1, file) != 1 || fread(&version, 4, 1, file) != 1 ||
            fread(&total, 4, 1, file) != 1 || magic != TRC_FILE_MAGIC ||
            version != TRC_FILE_VERSION || start >= total)
        {
            fclose(file);
            return 0;
        }

        uint32_t avail = total - start;
        uint32_t to_read = std::min(count, avail);

        long offset = (long)TRC_FILE_HEADER_SIZE + (long)start * (long)sizeof(TraceRouteRecord);
        if (fseek(file, offset, SEEK_SET) != 0)
        {
            fclose(file);
            return 0;
        }

        uint32_t loaded = 0;
        for (uint32_t i = 0; i < to_read; i++)
        {
            TraceRouteRecord rec;
            if (fread(&rec, sizeof(rec), 1, file) != 1)
                break;
            TraceRouteResult result;
            trRecordToResult(rec, result);
            out.push_back(std::move(result));
            loaded++;
        }
        fclose(file);
        return loaded;
    }

    uint32_t MeshDataStore::addTraceRoute(uint32_t node_id, const TraceRouteResult& result)
    {
        std::string path = getTraceRouteFilePath(node_id);

        // Read existing records
        std::vector<TraceRouteRecord> records;
        FILE* file = fopen(path.c_str(), "rb");
        if (file)
        {
            uint32_t magic = 0, version = 0, count = 0;
            if (fread(&magic, 4, 1, file) == 1 && fread(&version, 4, 1, file) == 1 &&
                fread(&count, 4, 1, file) == 1 && magic == TRC_FILE_MAGIC &&
                version == TRC_FILE_VERSION)
            {
                records.resize(count);
                for (uint32_t i = 0; i < count; i++)
                {
                    if (fread(&records[i], sizeof(TraceRouteRecord), 1, file) != 1)
                    {
                        records.resize(i);
                        break;
                    }
                }
            }
            fclose(file);
        }

        // Append new record
        TraceRouteRecord rec;
        trResultToRecord(result, rec);
        records.push_back(rec);

        // Trim oldest if over limit
        while (records.size() > MAX_TRACEROUTES_PER_NODE)
            records.erase(records.begin());

        uint32_t new_index = (uint32_t)(records.size() - 1);

        // Write back
        file = fopen(path.c_str(), "wb");
        if (!file)
        {
            ESP_LOGE(TAG, "Failed to write traceroute file: %s", path.c_str());
            return new_index;
        }

        uint32_t magic = TRC_FILE_MAGIC;
        uint32_t version = TRC_FILE_VERSION;
        uint32_t count = (uint32_t)records.size();
        fwrite(&magic, 4, 1, file);
        fwrite(&version, 4, 1, file);
        fwrite(&count, 4, 1, file);
        for (const auto& r : records)
            fwrite(&r, sizeof(r), 1, file);
        fclose(file);

        ESP_LOGD(TAG, "Saved %lu traceroute records for node %08lx",
                 (unsigned long)count, (unsigned long)node_id);
        return new_index;
    }

    bool MeshDataStore::updateTraceRoute(uint32_t node_id, uint32_t index, const TraceRouteResult& result)
    {
        std::string path = getTraceRouteFilePath(node_id);
        FILE* file = fopen(path.c_str(), "r+b");
        if (!file)
            return false;

        uint32_t magic = 0, version = 0, count = 0;
        if (fread(&magic, 4, 1, file) != 1 || fread(&version, 4, 1, file) != 1 ||
            fread(&count, 4, 1, file) != 1 || magic != TRC_FILE_MAGIC ||
            version != TRC_FILE_VERSION || index >= count)
        {
            fclose(file);
            return false;
        }

        TraceRouteRecord rec;
        trResultToRecord(result, rec);

        long offset = (long)TRC_FILE_HEADER_SIZE + (long)index * (long)sizeof(TraceRouteRecord);
        if (fseek(file, offset, SEEK_SET) != 0)
        {
            fclose(file);
            return false;
        }

        bool ok = fwrite(&rec, sizeof(rec), 1, file) == 1;
        fclose(file);
        return ok;
    }

} // namespace Mesh
