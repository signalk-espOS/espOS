/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bluedroid backend: controller bring-up, GAP scanning, advertisement
 * extraction.
 *
 * Two scanners, chosen by sdkconfig because Bluedroid builds only one of them:
 * BLE 4.2 legacy scan (CONFIG_BT_BLE_42_SCAN_EN) and BLE 5.0 extended scan
 * (CONFIG_BT_BLE_50_EXTEND_SCAN_EN). IDF refuses both at once. The extended
 * scanner hears everything the legacy one does -- legacy PDUs arrive as
 * extended reports -- plus devices that advertise only with extended PDUs,
 * and it can listen on the LE Coded PHY. Classic ESP32 has no BLE 5.0 and the
 * P4 keeps 4.2 (docs/ble.md, "Targets"), so the legacy path stays.
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

/* esp_hosted 3.x: the hosted BT feature flag is CONFIG_ESP_HOSTED_HOST_FEAT_BT.
 * 2.x spelled it CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID + ..._BLUEDROID_HCI_VHCI
 * and 3.0 removed both, along with esp_hosted_bluedroid.h -- so a stale gate
 * silently selects the NATIVE controller branch below on a P4, which has no
 * radio of its own, and the gateway comes up scanning nothing. */
#if defined(CONFIG_ESP_HOSTED_HOST_FEAT_BT)
#include "esp_hosted.h"
#include "esp_hosted_bt_host_stack.h"
#else
#include "esp_bt.h"
#endif

#if defined(CONFIG_BT_BLE_50_EXTEND_SCAN_EN)
#define ESPOS_BLE_EXT_SCAN 1
#elif defined(CONFIG_BT_BLE_42_SCAN_EN)
#define ESPOS_BLE_EXT_SCAN 0
#else
#error "espos_ble needs a scanner: enable CONFIG_BT_BLE_50_EXTEND_SCAN_EN or CONFIG_BT_BLE_42_SCAN_EN"
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
#if ESPOS_BLE_EXT_SCAN
static esp_ble_ext_scan_params_t s_scan_params;
static bool s_coded;
/* Only touched from gap_cb, on the one BTC task. */
static espos_ble_reasm_t s_reasm;
#else
static esp_ble_scan_params_t s_scan_params;
#endif

/* Copies one advertisement out of the stack's buffers and hands it to the
 * gateway. Runs on the BT stack task, so the callback must not block. The
 * copy is static rather than on that task's stack: with BLE 5 payloads it is
 * ~320 bytes, the BTC stack is IDF's default size, and the gateway copies it
 * into its ring before the callback returns, so one buffer is enough. */
static espos_ble_adv_t s_adv;

