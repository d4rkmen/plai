/**
 * @file settings.h
 * @brief Settings management system with NVS caching
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include "nvs_flash.h"
#include "esp_log.h"

namespace HAL
{
    class Hal;
}

namespace SETTINGS
{

    enum SettingType
    {
        TYPE_NONE,
        TYPE_BOOL,
        TYPE_NUMBER,
        TYPE_STRING,
        TYPE_CALLBACK
    };

    struct SettingItem_t
    {
        std::string key;
        std::string label;
        SettingType type;
        std::string default_val;
        std::string value;
        std::string min_val; // For TYPE_NUMBER
        std::string max_val; // For TYPE_NUMBER
        std::string hint;
        std::function<void(SettingItem_t& item)> callback;
    };

    struct SettingGroup_t
    {
        std::string name;
        std::string nvs_namespace;
        std::vector<SettingItem_t> items;
        std::function<void(SettingGroup_t& group)> callback;
    };

    class Settings
    {
    public:
        Settings();
        ~Settings();

        /**
         * @brief Set HAL pointer and initialize callbacks that require HAL access
         * @param hal Pointer to HAL instance
         */
        void setHal(HAL::Hal* hal);

        /**
         * @brief Initialize settings system and load values from NVS
         * @return true if successful
         */
        bool init();

        /**
         * @brief Get all setting groups metadata
         * @return Vector of setting groups
         */
        std::vector<SettingGroup_t> getMetadata() const;

        /**
         * @brief Get boolean setting value
         * @param ns Namespace
         * @param key Setting key
         * @return Boolean value
         */
        bool getBool(const std::string& ns, const std::string& key);

        /**
         * @brief Get number setting value
         * @param ns Namespace
         * @param key Setting key
         * @return Integer value
         */
        int32_t getNumber(const std::string& ns, const std::string& key);

        /**
         * @brief Get string setting value
         * @param ns Namespace
         * @param key Setting key
         * @return String value
         */
        std::string getString(const std::string& ns, const std::string& key);

        /**
         * @brief Set boolean setting value
         * @param ns Namespace
         * @param key Setting key
         * @param value Boolean value
         * @return true if successful
         */
        bool setBool(const std::string& ns, const std::string& key, bool value);

        /**
         * @brief Set number setting value
         * @param ns Namespace
         * @param key Setting key
         * @param value Integer value
         * @return true if successful
         */
        bool setNumber(const std::string& ns, const std::string& key, int32_t value);

        /**
         * @brief Set string setting value
         * @param ns Namespace
         * @param key Setting key
         * @param value String value
         * @return true if successful
         */
        bool setString(const std::string& ns, const std::string& key, const std::string& value);

        /**
         * @brief Save all modified settings to NVS
         * @return true if successful
         */
        bool saveAll();

        /**
         * @brief Export all settings to a file
         * @param filename The name of the file to export to
         * @return true if successful
         */
        bool exportToFile(const std::string& filename) const;

        /**
         * @brief Import settings from a file
         * @param filename The name of the file to import from
         * @return true if successful
         */
        bool importFromFile(const std::string& filename);

        /**
         * @brief Apply current mesh-related settings to MeshService
         * Reads LoRa, Node info, Security, and Position settings and pushes to mesh
         */
        void applyMeshConfig(SettingItem_t& item);

    private:
        static const char* const NVS_PARTITIONS[];
        const char* _active_partition = nullptr;

        // Cache storage
        struct CachedValue
        {
            SettingType type;
            union
            {
                bool bool_val;
                int32_t num_val;
            };
            std::string str_val;
        };

        HAL::Hal* _hal = nullptr;
        std::unordered_map<std::string, CachedValue> _cache;
        std::vector<SettingGroup_t> _metadata;
        bool _initialized = false;

        bool _initNvs();
        void _deinitNvs();
        void _loadSettings();
        std::string _makeKey(const std::string& ns, const std::string& key) const;
        const SettingItem_t* _findItem(const std::string& ns, const std::string& key) const;
    };

} // namespace SETTINGS