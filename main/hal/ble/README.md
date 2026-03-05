# BLE Peripheral for Meshtastic Mobile App

This module implements a BLE peripheral (server) that exposes the Meshtastic GATT service, allowing the Meshtastic mobile app to connect and control this device.

## Features

- **BLE Advertising**: Advertises as a Meshtastic device with configurable name
- **GATT Service**: Exposes the standard Meshtastic BLE service with ToRadio, FromRadio, and FromNum characteristics
- **Mobile App Support**: Compatible with official Meshtastic iOS and Android apps
- **PIN Authentication**: Optional PIN-based pairing for security
- **MTU Negotiation**: Supports up to 512-byte MTU for efficient data transfer

## Files

- `ble_peripheral.h` - Header file with API definitions
- `ble_peripheral.cpp` - NimBLE GATT server implementation

## Meshtastic BLE Protocol

### Service UUID

`6ba1b218-15a8-461f-9fa8-5dcae273eafd`

### Characteristics

| Name      | UUID                                   | Properties   | Description                                             |
| --------- | -------------------------------------- | ------------ | ------------------------------------------------------- |
| ToRadio   | `f75c76d2-129e-4dad-a1dd-7866124401e7` | Write        | Mobile app writes commands/messages here                |
| FromRadio | `2c55e69e-4993-11ed-b878-0242ac120002` | Read         | Mobile app reads responses/messages here                |
| FromNum   | `ed9da18c-a800-4f66-a670-aa75e347e353` | Read, Notify | Notification counter, increments when data is available |
| LogRadio  | `5a3d6e49-06e6-4423-9944-e9de8cdf9547` | Read, Notify | Optional debug logging (not used)                       |

## Usage

The BLE peripheral is automatically managed by the MeshService. Direct usage:

### Initialization

```cpp
// Initialize with device name and optional PIN
ble_peripheral_init("Meshtastic_XXXX", 123456);

// Or without PIN
ble_peripheral_init("Meshtastic_XXXX", 0);
```

### Setting Callbacks

```cpp
// Called when mobile app connects
ble_peripheral_set_connect_callback([](ble_client_t* client) {
    printf("App connected\n");
});

// Called when mobile app disconnects
ble_peripheral_set_disconnect_callback([](ble_client_t* client) {
    printf("App disconnected\n");
});

// Called when app writes to ToRadio
ble_peripheral_set_toradio_callback([](ble_client_t* client, const uint8_t* data, uint16_t len) {
    // Process ToRadio protobuf message
});

// Called when app reads FromRadio
ble_peripheral_set_fromradio_callback([](ble_client_t* client, uint8_t* data, uint16_t max_len) -> uint16_t {
    // Fill buffer with FromRadio protobuf message
    // Return number of bytes written
    return 0;
});
```

### Sending Notifications

```cpp
// Notify app that new data is available
ble_peripheral_notify_fromnum();

// Or update counter and notify
ble_peripheral_update_fromnum(new_value);
```

### State Management

```cpp
// Check if app is connected
if (ble_peripheral_is_connected()) {
    // Handle connected state
}

// Get current state
ble_peripheral_state_t state = ble_peripheral_get_state();

// Disconnect from app
ble_peripheral_disconnect();
```

## Configuration

Key settings in `ble_peripheral.h`:

- `BLE_MAX_MTU` - Maximum MTU size (512)
- `BLE_ADV_INTERVAL_MS` - Advertising interval (100ms)
- `BLE_DEVICE_NAME_MAX` - Maximum device name length (32)
- `BLE_FROMRADIO_BUFFER_SIZE` - FromRadio buffer size (512)

## sdkconfig Requirements

Ensure these options are set in sdkconfig:

```
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512
```

## Dependencies

- ESP-IDF NimBLE stack
- FreeRTOS
- ESP32-S3 Bluetooth controller

## Notes

- The implementation is designed for ESP32-S3 with NimBLE stack
- Only one client connection is supported at a time
- Device will restart advertising after disconnect
- Compatible with Meshtastic app version 2.x and later
