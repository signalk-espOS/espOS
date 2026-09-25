/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bluedroid backend: controller bring-up, GAP scanning, advertisement
 * extraction.
 *
 * One file covers both targets because only the controller differs. On the
 * ESP32-P4 there is no local radio at all: Bluedroid's HCI is routed at an
 * ESP32-C6 co-processor over esp_hosted's SDIO transport. Everything above
 * HCI - GAP, GATT, the scan loop - is identical.
 */

#include "sdkconfig.h"

#if defined(CONFIG_BT_BLUEDROID_ENABLED)

#include <string.h>

#include "ble_gattc.h"
#include "ble_proto.h"
#include "ble_types.h"
#include "esp_bt_device.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "esp_timer.h"

#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID)
#include "esp_hosted.h"
#include "esp_hosted_bluedroid.h"
#else
#include "esp_bt.h"
#endif

static const char *TAG = "espos_ble_backend";

static espos_ble_callbacks_t s_cb;
static bool s_scanning;
/* Whether controller_up() has succeeded. Not merely a fast path: the controller
 * may be started before the network (see espos_ble_reserve_controller) and
 * esp_bt_controller_init() on a running controller is an error, not a no-op.
 *
 * Untested by the host suite, and not for want of trying: espos_ble_test
 * compiles ble_proto.c alone because everything in this file needs IDF's
 * Bluetooth stack, which does not exist on the linux target. Covering the three
 * states this flag creates (reserve twice, start after reserve, retry after a
 * failed enable) needs a fake controller behind a seam this component does not
 * have. Verified on hardware instead -- an ESP32-C5, reserve then start -- which
 * is the weaker check, so treat this flag as the delicate part of the file. */
static bool s_controller_up;
/* A scan was stopped while it was still arming. Cleared by the next deliberate
 * start; see the SCAN_PARAM_SET_COMPLETE_EVT case. */
static bool s_scan_inhibited;
static uint32_t s_scan_hits;
static char s_mac[ESPOS_BLE_ADDR_LEN];
static esp_ble_scan_params_t s_scan_params;

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    /* Setting scan parameters only arms the scan; it starts here, once the
     * controller acknowledges them.
     *
     * Which makes starting a two-step, asynchronous affair, and leaves a
     * window: a suspension that arrives between espos_ble_scan_start() and
     * this event would be ignored, because espos_ble_scan_stop() sees
     * s_scanning still false and returns without doing anything -- and then
     * the scan starts here anyway. On an unconfigured device that window is
     * where the setup portal lives: the scanner took half the airtime for the
     * whole session while the log said "scanning suspended". Observed as
     * "scanning suspended" at 4450 ms followed by "scanning" at 4458 ms. */
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "scan param set failed: %d", param->scan_param_cmpl.status);
            break;
        }
        if (s_scan_inhibited) {
            ESP_LOGI(TAG, "scan armed but suspended meanwhile; not starting");
            break;
        }
        esp_ble_gap_start_scanning(0); /* 0 = until stopped */
        break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "scan start failed: %d", param->scan_start_cmpl.status);
            s_scanning = false;
        } else {
            s_scanning = true;
            ESP_LOGI(TAG, "scanning");
        }
        break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        s_scanning = false;
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;
        s_scan_hits++;
        if (!s_cb.on_advertisement) break;

        espos_ble_adv_t adv;
        memset(&adv, 0, sizeof(adv));
        snprintf(adv.address, sizeof(adv.address),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 param->scan_rst.bda[0], param->scan_rst.bda[1],
                 param->scan_rst.bda[2], param->scan_rst.bda[3],
                 param->scan_rst.bda[4], param->scan_rst.bda[5]);
        adv.rssi = param->scan_rst.rssi;
        adv.received_us = esp_timer_get_time();

        /* Sum in a wider type before clamping: each field is at most 31, so
         * the total fits today, but adding them in a uint8_t would wrap
         * silently if either ever grew. */
        uint16_t total = (uint16_t)param->scan_rst.adv_data_len +
                         (uint16_t)param->scan_rst.scan_rsp_len;
        if (total > ESPOS_BLE_ADV_DATA_MAX) total = ESPOS_BLE_ADV_DATA_MAX;
        memcpy(adv.adv_data, param->scan_rst.ble_adv, total);
        adv.adv_data_len = (uint8_t)total;

        /* The bounded variant: the unbounded esp_ble_resolve_adv_data() walks
         * the payload with no length to stop at, and IDF 6 deprecates it for
         * exactly that reason. Fall back to the shortened name, which is what
         * a device advertises when the full one does not fit in 31 bytes. */
        uint8_t name_len = 0;
        uint16_t adv_len = total;
        uint8_t *name = esp_ble_resolve_adv_data_by_type(
            param->scan_rst.ble_adv, adv_len, ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
        if (!name || !name_len) {
            name = esp_ble_resolve_adv_data_by_type(
                param->scan_rst.ble_adv, adv_len, ESP_BLE_AD_TYPE_NAME_SHORT,
                &name_len);
        }
        if (name && name_len) {
            if (name_len >= ESPOS_BLE_NAME_MAX) name_len = ESPOS_BLE_NAME_MAX - 1;
            memcpy(adv.name, name, name_len);
            adv.name[name_len] = '\0';
        }

        /* Runs on the BT stack task: the callback must not block. */
        s_cb.on_advertisement(&adv, s_cb.arg);
        break;
    }

    default:
        break;
    }
}

