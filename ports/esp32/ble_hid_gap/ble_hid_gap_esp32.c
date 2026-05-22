/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * NimBLE-only BLE HID GAP layer.
 * Bluedroid (classic BT + legacy BLE) paths removed — this project uses
 * CONFIG_BT_NIMBLE_ENABLED exclusively.
 */


#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "ble_hid_gap.h"
#include "ble_audio_stream.h"

#include "esp_bt.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_store.h"
#include "nimble/ble.h"
#include "host/ble_sm.h"

static const char *TAG = "ESP_HID_GAP";

extern void ble_hid_task_start_up(void);

static void ble_hid_gap_log_conn_desc(const char *context, uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ESP_LOGW(TAG, "%s: connection descriptor lookup failed: conn_handle=%u rc=%d", context, conn_handle, rc);
        return;
    }

    ESP_LOGI(
        TAG,
        "%s: conn_handle=%u interval_units=%u interval_ms=%.2f latency=%u supervision_timeout_units=%u supervision_timeout_ms=%u",
        context,
        conn_handle,
        desc.conn_itvl,
        (double)desc.conn_itvl * 1.25,
        desc.conn_latency,
        desc.supervision_timeout,
        desc.supervision_timeout * 10);
}

/*
 * NimBLE GAP
 */

#define GATT_SVR_SVC_HID_UUID 0x1812

static struct ble_hs_adv_fields s_adv_fields;
static struct ble_hs_adv_fields s_scan_rsp_fields;
static ble_uuid16_t s_hid_service_uuid = BLE_UUID16_INIT(GATT_SVR_SVC_HID_UUID);
static ble_uuid128_t s_audio_stream_service_uuid = BLE_AUDIO_STREAM_SERVICE_UUID;
static bool s_nimble_stack_ready = false;
static bool s_hid_start_event_seen = false;
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static bool s_directed_adv_pending = true;
static bool s_last_adv_was_directed = false;
static bool s_ble_gap_connected = false;
static uint16_t s_ble_gap_conn_handle = BLE_HS_CONN_HANDLE_NONE;

/*
 * Legacy advertising has a hard 31-byte payload limit. With flags,
 * appearance and one 16-bit HID UUID, the current 17-byte product name fits
 * exactly under that limit; longer names stay in scan response data.
 */
#define BLE_HID_ADV_NAME_MAX_LEN 17

esp_err_t esp_hid_ble_gap_adv_init(uint16_t appearance, const char *device_name)
{
    memset(&s_adv_fields, 0, sizeof(s_adv_fields));
    memset(&s_scan_rsp_fields, 0, sizeof(s_scan_rsp_fields));

    s_adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_adv_fields.appearance = appearance;
    s_adv_fields.appearance_is_present = 1;
    s_adv_fields.uuids16 = &s_hid_service_uuid;
    s_adv_fields.num_uuids16 = 1;
    s_adv_fields.uuids16_is_complete = 1;

    size_t device_name_len = strlen(device_name);
    bool name_in_adv = device_name_len <= BLE_HID_ADV_NAME_MAX_LEN;

    if (name_in_adv) {
        s_adv_fields.name = (uint8_t *)device_name;
        s_adv_fields.name_len = device_name_len;
        s_adv_fields.name_is_complete = 1;
    } else {
        s_scan_rsp_fields.name = (uint8_t *)device_name;
        s_scan_rsp_fields.name_len = device_name_len;
        s_scan_rsp_fields.name_is_complete = 1;
    }

    s_scan_rsp_fields.tx_pwr_lvl_is_present = 1;
    s_scan_rsp_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    s_scan_rsp_fields.uuids128 = &s_audio_stream_service_uuid;
    s_scan_rsp_fields.num_uuids128 = 1;
    s_scan_rsp_fields.uuids128_is_complete = 1;

    /* Initialize the security configuration */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

    ESP_LOGI(
        TAG,
        "NimBLE advertising configured: appearance=0x%04x name=%s name_in_adv=%s io_cap=%u bonding=%u mitm=%u sc=%u",
        appearance,
        device_name,
        name_in_adv ? "yes" : "scan_rsp",
        ble_hs_cfg.sm_io_cap,
        ble_hs_cfg.sm_bonding,
        ble_hs_cfg.sm_mitm,
        ble_hs_cfg.sm_sc);

    return ESP_OK;
}

