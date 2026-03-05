/**
 * @file usb.h
 * @brief USB MSC device handling
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "usb/usb_host.h"
#include "msc_host.h"
#include "msc_host_vfs.h"

namespace HAL
{
    /**
     * @brief USB device info structure
     */
    struct USBDeviceInfo
    {
        uint16_t idVendor;
        uint16_t idProduct;
        std::string manufacturer;
        std::string product;
        std::string serialNumber;
        uint32_t sectorSize;
        uint32_t sectorCount;
        uint64_t capacityBytes;
    };

    /**
     * @brief USB MSC device handler
     */
    class USB
    {
    private:
        static const char* MOUNT_POINT;

        BaseType_t _usb_task_created;
        BaseType_t _msc_task_created;
        TaskHandle_t _usb_task_handle;
        TaskHandle_t _msc_task_handle;
        QueueHandle_t _app_queue;
        bool _usb_initialized;
        bool _device_connected;
        bool _is_mounted;
        uint8_t _device_addr;
        USBDeviceInfo _device_info;
        msc_host_device_handle_t _msc_device;
        msc_host_vfs_handle_t _vfs_handle;
        void* _hal;

        // Internal message types
        enum MessageType
        {
            MSG_DEVICE_CONNECTED,
            MSG_DEVICE_DISCONNECTED
        };

        struct TaskMessage
        {
            MessageType type;
            uint8_t device_addr;
        };

        // USB task function
        static void usb_task(void* arg);
        // MSC task function
        static void msc_task(void* arg);
        // MSC event callback
        static void msc_event_callback(const void* event, void* arg);

    public:
        USB(void* hal);
        ~USB();

        /**
         * @brief Initialize USB host (creates tasks and queues)
         *
         * @return true if initialized successfully
         * @return false if initialization failed or USB host is disabled in settings
         */
        bool init();

        /**
         * @brief Deinitialize USB host (stops tasks and cleans up resources)
         */
        void deinit();

        /**
         * @brief Check if USB host is initialized
         *
         * @return true USB host is initialized
         * @return false USB host is not initialized
         */
        bool is_initialized() const { return _usb_initialized; }

        /**
         * @brief Mount the USB device filesystem
         *
         * @return true if mounted successfully
         * @return false if mount failed
         */
        bool mount();

        /**
         * @brief Unmount the USB device filesystem
         *
         * @return true if unmounted successfully
         * @return false if unmount failed
         */
        bool unmount();

        /**
         * @brief Check if USB device is mounted
         *
         * @return true Device is mounted
         * @return false Device is not mounted
         */
        bool is_mounted() const { return _device_connected && _is_mounted; }

        /**
         * @brief Check if USB device is connected
         *
         * @return true Device is connected
         * @return false Device is not connected
         */
        bool is_connected() const { return _device_connected; }

        /**
         * @brief Get USB device information
         *
         * @return const USBDeviceInfo& Device information
         */
        const USBDeviceInfo& get_device_info() const { return _device_info; }

        /**
         * @brief Get mount point
         *
         * @return const char* Mount point path
         */
        static const char* get_mount_point() { return MOUNT_POINT; }

        /**
         * @brief Get USB device name
         *
         * @return const std::string& Device name
         */
        std::string get_device_name() const { return _device_info.manufacturer; }

        /**
         * @brief Get USB device capacity
         *
         * @return uint64_t Capacity in bytes
         */
        uint64_t get_capacity() const { return _device_info.capacityBytes; }
    };

} // namespace HAL