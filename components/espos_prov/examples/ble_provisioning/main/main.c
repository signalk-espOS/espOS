/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * ble_provisioning — a device that gets its WiFi credentials from a phone over
 * BLE, with no access point and no captive portal. The whole firmware is
 * espos_start().
 *
 * NOTHING HERE CALLS espos_prov_start(). espos_start() does it, and only when
 * no WiFi network is configured (espos_core.c): a device already on a network
 * must not sit advertising for anyone in range to reconfigure it. So this
 * example provisions itself on a fresh device, and on the next boot — now
 * holding credentials — comes up as an ordinary espOS device with the radio
 * free. To provision it again, clear the networks.
 *
 * The loop below only narrates. It is here because provisioning is the one
 * espOS flow you cannot watch over the network: the device has no network yet,
 * which is the whole reason it is advertising.
 */
#include "esp_log.h"
#include "espos.h"
#include "espos_prov.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ble_provisioning";

void app_main(void)
{
    /* log -> config -> httpd -> net -> wifi -> sk -> ota, then provisioning if
     * there is nothing to join. Returns advertising; credentials arrive later,
     * on the provisioning task. A device that cannot advertise still boots --
     * espos_start() logs the reason and carries on rather than failing. */
    ESP_ERROR_CHECK(espos_start(NULL));

    if (!espos_prov_is_active()) {
        /* The ordinary case after the first provisioning: credentials are
         * stored, so espos_start() did not advertise and the device is
         * connecting with them. Read the PoP from GET /api/v1/prov over that
         * network if you need it for next time. */
        ESP_LOGI(TAG, "not advertising -- a network is already configured");
        return;
    }

    /* The PoP is printed by espos_prov itself on the line above this one. It is
     * generated once, at random, and kept, so it is the same on every boot
     * until it is cleared. */
    ESP_LOGI(TAG, "advertising for a phone; the proof of possession is logged above");

    /* Until the credentials land. got_credentials() says the handover
     * happened, not that the network works -- espos_wifi decides that, and its
     * own log lines say which SSID it is trying. */
    while (espos_prov_is_active() && !espos_prov_got_credentials()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (espos_prov_got_credentials()) {
        ESP_LOGI(TAG, "credentials received -- espos_wifi is connecting with them");
    } else {
        /* The window closes on its own (CONFIG_ESPOS_PROV_TIMEOUT_S, ten
         * minutes by default), because a device left advertising is a device
         * anyone in the marina can try to provision. Reboot to reopen it. */
        ESP_LOGW(TAG, "the provisioning window closed with no credentials");
    }
}
