/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * Types shared between the BLE backends and the gateway. Deliberately free of
 * ESP-IDF Bluetooth headers so the protocol logic above can be built and
 * tested on the linux host target, where no BLE stack exists.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Largest advertisement payload forwarded, from Kconfig: 62 (31 bytes of
 * legacy advertisement plus a 31-byte scan response) unless the BLE 5
 * extended scanner is built, whose reports carry more. The fallback is for
 * the host build, which has no Kconfig for this component. */
#include "sdkconfig.h"
#ifdef CONFIG_ESPOS_BLE_ADV_DATA_MAX
#define ESPOS_BLE_ADV_DATA_MAX CONFIG_ESPOS_BLE_ADV_DATA_MAX
#else
#define ESPOS_BLE_ADV_DATA_MAX 62
#endif
#define ESPOS_BLE_NAME_MAX 32
#define ESPOS_BLE_ADDR_LEN 18 /* "AA:BB:CC:DD:EE:FF" + NUL */

/* One advertisement, copied out of the stack callback. Values only - no
 * pointers into stack-owned buffers, because these are queued. */
typedef struct {
    char address[ESPOS_BLE_ADDR_LEN]; /* uppercase, colon-separated */
    char name[ESPOS_BLE_NAME_MAX];    /* "" when not advertised */
    int8_t rssi;
    uint8_t adv_data_len;
    uint8_t adv_data[ESPOS_BLE_ADV_DATA_MAX];
    int64_t received_us; /* esp_timer_get_time() when seen */
} espos_ble_adv_t;

/* GATT write mode. Some peripherals (JK-BMS, Daly-BMS and friends) reject
 * write-with-response on their command characteristic with "Write not
 * permitted" and accept only write-without-response; signalk-server sends the
 * per-write `with_response` flag for exactly this. Note a no-response write
 * produces NO completion event, so anything sequencing writes must not wait
 * for one. */
typedef enum {
    ESPOS_BLE_WRITE_WITH_RESPONSE = 0,
    ESPOS_BLE_WRITE_NO_RESPONSE = 1,
} espos_ble_write_mode_t;

/* Callbacks from the BLE backend. All run on the Bluetooth stack task and
 * must not block. */
typedef struct {
    void (*on_advertisement)(const espos_ble_adv_t *adv, void *arg);
    void (*on_gatt_connected)(int conn_id, void *arg);
    void (*on_gatt_disconnected)(int conn_id, int reason, void *arg);
    void (*on_gatt_notify)(int conn_id, const char *char_uuid,
                           const uint8_t *data, size_t len, void *arg);
    void (*on_gatt_read)(int conn_id, const char *char_uuid,
                         const uint8_t *data, size_t len, void *arg);
    void (*on_gatt_write_done)(int conn_id, const char *char_uuid, bool ok,
                               void *arg);
    void (*on_gatt_error)(int conn_id, const char *error, void *arg);
    void *arg;
} espos_ble_callbacks_t;

/* Backend interface. One implementation is compiled in per target: the
 * esp_hosted VHCI path on the ESP32-P4, native Bluedroid elsewhere. */
/* Bring up ONLY the radio controller, not the Bluedroid host above it. Backs
 * espos_ble_reserve_controller(); idempotent. */
esp_err_t espos_ble_backend_controller_only(void);

esp_err_t espos_ble_backend_init(const espos_ble_callbacks_t *cb);
esp_err_t espos_ble_backend_deinit(void);
/* coded: also listen on the LE Coded PHY (long range). Only the BLE 5
 * extended scanner can; the legacy one logs and ignores it. */
esp_err_t espos_ble_scan_start(bool active, bool coded, uint16_t interval_ms, uint16_t window_ms);
esp_err_t espos_ble_scan_stop(void);
bool espos_ble_is_scanning(void);
/* Whether this build scans with BLE 5 extended scanning (else 4.2 legacy). */
bool espos_ble_scan_extended(void);
/* Extended advertisements not forwarded: larger than ESPOS_BLE_ADV_DATA_MAX,
 * truncated by the controller, unfinished or malformed (ble_proto.h,
 * espos_ble_reasm_feed). Always 0 on the legacy scanner. */
uint32_t espos_ble_ext_adv_dropped(void);

/* Take the Bluedroid GAP callback back.
 *
 * Bluedroid holds exactly ONE GAP callback -- esp_ble_gap_register_callback()
 * is a setter (btc_profile_cb_set), not a subscribe -- so any other component
 * that registers one replaces ours and our scan-result events stop arriving,
 * with no error anywhere (protocomm's simple_ble does exactly that). Call
 * this after such a component has finished, before scanning again; scanning
 * without it looks like a dead radio. */
esp_err_t espos_ble_gap_reclaim(void);
uint32_t espos_ble_scan_hits(void);
/* Controller address as "AA:BB:...", or "" if unavailable. */
const char *espos_ble_mac(void);

/* GATT client. conn_id identifies the connection; -1 on failure. */
int espos_ble_gatt_connect(const char *mac, const char *service_uuid);
esp_err_t espos_ble_gatt_subscribe(int conn_id, const char *char_uuid);
esp_err_t espos_ble_gatt_read(int conn_id, const char *char_uuid);
/* Returns ESP_OK when the write was sent and a completion event will follow;
 * ESP_ERR_NOT_FINISHED when it was sent as write-without-response, for which
 * the stack generates no completion at all - callers sequencing writes must
 * advance themselves on that. Other codes are real failures.
 *
 * Never calls back into espos_ble_callbacks_t before returning, so a caller
 * may hold its own lock across it. */
esp_err_t espos_ble_gatt_write(int conn_id, const char *char_uuid,
                               const uint8_t *data, size_t len,
                               espos_ble_write_mode_t mode);
esp_err_t espos_ble_gatt_disconnect(int conn_id);
uint32_t espos_ble_gatt_active_count(void);

/* The gateway's GATT frame queue for GET /api/v1/ble/status: frames lost
 * before the control socket took them, and frames still waiting. Internal
 * rather than in espos_ble_status_t, whose layout is public ABI.
 * ESP_ERR_TIMEOUT if the queue's lock could not be taken. */
esp_err_t espos_ble_gatt_queue_stats(uint32_t *dropped, size_t *pending);

#ifdef __cplusplus
}
#endif