static int
nimble_hid_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
        if (event->connect.status != 0) {
            s_directed_adv_pending = false;
            ble_hid_gap_start_advertising();
            return 0;
        }

        s_ble_gap_connected = true;
        s_ble_gap_conn_handle = event->connect.conn_handle;
        ble_audio_stream_on_gap_connect(event->connect.conn_handle);
        s_last_adv_was_directed = false;

        rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
        if (rc == 0) {
            ESP_LOGI(
                TAG,
                "security state before initiate: encrypted=%u authenticated=%u bonded=%u key_size=%u",
                desc.sec_state.encrypted,
                desc.sec_state.authenticated,
                desc.sec_state.bonded,
                desc.sec_state.key_size);
        } else {
            ESP_LOGW(TAG, "connection descriptor lookup failed before security initiate: rc=%d", rc);
        }

        rc = ble_gap_security_initiate(event->connect.conn_handle);
        if (rc == 0) {
            ESP_LOGI(TAG, "security initiate requested");
        } else if (rc == BLE_HS_EALREADY) {
            ESP_LOGI(TAG, "security already in progress");
        } else {
            ESP_LOGW(TAG, "security initiate failed: rc=%d", rc);
        }

        struct ble_gap_upd_params audio_params = {
            .itvl_min = 6,
            .itvl_max = 6,
            .latency = 0,
            .supervision_timeout = 800,
            .min_ce_len = 0,
            .max_ce_len = 0,
        };
        rc = ble_gap_update_params(event->connect.conn_handle, &audio_params);
        if (rc == 0) {
            ESP_LOGI(TAG, "audio connection parameter update requested");
        } else {
            ESP_LOGW(TAG, "audio connection parameter update failed: rc=%d", rc);
        }

        rc = ble_gap_set_prefered_le_phy(
            event->connect.conn_handle,
            BLE_GAP_LE_PHY_2M_MASK,
            BLE_GAP_LE_PHY_2M_MASK,
            0);
        if (rc == 0) {
            ESP_LOGI(TAG, "audio 2M PHY preference requested");
        } else {
            ESP_LOGW(TAG, "audio 2M PHY preference failed: rc=%d", rc);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
        s_ble_gap_connected = false;
        s_ble_gap_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ble_audio_stream_on_gap_disconnect(event->disconnect.conn.conn_handle);
        s_directed_adv_pending = true;
        s_last_adv_was_directed = false;
        ble_hid_gap_start_advertising();
        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        ESP_LOGI(TAG, "connection updated; status=%d",
                event->conn_update.status);
        ble_hid_gap_log_conn_desc("connection updated", event->conn_update.conn_handle);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "advertise complete; reason=%d",
                event->adv_complete.reason);
        if (s_last_adv_was_directed) {
            ESP_LOGI(TAG, "directed advertising completed; falling back to undirected advertising");
            s_last_adv_was_directed = false;
        }
        ble_hid_gap_start_advertising();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d "
                "reason=%d prevn=%d curn=%d previ=%d curi=%d",
                event->subscribe.conn_handle,
                event->subscribe.attr_handle,
                event->subscribe.reason,
                event->subscribe.prev_notify,
                event->subscribe.cur_notify,
                event->subscribe.prev_indicate,
                event->subscribe.cur_indicate);
        ble_audio_stream_on_gap_subscribe(
            event->subscribe.conn_handle,
            event->subscribe.attr_handle,
            event->subscribe.cur_notify,
            event->subscribe.cur_indicate);
        if (event->subscribe.attr_handle == ble_audio_stream_get_notify_attr_handle() &&
            event->subscribe.cur_notify != 0) {
            ble_hid_gap_log_conn_desc("audio notify subscribed", event->subscribe.conn_handle);
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d",
                event->mtu.conn_handle,
                event->mtu.channel_id,
                event->mtu.value);
        ble_audio_stream_on_gap_mtu(event->mtu.conn_handle, event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        ESP_LOGI(TAG, "encryption change event; status=%d", event->enc_change.status);
        if (event->enc_change.status == 0) {
            ble_svc_gatt_changed(0x0001, 0xffff);
            ESP_LOGI(TAG, "service changed indication queued for refreshed GATT discovery");
            rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
            if (rc == 0) {
                ESP_LOGI(
                    TAG,
                    "security state after encryption: encrypted=%u authenticated=%u bonded=%u key_size=%u",
                    desc.sec_state.encrypted,
                    desc.sec_state.authenticated,
                    desc.sec_state.bonded,
                    desc.sec_state.key_size);
            } else {
                ESP_LOGW(TAG, "connection descriptor lookup failed after encryption: rc=%d", rc);
            }
            ble_hid_task_start_up();
        } else {
            ESP_LOGW(TAG, "encryption failed or connection already gone; status=%d", event->enc_change.status);
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        ESP_LOGV(TAG, "notify_tx event; conn_handle=%d attr_handle=%d "
                "status=%d indication=%d",
                event->notify_tx.conn_handle,
                event->notify_tx.attr_handle,
                event->notify_tx.status,
                event->notify_tx.indication);
        ble_audio_stream_on_gap_notify_tx(
            event->notify_tx.conn_handle,
            event->notify_tx.attr_handle,
            event->notify_tx.status,
            event->notify_tx.indication != 0);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc != 0) {
            ESP_LOGW(TAG, "repeat pairing: conn find failed rc=%d; ignoring", rc);
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    }
    return 0;
}