static void emit_adv(const uint8_t *bda, int8_t rssi, const uint8_t *data, uint16_t len)
{
    if (!s_cb.on_advertisement) return;

    espos_ble_adv_t *const a = &s_adv;
    memset(a, 0, sizeof(*a));
    snprintf(a->address, sizeof(a->address), "%02X:%02X:%02X:%02X:%02X:%02X",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    a->rssi = rssi;
    a->received_us = esp_timer_get_time();

    if (len > ESPOS_BLE_ADV_DATA_MAX) len = ESPOS_BLE_ADV_DATA_MAX;
    memcpy(a->adv_data, data, len);
    a->adv_data_len = (uint8_t)len;

    /* The bounded variant: the unbounded esp_ble_resolve_adv_data() walks
     * the payload with no length to stop at, and IDF 6 deprecates it for
     * exactly that reason. Fall back to the shortened name, which is what
     * a device advertises when the full one does not fit. Resolved on the
     * copy, which the stack does not own and the API wants non-const. */
    uint8_t name_len = 0;
    uint8_t *name = esp_ble_resolve_adv_data_by_type(a->adv_data, len,
                                                     ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
    if (!name || !name_len) {
        name = esp_ble_resolve_adv_data_by_type(a->adv_data, len,
                                                ESP_BLE_AD_TYPE_NAME_SHORT, &name_len);
    }
    if (name && name_len) {
        if (name_len >= ESPOS_BLE_NAME_MAX) name_len = ESPOS_BLE_NAME_MAX - 1;
        memcpy(a->name, name, name_len);
        a->name[name_len] = '\0';
    }

    s_cb.on_advertisement(a, s_cb.arg);
}

#if ESPOS_BLE_EXT_SCAN
static void on_ext_report(const esp_ble_gap_ext_adv_report_t *r)
{
    /* One hit per advertisement the radio finished receiving, forwarded or
     * not, so scan_hits keeps meaning what the radio saw. */
    if (r->data_status != ESP_BLE_GAP_EXT_ADV_DATA_INCOMPLETE) s_scan_hits++;
    const uint8_t *data;
    uint16_t len;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (espos_ble_reasm_feed(&s_reasm, now_ms, r->addr, r->sid, r->data_status, r->adv_data,
                             r->adv_data_len, &data, &len)) {
        emit_adv(r->addr, r->rssi, data, len);
    }
}
#endif

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
#if ESPOS_BLE_EXT_SCAN
    case ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT:
        if (param->set_ext_scan_params.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "ext scan param set failed: %d", param->set_ext_scan_params.status);
            break;
        }
        if (s_scan_inhibited) {
            ESP_LOGI(TAG, "scan armed but suspended meanwhile; not starting");
            break;
        }
        esp_ble_gap_start_ext_scan(0, 0); /* until stopped, no period */
        break;

    case ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT:
        if (param->ext_scan_start.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "ext scan start failed: %d", param->ext_scan_start.status);
            s_scanning = false;
        } else if (s_scan_inhibited) {
            /* Suspended while the start was in flight; scan_stop() saw no
             * running scan and could not stop it then. */
            s_scanning = true;
            esp_ble_gap_stop_ext_scan();
        } else {
            s_scanning = true;
            ESP_LOGI(TAG, "scanning (BLE 5 extended, %s)", s_coded ? "1M + Coded PHY" : "1M PHY");
        }
        break;

    case ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT:
        s_scanning = false;
        espos_ble_reasm_reset(&s_reasm);
        break;

    case ESP_GAP_BLE_EXT_ADV_REPORT_EVT:
        on_ext_report(&param->ext_adv_report.params);
        break;
#else
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
        } else if (s_scan_inhibited) {
            /* Suspended while the start was in flight; scan_stop() saw no
             * running scan and could not stop it then. */
            s_scanning = true;
            esp_ble_gap_stop_scanning();
        } else {
            s_scanning = true;
            ESP_LOGI(TAG, "scanning (BLE 4.2 legacy)");
        }
        break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        s_scanning = false;
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;
        s_scan_hits++;
        /* Legacy reports carry the scan response behind the advertisement in
         * one buffer. Sum in a wider type: each part is at most 31. */
        uint16_t total = (uint16_t)param->scan_rst.adv_data_len +
                         (uint16_t)param->scan_rst.scan_rsp_len;
        emit_adv(param->scan_rst.bda, (int8_t)param->scan_rst.rssi,
                 param->scan_rst.ble_adv, total);
        break;
    }
#endif

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
#if defined(CONFIG_ESP_HOSTED_HOST_FEAT_BT)
    /* ESP32-P4: the controller lives on the C6, reached over esp_hosted's HCI
     * byte pipe.
     *
     * esp_hosted 3.0 moved the stack glue out of the hosted core into an
     * adapter with one entry point, and that entry point now owns the order
     * this code used to get right by hand: enabling the remote controller is
     * what populates the VHCI driver's function pointers, so attaching the
     * HCI driver first faulted on the first call through them (verified
     * 2026-08-21, espOS 0.2.x). esp_hosted_bt_host_stack_setup() does the
     * controller init+enable and the HCI bind in that order, and adds the
     * bounded wait for the co-processor's controller that the hand-rolled
     * sequence never had -- so a slave that is slow to bring BT up is a
     * retry rather than a fault.
     *
     * There is no hosted-side stack knob any more: 3.x picks the stack from
     * the IDF BT Kconfig. The _CONFIG_BLUEDROID() initialiser is used rather
     * than _CONFIG_DEFAULT() because the latter resolves to CUSTOM when
     * neither BT stack is enabled, and a silent CUSTOM binding would be a
     * gateway that scans nothing. Naming the stack makes the mismatch a
     * compile-time fact instead. */
    ESP_RETURN_ON_ERROR(esp_hosted_init(), TAG, "esp_hosted_init");
    ESP_RETURN_ON_ERROR(esp_hosted_connect_to_slave(), TAG, "connect_to_slave");

    esp_hosted_bt_host_stack_cfg_t bt_cfg = ESP_HOSTED_BT_HOST_STACK_CONFIG_BLUEDROID();
    bt_cfg.controller_ready_timeout_ms = EH_BT_CTRL_DEFAULT_READY_TIMEOUT_MS;
    ESP_RETURN_ON_ERROR(esp_hosted_bt_host_stack_setup(&bt_cfg), TAG, "bt_host_stack_setup");
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