static void gattc_trampoline(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                             esp_ble_gattc_cb_param_t *param)
{
    espos_ble_gattc_event(event, gattc_if, param);
}

static esp_err_t controller_up(void)
{
#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID)
    /* ESP32-P4: the controller lives on the C6. Order is load-bearing -
     * enabling the remote controller is what populates the VHCI driver's
     * function pointers, so attaching the HCI driver first faults on the
     * first call through them (verified 2026-08-21). */
    ESP_RETURN_ON_ERROR(esp_hosted_init(), TAG, "esp_hosted_init");
    ESP_RETURN_ON_ERROR(esp_hosted_connect_to_slave(), TAG, "connect_to_slave");
    ESP_RETURN_ON_ERROR(esp_hosted_bt_controller_init(), TAG, "bt_controller_init");
    ESP_RETURN_ON_ERROR(esp_hosted_bt_controller_enable(), TAG, "bt_controller_enable");

    hosted_hci_bluedroid_open();
    esp_bluedroid_hci_driver_operations_t ops = {
        .send = hosted_hci_bluedroid_send,
        .check_send_available = hosted_hci_bluedroid_check_send_available,
        .register_host_callback = hosted_hci_bluedroid_register_host_callback,
    };
    ESP_RETURN_ON_ERROR(esp_bluedroid_attach_hci_driver(&ops), TAG, "attach_hci");
#else
    /* Native controller. Classic BT memory is released because this is a
     * BLE-only gateway and that RAM is scarce. */
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    /* Say what ran out, not just that something did. The controller answers
     * ESP_ERR_NO_MEM (the ROM prints it as "r_ble_controller_init failed 257",
     * which is 0x101) when it cannot get its ~24 KB in one piece, and the
     * number that decides that is the LARGEST FREE BLOCK, not the total --
     * measured on a C5 at 33 KB free / 16 KB largest, failing without
     * allocating a byte (espOS #127). Without both numbers in the log this
     * reads as a radio fault and invites freeing total heap, which cannot
     * help. */
    esp_err_t cerr = esp_bt_controller_init(&cfg);
    if (cerr != ESP_OK) {
        ESP_LOGE(TAG, "bt_controller_init: %s -- internal heap %u B free, largest block %u B; "
                      "the controller needs ~24 KB CONTIGUOUS",
                 esp_err_to_name(cerr),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        return cerr;
    }

    /* Hand back an initialised-but-not-enabled controller rather than leaving
     * one behind. init succeeding and enable failing is a plausible split on a
     * part this tight, and the caller's retry would then call
     * esp_bt_controller_init() on a live controller, which answers
     * ESP_ERR_INVALID_STATE -- so one transient enable failure would wedge BLE
     * until reboot. Deinit puts it back where the retry expects it. */
    esp_err_t eerr = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (eerr != ESP_OK) {
        ESP_LOGE(TAG, "bt_controller_enable: %s", esp_err_to_name(eerr));
        esp_err_t derr = esp_bt_controller_deinit();
        if (derr != ESP_OK) {
            /* Nothing useful left to do, but say so: a later retry will fail on
             * init and this line is what explains why. */
            ESP_LOGE(TAG, "bt_controller_deinit after a failed enable: %s", esp_err_to_name(derr));
        }
        return eerr;
    }
#endif
    return ESP_OK;
}

esp_err_t espos_ble_backend_controller_only(void)
{
    /* Just the radio, and nothing above it. This is what reserving the
     * controller's ~24 KB means: claim the block, leave the Bluedroid host and
     * its BTU/BTC threads for later. Starting the host here would also start
     * those threads, whose stacks come from the same internal RAM the reserve
     * exists to protect -- and on a C5 that is what tipped the rest of the
     * firmware over. */
    if (s_controller_up) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(controller_up(), TAG, "controller");
    s_controller_up = true;
    return ESP_OK;
}

esp_err_t espos_ble_backend_init(const espos_ble_callbacks_t *cb)
{
    if (cb) s_cb = *cb;

    /* The controller may already be up: espos_ble_reserve_controller() can have
     * run minutes earlier, before the network. Re-initialising a live
     * controller is an error rather than a no-op, so skip it and carry on with
     * the host, which is what the gateway actually needs from this call. */
    if (!s_controller_up) {
        ESP_RETURN_ON_ERROR(controller_up(), TAG, "controller");
        s_controller_up = true;
    }

    /* Bluedroid is NOT skipped on a second call, because a reserve deliberately
     * never started it -- see espos_ble_backend_controller_only(). Guarding on
     * its own state anyway: esp_bluedroid_init() answers ESP_ERR_INVALID_STATE
     * on an already-initialised host, and under ESP_RETURN_ON_ERROR that would
     * fail espos_ble_start() outright and leave the gateway never scanning. A
     * stack that is already up is success here, not failure. */
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        ESP_RETURN_ON_ERROR(esp_bluedroid_init(), TAG, "bluedroid_init");
    }
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
        ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), TAG, "bluedroid_enable");
    }
    ESP_RETURN_ON_ERROR(esp_ble_gap_register_callback(gap_cb), TAG, "gap_register");

    const uint8_t *mac = esp_bt_dev_get_address();
    if (mac) {
        snprintf(s_mac, sizeof(s_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ESP_LOGI(TAG, "BT MAC %s", s_mac);
    }

#ifdef CONFIG_BT_GATTC_ENABLE
    ESP_RETURN_ON_ERROR(esp_ble_gattc_register_callback(gattc_trampoline), TAG,
                        "gattc_register");
    ESP_RETURN_ON_ERROR(espos_ble_gattc_init(cb), TAG, "gattc_init");
#endif
    return ESP_OK;
}

esp_err_t espos_ble_backend_deinit(void)
{
    /* Deliberately does not tear Bluedroid down: IDF's deinit paths for
     * stateful subsystems do not reliably return to a state a later init can
     * build on, and a gateway that cannot re-init its radio is worse than one
     * that leaves it running. */
    espos_ble_scan_stop();
    return ESP_OK;
}

/* Scan interval/window are in 0.625 ms units, clamped to the spec range. */
static uint16_t ms_to_units(uint32_t ms)
{
    uint32_t units = (ms * 1000U) / 625U;
    if (units < 0x0004) units = 0x0004;
    if (units > 0x4000) units = 0x4000;
    return (uint16_t)units;
}

esp_err_t espos_ble_scan_start(bool active, uint16_t interval_ms, uint16_t window_ms)
{
    s_scan_inhibited = false;
    if (window_ms > interval_ms) window_ms = interval_ms;

    s_scan_params.scan_type = active ? BLE_SCAN_TYPE_ACTIVE : BLE_SCAN_TYPE_PASSIVE;
    s_scan_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    s_scan_params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    s_scan_params.scan_interval = ms_to_units(interval_ms);
    s_scan_params.scan_window = ms_to_units(window_ms);
    /* Duplicates are wanted: the server times devices out on silence, so a
     * beacon that never changes must keep being reported. */
    s_scan_params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;

    return esp_ble_gap_set_scan_params(&s_scan_params);
}

esp_err_t espos_ble_scan_stop(void)
{
    /* Set before the early return: a scan that has been ARMED but has not yet
     * reported SCAN_START_COMPLETE has s_scanning == false, and without this
     * flag the pending start would run on regardless of having been stopped. */
    s_scan_inhibited = true;
    if (!s_scanning) return ESP_OK;
    return esp_ble_gap_stop_scanning();
}

esp_err_t espos_ble_gap_reclaim(void)
{
    /* Cheap and idempotent, so callers do not have to track who holds it.
     * esp_ble_gap_get_callback() is the only way to know: registering is
     * silent whether or not it displaced someone. */
    if (esp_ble_gap_get_callback() == gap_cb) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "GAP callback was taken by another component; reclaiming");
    return esp_ble_gap_register_callback(gap_cb);
}

bool espos_ble_is_scanning(void) { return s_scanning; }
uint32_t espos_ble_scan_hits(void) { return s_scan_hits; }
const char *espos_ble_mac(void) { return s_mac; }

#endif /* CONFIG_BT_BLUEDROID_ENABLED */