static void nimble_hid_on_reset(int reason)
{
    s_nimble_stack_ready = false;
    ESP_LOGW(TAG, "NimBLE host reset; reason=%d", reason);
}

static void nimble_hid_on_sync(void)
{
    int rc;
    uint8_t addr_val[6] = {0};

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: rc=%d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(s_own_addr_type, addr_val, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_hs_id_copy_addr failed: rc=%d", rc);
    } else {
        ESP_LOGI(
            TAG,
            "NimBLE host sync complete: own_addr_type=%u addr=%02x:%02x:%02x:%02x:%02x:%02x",
            s_own_addr_type,
            addr_val[0],
            addr_val[1],
            addr_val[2],
            addr_val[3],
            addr_val[4],
            addr_val[5]);
    }

    s_nimble_stack_ready = true;
    s_directed_adv_pending = true;
    s_last_adv_was_directed = false;

    if (s_hid_start_event_seen) {
        ble_hid_gap_start_advertising();
    } else {
        ESP_LOGI(TAG, "NimBLE host synced before HID START; advertising deferred");
    }
}

esp_err_t esp_hid_ble_gap_adv_start(void)
{
    int rc;
    struct ble_gap_adv_params adv_params;
    ble_addr_t bonded_peers[4];
    int bonded_peer_count = 0;
    bool start_directed = false;
    ble_addr_t direct_peer_addr = {0};

    if (!s_hid_start_event_seen) {
        ESP_LOGI(TAG, "NimBLE advertising deferred: HID START not seen yet");
        return ESP_OK;
    }

    if (!s_nimble_stack_ready) {
        ESP_LOGI(TAG, "NimBLE advertising deferred: host stack not synced yet");
        return ESP_OK;
    }

    if (ble_gap_adv_active()) {
        ESP_LOGI(TAG, "NimBLE advertising already active");
        return ESP_OK;
    }

    rc = ble_gap_adv_set_fields(&s_adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gap_adv_rsp_set_fields(&s_scan_rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting scan response data; rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_store_util_bonded_peers(
        bonded_peers,
        &bonded_peer_count,
        sizeof(bonded_peers) / sizeof(bonded_peers[0]));
    if (rc == 0) {
        ESP_LOGI(TAG, "NimBLE bonded peers=%d", bonded_peer_count);
        if (bonded_peer_count > 0) {
            direct_peer_addr = bonded_peers[0];
            start_directed = s_directed_adv_pending;
        }
    } else {
        ESP_LOGW(TAG, "NimBLE bonded peer lookup failed: rc=%d", rc);
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    if (start_directed) {
        adv_params.conn_mode = BLE_GAP_CONN_MODE_DIR;
        adv_params.high_duty_cycle = 1;
        rc = ble_gap_adv_start(
            s_own_addr_type,
            &direct_peer_addr,
            BLE_HS_FOREVER,
            &adv_params,
            nimble_hid_gap_event,
            NULL);
        if (rc == 0) {
            s_directed_adv_pending = false;
            s_last_adv_was_directed = true;
            ESP_LOGI(
                TAG,
                "NimBLE directed advertising started: peer_type=%u peer_addr=%02x:%02x:%02x:%02x:%02x:%02x",
                direct_peer_addr.type,
                direct_peer_addr.val[0],
                direct_peer_addr.val[1],
                direct_peer_addr.val[2],
                direct_peer_addr.val[3],
                direct_peer_addr.val[4],
                direct_peer_addr.val[5]);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "directed advertising failed, fallback to undirected; rc=%d", rc);
    }

    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);/* Recommended interval 30ms to 50ms */
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, nimble_hid_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling undirected advertisement; rc=%d", rc);
        return ESP_FAIL;
    }

    s_last_adv_was_directed = false;
    ESP_LOGI(TAG, "NimBLE undirected advertising started");
    return ESP_OK;
}

/*
 * CONTROLLER INIT
 */

static esp_err_t init_low_level(uint8_t mode)
{
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
#if CONFIG_IDF_TARGET_ESP32
    bt_cfg.mode = mode;
#endif
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_mem_release failed: %d", ret);
        return ret;
    }
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %d", ret);
        return ret;
    }

    ret = esp_bt_controller_enable(mode);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %d", ret);
        return ret;
    }

    ret = esp_nimble_init();
    if (ret) {
        ESP_LOGE(TAG, "esp_nimble_init failed: %d", ret);
        return ret;
    }

    ble_hs_cfg.sync_cb = nimble_hid_on_sync;
    ble_hs_cfg.reset_cb = nimble_hid_on_reset;

    return ret;
}