esp_err_t espos_ble_scan_start(bool active, bool coded, uint16_t interval_ms, uint16_t window_ms)
{
    s_scan_inhibited = false;
    if (window_ms > interval_ms) window_ms = interval_ms;

#if ESPOS_BLE_EXT_SCAN
    s_coded = coded;
    /* With two PHYs the controller listens on each in turn within one
     * interval, so the configured window is shared between them: each PHY
     * gets half, which keeps the duty cycle the user set. */
    if (coded) {
        window_ms /= 2;
    }
    esp_ble_ext_scan_cfg_t cfg = {
        .scan_type = active ? BLE_SCAN_TYPE_ACTIVE : BLE_SCAN_TYPE_PASSIVE,
        .scan_interval = ms_to_units(interval_ms),
        .scan_window = ms_to_units(window_ms),
    };
    memset(&s_scan_params, 0, sizeof(s_scan_params));
    s_scan_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    s_scan_params.filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    /* Duplicates are wanted: the server times devices out on silence, so a
     * beacon that never changes must keep being reported. */
    s_scan_params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
    s_scan_params.cfg_mask = ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK;
    s_scan_params.uncoded_cfg = cfg;
    if (coded) {
        s_scan_params.cfg_mask |= ESP_BLE_GAP_EXT_SCAN_CFG_CODE_MASK;
        s_scan_params.coded_cfg = cfg;
    }
    return esp_ble_gap_set_ext_scan_params(&s_scan_params);
#else
    static bool warned;
    if (coded && !warned) {
        /* Once: scanning restarts after every suspension. */
        warned = true;
        ESP_LOGW(TAG, "coded_phy needs the BLE 5 extended scanner; this build scans BLE 4.2 legacy only");
    }
    s_scan_params.scan_type = active ? BLE_SCAN_TYPE_ACTIVE : BLE_SCAN_TYPE_PASSIVE;
    s_scan_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    s_scan_params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    s_scan_params.scan_interval = ms_to_units(interval_ms);
    s_scan_params.scan_window = ms_to_units(window_ms);
    /* Duplicates are wanted: the server times devices out on silence, so a
     * beacon that never changes must keep being reported. */
    s_scan_params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
    return esp_ble_gap_set_scan_params(&s_scan_params);
#endif
}

esp_err_t espos_ble_scan_stop(void)
{
    /* Set before the early return: a scan that has been ARMED but has not yet
     * reported SCAN_START_COMPLETE has s_scanning == false, and without this
     * flag the pending start would run on regardless of having been stopped. */
    s_scan_inhibited = true;
    if (!s_scanning) return ESP_OK;
#if ESPOS_BLE_EXT_SCAN
    return esp_ble_gap_stop_ext_scan();
#else
    return esp_ble_gap_stop_scanning();
#endif
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
bool espos_ble_scan_extended(void) { return ESPOS_BLE_EXT_SCAN; }
#if ESPOS_BLE_EXT_SCAN
uint32_t espos_ble_ext_adv_dropped(void) { return s_reasm.dropped; }
#else
uint32_t espos_ble_ext_adv_dropped(void) { return 0; }
#endif
const char *espos_ble_mac(void) { return s_mac; }

#endif /* CONFIG_BT_BLUEDROID_ENABLED */
