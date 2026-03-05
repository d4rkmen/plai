/**
 * @file ble_peripheral.h
 * @author d4rkmen
 * @brief BLE Peripheral implementation for Meshtastic mobile app connectivity
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef BLE_PERIPHERAL_H
#define BLE_PERIPHERAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Meshtastic BLE Service and Characteristic UUIDs
#define MESHTASTIC_SERVICE_UUID_STR "6ba1b218-15a8-461f-9fa8-5dcae273eafd"
#define MESHTASTIC_TORADIO_UUID_STR "f75c76d2-129e-4dad-a1dd-7866124401e7"
#define MESHTASTIC_FROMRADIO_UUID_STR "2c55e69e-4993-11ed-b878-0242ac120002"
#define MESHTASTIC_FROMNUM_UUID_STR "ed9da18c-a800-4f66-a670-aa75e347e353"
#define MESHTASTIC_LOGRADIO_UUID_STR "5a3d6e49-06e6-4423-9944-e9de8cdf9547"

// BLE Configuration
#define BLE_MAX_MTU 512
#define BLE_DEFAULT_MTU 256
#define BLE_ADV_INTERVAL_MS 100
#define BLE_DEVICE_NAME_MAX 32
#define BLE_FROMRADIO_BUFFER_SIZE 512
#define BLE_TORADIO_BUFFER_SIZE 512

    // BLE Peripheral states
    typedef enum
    {
        BLE_PERIPH_STATE_IDLE,
        BLE_PERIPH_STATE_ADVERTISING,
        BLE_PERIPH_STATE_CONNECTED,
        BLE_PERIPH_STATE_ERROR
    } ble_peripheral_state_t;

    // Connected client info
    typedef struct
    {
        uint16_t conn_handle;
        uint8_t addr[6];
        uint8_t addr_type;
        uint16_t mtu;
        bool subscribed_fromnum;
        bool authenticated;
    } ble_client_t;

    // Callback types
    typedef void (*ble_on_connect_cb_t)(ble_client_t* client);
    typedef void (*ble_on_disconnect_cb_t)(ble_client_t* client);
    typedef void (*ble_on_toradio_cb_t)(ble_client_t* client, const uint8_t* data, uint16_t len);
    typedef uint16_t (*ble_on_fromradio_cb_t)(ble_client_t* client, uint8_t* data, uint16_t max_len);

    // BLE Peripheral context
    typedef struct
    {
        ble_peripheral_state_t state;
        ble_client_t client;
        char device_name[BLE_DEVICE_NAME_MAX];
        uint32_t pairing_pin;
        bool require_pin;

        // FromRadio buffer for pending data
        uint8_t fromradio_buffer[BLE_FROMRADIO_BUFFER_SIZE];
        uint16_t fromradio_len;
        uint32_t fromradio_num; // Notification counter

        // Callbacks
        ble_on_connect_cb_t on_connect;
        ble_on_disconnect_cb_t on_disconnect;
        ble_on_toradio_cb_t on_toradio;
        ble_on_fromradio_cb_t on_fromradio;
    } ble_peripheral_ctx_t;

    /**
     * @brief Initialize BLE peripheral
     * @param device_name Device name to advertise (e.g., "Meshtastic_xxxx")
     * @param pin Pairing PIN (0 to disable PIN requirement)
     * @return 0 on success, negative on error
     */
    int ble_peripheral_init(const char* device_name, uint32_t pin);

    /**
     * @brief Deinitialize BLE peripheral
     */
    void ble_peripheral_deinit(void);

    /**
     * @brief Start BLE advertising
     * @return 0 on success, negative on error
     */
    int ble_peripheral_start_advertising(void);

    /**
     * @brief Stop BLE advertising
     * @return 0 on success, negative on error
     */
    int ble_peripheral_stop_advertising(void);

    /**
     * @brief Disconnect current client
     * @return 0 on success, negative on error
     */
    int ble_peripheral_disconnect(void);

    /**
     * @brief Check if a client is connected
     * @return true if connected
     */
    bool ble_peripheral_is_connected(void);

    /**
     * @brief Get current BLE state
     * @return Current state
     */
    ble_peripheral_state_t ble_peripheral_get_state(void);

    /**
     * @brief Get connected client info
     * @return Pointer to client info or NULL if not connected
     */
    ble_client_t* ble_peripheral_get_client(void);

    /**
     * @brief Set data to be read via FromRadio characteristic
     * @param data Data buffer
     * @param len Data length
     * @return 0 on success, negative on error
     */
    int ble_peripheral_set_fromradio(const uint8_t* data, uint16_t len);

    /**
     * @brief Notify connected client that FromRadio data is available
     * @return 0 on success, negative on error
     */
    int ble_peripheral_notify_fromnum(void);

    /**
     * @brief Update the FromNum value and send notification
     * @param num New FromNum value
     * @return 0 on success, negative on error
     */
    int ble_peripheral_update_fromnum(uint32_t num);

    /**
     * @brief Set connection callback
     * @param callback Callback function
     */
    void ble_peripheral_set_connect_callback(ble_on_connect_cb_t callback);

    /**
     * @brief Set disconnection callback
     * @param callback Callback function
     */
    void ble_peripheral_set_disconnect_callback(ble_on_disconnect_cb_t callback);

    /**
     * @brief Set ToRadio data received callback
     * @param callback Callback function
     */
    void ble_peripheral_set_toradio_callback(ble_on_toradio_cb_t callback);

    /**
     * @brief Set FromRadio data request callback
     * @param callback Callback function that fills buffer and returns length
     */
    void ble_peripheral_set_fromradio_callback(ble_on_fromradio_cb_t callback);

    /**
     * @brief Set device name (will take effect on next advertising start)
     * @param name New device name
     */
    void ble_peripheral_set_device_name(const char* name);

    /**
     * @brief Update pairing PIN
     * @param pin New PIN (0 to disable)
     */
    void ble_peripheral_set_pin(uint32_t pin);

#ifdef __cplusplus
}
#endif

#endif // BLE_PERIPHERAL_H