esp_err_t esp_hid_gap_init(uint8_t mode)
{
    esp_err_t ret;
    if (!mode || mode > ESP_BT_MODE_BTDM) {
        ESP_LOGE(TAG, "Invalid mode given!");
        return ESP_FAIL;
    }

    ret = init_low_level(mode);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t ble_hid_gap_init(void)
{
    return esp_hid_gap_init(HIDD_BLE_MODE);
}

esp_err_t ble_hid_gap_configure_advertising(uint16_t appearance, const char *device_name)
{
    return esp_hid_ble_gap_adv_init(appearance, device_name);
}

esp_err_t ble_hid_gap_start_advertising(void)
{
    return esp_hid_ble_gap_adv_start();
}

esp_err_t ble_hid_gap_mark_stack_ready(void)
{
    s_hid_start_event_seen = true;
    if (!s_nimble_stack_ready) {
        ESP_LOGI(TAG, "HID START received; waiting for NimBLE host sync before advertising");
        return ESP_OK;
    }
    return ble_hid_gap_start_advertising();
}

esp_err_t ble_hid_gap_forget_bonds_and_repair(void)
{
    ble_addr_t bonded_peers[8];
    int bonded_peer_count = 0;
    int first_error = 0;
    int rc = ble_store_util_bonded_peers(
        bonded_peers,
        &bonded_peer_count,
        sizeof(bonded_peers) / sizeof(bonded_peers[0]));
    if (rc != 0) {
        ESP_LOGE(TAG, "recovery: bonded peer lookup failed rc=%d", rc);
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "recovery: clearing pairing bonds count=%d", bonded_peer_count);
    for (int index = 0; index < bonded_peer_count; ++index) {
        rc = ble_store_util_delete_peer(&bonded_peers[index]);
        if (rc != 0) {
            ESP_LOGE(TAG, "recovery: delete peer index=%d failed rc=%d", index, rc);
            if (first_error == 0) {
                first_error = rc;
            }
        }
    }

    s_directed_adv_pending = false;
    s_last_adv_was_directed = false;

    if (s_ble_gap_connected && s_ble_gap_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        rc = ble_gap_terminate(s_ble_gap_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc == 0) {
            ESP_LOGW(TAG, "recovery: active BLE connection terminating for re-pair; advertising restarts after disconnect");
            return first_error == 0 ? ESP_OK : ESP_FAIL;
        } else {
            ESP_LOGW(TAG, "recovery: BLE terminate failed rc=%d; advertising restart will continue", rc);
        }
    } else if (ble_gap_adv_active()) {
        rc = ble_gap_adv_stop();
        if (rc != 0) {
            ESP_LOGW(TAG, "recovery: advertising stop failed rc=%d; restart will continue", rc);
        }
    }

    esp_err_t adv_ret = ble_hid_gap_start_advertising();
    if (adv_ret != ESP_OK) {
        ESP_LOGE(TAG, "recovery: advertising restart failed: %s", esp_err_to_name(adv_ret));
        return adv_ret;
    }

    ESP_LOGW(TAG, "recovery: pairing reset complete, device is discoverable for first-time pairing");
    return first_error == 0 ? ESP_OK : ESP_FAIL;
}
