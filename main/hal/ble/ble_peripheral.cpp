/**
 * @file ble_peripheral.cpp
 * @author d4rkmen
 * @brief BLE Peripheral implementation for Meshtastic mobile app connectivity
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "ble_peripheral.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "nvs_flash.h"
#include <string.h>

static const char* TAG = "BLE_PERIPH";

// Global peripheral context
static ble_peripheral_ctx_t g_ctx = {
    .state = BLE_PERIPH_STATE_IDLE,
    .client = {0},
    .device_name = "Meshtastic",
    .pairing_pin = 0,
    .require_pin = false,
    .fromradio_buffer = {0},
    .fromradio_len = 0,
    .fromradio_num = 0,
    .on_connect = NULL,
    .on_disconnect = NULL,
    .on_toradio = NULL,
    .on_fromradio = NULL,
};

// Characteristic value handles (filled by NimBLE)
static uint16_t g_toradio_handle = 0;
static uint16_t g_fromradio_handle = 0;
static uint16_t g_fromnum_handle = 0;
static uint16_t g_logradio_handle = 0;

// Meshtastic Service UUID (128-bit, little-endian)
static const ble_uuid128_t meshtastic_svc_uuid =
    BLE_UUID128_INIT(0xfd, 0xea, 0x73, 0xe2, 0xca, 0x5d, 0xa8, 0x9f, 0x1f, 0x46, 0xa8, 0x15, 0x18, 0xb2, 0xa1, 0x6b);

// ToRadio Characteristic UUID (Write)
static const ble_uuid128_t toradio_chr_uuid =
    BLE_UUID128_INIT(0xe7, 0x01, 0x44, 0x12, 0x66, 0x78, 0xdd, 0xa1, 0xad, 0x4d, 0x9e, 0x12, 0xd2, 0x76, 0x5c, 0xf7);

// FromRadio Characteristic UUID (Read)
// UUID string: 2c55e69e-4993-11ed-b878-0242ac120002
// BLE little-endian format: entire UUID byte sequence reversed
// Original: 2c 55 e6 9e 49 93 11 ed b8 78 02 42 ac 12 00 02
// Reversed: 02 00 12 ac 42 02 78 b8 ed 11 93 49 9e e6 55 2c
static const ble_uuid128_t fromradio_chr_uuid =
    BLE_UUID128_INIT(0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x78, 0xb8, 0xed, 0x11, 0x93, 0x49, 0x9e, 0xe6, 0x55, 0x2c);

// FromNum Characteristic UUID (Notify)
static const ble_uuid128_t fromnum_chr_uuid =
    BLE_UUID128_INIT(0x53, 0x44, 0xe3, 0x47, 0x75, 0xaa, 0x70, 0xa6, 0x66, 0x4f, 0x00, 0xa8, 0x8c, 0xa1, 0x9d, 0xed);

// LogRadio Characteristic UUID (Notify, optional)
// UUID string: 5a3d6e49-06e6-4423-9944-e9de8cdf9547
// BLE little-endian format: entire UUID byte sequence reversed
// Original: 5a 3d 6e 49 06 e6 44 23 99 44 e9 de 8c df 95 47
// Reversed: 47 95 df 8c de e9 44 99 23 44 e6 06 49 6e 3d 5a
static const ble_uuid128_t logradio_chr_uuid =
    BLE_UUID128_INIT(0x47, 0x95, 0xdf, 0x8c, 0xde, 0xe9, 0x44, 0x99, 0x23, 0x44, 0xe6, 0x06, 0x49, 0x6e, 0x3d, 0x5a);

// Forward declarations
static int ble_gap_event_handler(struct ble_gap_event* event, void* arg);
static int gatt_toradio_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg);
static int gatt_fromradio_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg);
static int gatt_fromnum_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg);
static int gatt_logradio_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg);
static int gatt_svr_init(void);

// Characteristics array (must be static for handles to be populated correctly)
static struct ble_gatt_chr_def meshtastic_chrs[] = {
    {
        // ToRadio - Write characteristic
        .uuid = &toradio_chr_uuid.u,
        .access_cb = gatt_toradio_access,
        .arg = NULL,
        .descriptors = NULL,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = &g_toradio_handle,
        .cpfd = NULL,
    },
    {
        // FromRadio - Read characteristic
        .uuid = &fromradio_chr_uuid.u,
        .access_cb = gatt_fromradio_access,
        .arg = NULL,
        .descriptors = NULL,
        .flags = BLE_GATT_CHR_F_READ,
        .min_key_size = 0,
        .val_handle = &g_fromradio_handle,
        .cpfd = NULL,
    },
    {
        // FromNum - Notify characteristic
        .uuid = &fromnum_chr_uuid.u,
        .access_cb = gatt_fromnum_access,
        .arg = NULL,
        .descriptors = NULL,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_fromnum_handle,
        .cpfd = NULL,
    },
    {
        // LogRadio - Notify characteristic (optional debug)
        .uuid = &logradio_chr_uuid.u,
        .access_cb = gatt_logradio_access,
        .arg = NULL,
        .descriptors = NULL,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_logradio_handle,
        .cpfd = NULL,
    },
    {
        0, // End of characteristics
    },
};

// GATT service definition
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &meshtastic_svc_uuid.u,
        .characteristics = meshtastic_chrs,
    },
    {
        0, // End of services
    },
};

// ToRadio write handler
static int gatt_toradio_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > BLE_TORADIO_BUFFER_SIZE)
        {
            ESP_LOGW(TAG, "ToRadio data too large: %d bytes", len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        uint8_t buffer[BLE_TORADIO_BUFFER_SIZE];
        uint16_t copied = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buffer, len, &copied);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "Failed to read ToRadio data: %d", rc);
            return BLE_ATT_ERR_UNLIKELY;
        }

        ESP_LOGI(TAG, "ToRadio received %d bytes", copied);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, copied, ESP_LOG_DEBUG);

        // Call callback if set
        if (g_ctx.on_toradio)
        {
            g_ctx.on_toradio(&g_ctx.client, buffer, copied);
        }

        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// FromRadio read handler
static int gatt_fromradio_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        ESP_LOGD(TAG, "FromRadio read request");

        // If callback is set, use it to get fresh data
        if (g_ctx.on_fromradio)
        {
            g_ctx.fromradio_len = g_ctx.on_fromradio(&g_ctx.client, g_ctx.fromradio_buffer, sizeof(g_ctx.fromradio_buffer));
        }

        if (g_ctx.fromradio_len > 0)
        {
            ESP_LOGI(TAG, "FromRadio sending %d bytes", g_ctx.fromradio_len);
            int rc = os_mbuf_append(ctxt->om, g_ctx.fromradio_buffer, g_ctx.fromradio_len);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "Failed to append FromRadio data: %d", rc);
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            // Clear buffer after read
            g_ctx.fromradio_len = 0;
        }
        else
        {
            // Return empty response - characteristic exists but no data available
            ESP_LOGD(TAG, "FromRadio read: no data available");
        }

        return 0;
    }

    ESP_LOGW(TAG, "FromRadio: unexpected operation %d", ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

// FromNum read/notify handler
static int gatt_fromnum_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        // Return current fromradio_num as 4-byte little-endian value
        int rc = os_mbuf_append(ctxt->om, &g_ctx.fromradio_num, sizeof(g_ctx.fromradio_num));
        if (rc != 0)
        {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// LogRadio handler (optional debug logging)
static int gatt_logradio_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        // LogRadio is optional, return empty for now
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// Start advertising
static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    // Advertising flags
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // Include 128-bit service UUID
    fields.uuids128 = &meshtastic_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set adv fields: %d", rc);
        return;
    }

    // Scan response with device name
    rsp_fields.name = (uint8_t*)g_ctx.device_name;
    rsp_fields.name_len = strlen(g_ctx.device_name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set scan response: %d", rc);
        return;
    }

    // Set advertising parameters
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(BLE_ADV_INTERVAL_MS);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(BLE_ADV_INTERVAL_MS + 50);

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event_handler, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        g_ctx.state = BLE_PERIPH_STATE_ERROR;
        return;
    }

    g_ctx.state = BLE_PERIPH_STATE_ADVERTISING;
    ESP_LOGI(TAG, "Advertising started as '%s'", g_ctx.device_name);
}

// GAP event handler
static int ble_gap_event_handler(struct ble_gap_event* event, void* arg)
{
    struct ble_gap_conn_desc desc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; status=%d", event->connect.status == 0 ? "established" : "failed", event->connect.status);

        if (event->connect.status == 0)
        {
            // Connection established
            int rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc == 0)
            {
                g_ctx.client.conn_handle = event->connect.conn_handle;
                memcpy(g_ctx.client.addr, desc.peer_id_addr.val, 6);
                g_ctx.client.addr_type = desc.peer_id_addr.type;
                g_ctx.client.mtu = BLE_ATT_MTU_DFLT;
                g_ctx.client.subscribed_fromnum = false;
                g_ctx.client.authenticated = !g_ctx.require_pin;
                g_ctx.state = BLE_PERIPH_STATE_CONNECTED;

                ESP_LOGI(TAG,
                         "Client connected: %02x:%02x:%02x:%02x:%02x:%02x",
                         g_ctx.client.addr[5],
                         g_ctx.client.addr[4],
                         g_ctx.client.addr[3],
                         g_ctx.client.addr[2],
                         g_ctx.client.addr[1],
                         g_ctx.client.addr[0]);

                // Request higher MTU
                ble_att_set_preferred_mtu(BLE_MAX_MTU);
                ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);

                // Call callback
                if (g_ctx.on_connect)
                {
                    g_ctx.on_connect(&g_ctx.client);
                }
            }
        }
        else
        {
            // Connection failed, restart advertising
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Client disconnected; reason=%d", event->disconnect.reason);

        g_ctx.state = BLE_PERIPH_STATE_IDLE;

        // Call callback
        if (g_ctx.on_disconnect)
        {
            g_ctx.on_disconnect(&g_ctx.client);
        }

        // Clear client info
        memset(&g_ctx.client, 0, sizeof(g_ctx.client));

        // Restart advertising
        start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete");
        if (g_ctx.state == BLE_PERIPH_STATE_ADVERTISING)
        {
            // Restart advertising if we weren't connected
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update: conn_handle=%d, mtu=%d", event->mtu.conn_handle, event->mtu.value);
        g_ctx.client.mtu = event->mtu.value;
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG,
                 "Subscribe event: conn_handle=%d, attr_handle=%d, "
                 "cur_notify=%d, cur_indicate=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify,
                 event->subscribe.cur_indicate);

        if (event->subscribe.attr_handle == g_fromnum_handle)
        {
            g_ctx.client.subscribed_fromnum = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "FromNum notifications %s", g_ctx.client.subscribed_fromnum ? "enabled" : "disabled");
        }
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(TAG, "Passkey action: %d", event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_DISP)
        {
            // Display passkey
            struct ble_sm_io pk;
            pk.action = event->passkey.params.action;
            pk.passkey = g_ctx.pairing_pin;
            ESP_LOGI(TAG, "Displaying passkey: %06lu", (unsigned long)pk.passkey);
            ble_sm_inject_io(event->passkey.conn_handle, &pk);
        }
        else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP)
        {
            // Numeric comparison - auto accept for now
            struct ble_sm_io pk;
            pk.action = event->passkey.params.action;
            pk.numcmp_accept = 1;
            ble_sm_inject_io(event->passkey.conn_handle, &pk);
        }
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption change: status=%d", event->enc_change.status);
        if (event->enc_change.status == 0)
        {
            g_ctx.client.authenticated = true;
        }
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        // Delete old bond and allow re-pairing
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        break;
    }

    return 0;
}

// BLE host task
static void ble_host_task(void* param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// Host reset callback
static void ble_on_reset(int reason) { ESP_LOGE(TAG, "BLE host reset: reason=%d", reason); }

// Host sync callback - called when BLE stack is ready
static void ble_on_sync(void)
{
    int rc;

    ESP_LOGI(TAG, "BLE host synchronized");

    // Ensure we have a valid identity address
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to ensure address: %d", rc);
        return;
    }

    // Configure security manager
    ble_hs_cfg.sm_io_cap = g_ctx.require_pin ? BLE_SM_IO_CAP_DISP_ONLY : BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = g_ctx.require_pin ? 1 : 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    // Start advertising
    start_advertising();
}

// Initialize GATT server
static int gatt_svr_init(void)
{
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to count GATT services: %d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
        return rc;
    }

    // Log registered characteristics for debugging
    // Note: Handles are populated by ble_gatts_add_svcs() synchronously
    ESP_LOGI(TAG, "GATT service registered with characteristics:");
    ESP_LOGI(TAG, "  ToRadio handle: %d (0x%04x)", g_toradio_handle, g_toradio_handle);
    ESP_LOGI(TAG, "  FromRadio handle: %d (0x%04x)", g_fromradio_handle, g_fromradio_handle);
    ESP_LOGI(TAG, "  FromNum handle: %d (0x%04x)", g_fromnum_handle, g_fromnum_handle);
    ESP_LOGI(TAG, "  LogRadio handle: %d (0x%04x)", g_logradio_handle, g_logradio_handle);

    // Log UUIDs for debugging
    ESP_LOGI(TAG, "Characteristic UUIDs:");
    ESP_LOGI(TAG, "  ToRadio: f75c76d2-129e-4dad-a1dd-7866124401e7");
    ESP_LOGI(TAG, "  FromRadio: 2c55e69e-4993-11ed-b878-0242ac120002");
    ESP_LOGI(TAG, "  FromNum: ed9da18c-a800-4f66-a670-aa75e347e353");

    // Check if handles were populated
    if (g_toradio_handle == 0 && g_fromradio_handle == 0 && g_fromnum_handle == 0 && g_logradio_handle == 0)
    {
        ESP_LOGW(TAG, "WARNING: All characteristic handles are 0 - they may be populated asynchronously");
        // Don't return error - handles might be populated later when host syncs
    }
    else if (g_fromradio_handle == 0)
    {
        ESP_LOGW(TAG, "WARNING: FromRadio characteristic handle is 0");
    }
    else
    {
        ESP_LOGI(TAG, "All characteristics registered successfully");
    }

    return 0;
}

// Public API implementations

int ble_peripheral_init(const char* device_name, uint32_t pin)
{
    int rc;

    ESP_LOGI(TAG, "Initializing BLE Peripheral");

    // Store configuration
    if (device_name && strlen(device_name) > 0)
    {
        strncpy(g_ctx.device_name, device_name, sizeof(g_ctx.device_name) - 1);
        g_ctx.device_name[sizeof(g_ctx.device_name) - 1] = '\0';
    }

    g_ctx.pairing_pin = pin;
    g_ctx.require_pin = (pin > 0);

    esp_err_t ret;
#if 0
    // Initialize NVS (if not already)
    // Note: NVS may already be initialized by Settings class
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition needs erasing, erasing...");
        ret = nvs_flash_erase();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
            return -1;
        }
        ret = nvs_flash_init();
    }
    // ESP_ERR_INVALID_STATE means NVS is already initialized (e.g., by Settings)
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return -1;
    }
    if (ret == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGD(TAG, "NVS already initialized");
    }
#endif
    // Initialize NimBLE
    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init NimBLE: %d", ret);
        return -1;
    }

    // Initialize GATT server
    rc = gatt_svr_init();
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to init GATT server: %d", rc);
        return -1;
    }

    // Set device name for GAP
    ble_svc_gap_device_name_set(g_ctx.device_name);

    // Configure host callbacks
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Start BLE host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE Peripheral initialized");
    return 0;
}

void ble_peripheral_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BLE Peripheral");

    // Stop advertising if active
    if (g_ctx.state == BLE_PERIPH_STATE_ADVERTISING)
    {
        ble_gap_adv_stop();
    }

    // Disconnect if connected
    if (g_ctx.state == BLE_PERIPH_STATE_CONNECTED)
    {
        ble_gap_terminate(g_ctx.client.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    // Deinitialize NimBLE
    nimble_port_stop();
    nimble_port_deinit();

    g_ctx.state = BLE_PERIPH_STATE_IDLE;
    memset(&g_ctx.client, 0, sizeof(g_ctx.client));
}

int ble_peripheral_start_advertising(void)
{
    if (g_ctx.state == BLE_PERIPH_STATE_ADVERTISING)
    {
        return 0; // Already advertising
    }

    if (g_ctx.state == BLE_PERIPH_STATE_CONNECTED)
    {
        ESP_LOGW(TAG, "Cannot advertise while connected");
        return -1;
    }

    start_advertising();
    return (g_ctx.state == BLE_PERIPH_STATE_ADVERTISING) ? 0 : -1;
}

int ble_peripheral_stop_advertising(void)
{
    if (g_ctx.state != BLE_PERIPH_STATE_ADVERTISING)
    {
        return 0;
    }

    int rc = ble_gap_adv_stop();
    if (rc == 0)
    {
        g_ctx.state = BLE_PERIPH_STATE_IDLE;
    }
    return rc;
}

int ble_peripheral_disconnect(void)
{
    if (g_ctx.state != BLE_PERIPH_STATE_CONNECTED)
    {
        return 0;
    }

    return ble_gap_terminate(g_ctx.client.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

bool ble_peripheral_is_connected(void) { return g_ctx.state == BLE_PERIPH_STATE_CONNECTED; }

ble_peripheral_state_t ble_peripheral_get_state(void) { return g_ctx.state; }

ble_client_t* ble_peripheral_get_client(void)
{
    if (g_ctx.state == BLE_PERIPH_STATE_CONNECTED)
    {
        return &g_ctx.client;
    }
    return NULL;
}

int ble_peripheral_set_fromradio(const uint8_t* data, uint16_t len)
{
    if (len > sizeof(g_ctx.fromradio_buffer))
    {
        ESP_LOGE(TAG, "FromRadio data too large: %d", len);
        return -1;
    }

    memcpy(g_ctx.fromradio_buffer, data, len);
    g_ctx.fromradio_len = len;
    return 0;
}

int ble_peripheral_notify_fromnum(void)
{
    if (g_ctx.state != BLE_PERIPH_STATE_CONNECTED)
    {
        return -1;
    }

    if (!g_ctx.client.subscribed_fromnum)
    {
        ESP_LOGD(TAG, "Client not subscribed to FromNum notifications");
        return 0;
    }

    // Increment notification counter
    g_ctx.fromradio_num++;

    // Create mbuf with the counter value
    struct os_mbuf* om = ble_hs_mbuf_from_flat(&g_ctx.fromradio_num, sizeof(g_ctx.fromradio_num));
    if (om == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate mbuf for notification");
        return -1;
    }

    int rc = ble_gatts_notify_custom(g_ctx.client.conn_handle, g_fromnum_handle, om);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to send notification: %d", rc);
        return -1;
    }

    ESP_LOGD(TAG, "FromNum notification sent: %lu", (unsigned long)g_ctx.fromradio_num);
    return 0;
}

int ble_peripheral_update_fromnum(uint32_t num)
{
    g_ctx.fromradio_num = num;
    return ble_peripheral_notify_fromnum();
}

void ble_peripheral_set_connect_callback(ble_on_connect_cb_t callback) { g_ctx.on_connect = callback; }

void ble_peripheral_set_disconnect_callback(ble_on_disconnect_cb_t callback) { g_ctx.on_disconnect = callback; }

void ble_peripheral_set_toradio_callback(ble_on_toradio_cb_t callback) { g_ctx.on_toradio = callback; }

void ble_peripheral_set_fromradio_callback(ble_on_fromradio_cb_t callback) { g_ctx.on_fromradio = callback; }

void ble_peripheral_set_device_name(const char* name)
{
    if (name && strlen(name) > 0)
    {
        strncpy(g_ctx.device_name, name, sizeof(g_ctx.device_name) - 1);
        g_ctx.device_name[sizeof(g_ctx.device_name) - 1] = '\0';
        ble_svc_gap_device_name_set(g_ctx.device_name);

        // Restart advertising with new name if currently advertising
        if (g_ctx.state == BLE_PERIPH_STATE_ADVERTISING)
        {
            ESP_LOGI(TAG, "Updating advertising with new device name: %s", g_ctx.device_name);
            ble_gap_adv_stop();
            start_advertising();
        }
    }
}

void ble_peripheral_set_pin(uint32_t pin)
{
    g_ctx.pairing_pin = pin;
    g_ctx.require_pin = (pin > 0);

    // Update security configuration
    ble_hs_cfg.sm_io_cap = g_ctx.require_pin ? BLE_SM_IO_CAP_DISP_ONLY : BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_mitm = g_ctx.require_pin ? 1 : 0;
}
