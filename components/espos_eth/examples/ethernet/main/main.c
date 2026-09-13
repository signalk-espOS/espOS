/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * ethernet — an espOS device on a cable. The whole firmware is espos_start().
 *
 * Built for the Waveshare ESP32-P4-WIFI6-POE-ETH, where the EMAC defaults are
 * the board's wiring and one PoE cable is both power and network. espos_eth
 * brings the internal EMAC and the PHY up, DHCP hands out an address, and
 * espos_net routes everything -- SignalK, OTA, the web UI, mDNS -- over the
 * cable.
 *
 * WiFi is off in this example's sdkconfig.defaults, and that is what makes it
 * an Ethernet device rather than a WiFi device that happens to have a port.
 * With WiFi on, both transports run and the cable still carries the route
 * whenever it has a link.
 */
#include "esp_log.h"
#include "espos.h"
#include "espos_eth.h"
#include "espos_net.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ethernet";

void app_main(void)
{
    /* log -> config -> httpd -> net -> eth -> sk -> ota. Returns with the
     * driver started; the link and the address arrive whenever a cable does,
     * and a cable that is not plugged in is not an error. */
    ESP_ERROR_CHECK(espos_start(NULL));

    /* Nothing more is needed. This loop only says where the device can be
     * found, once, on each change. The line also lands in the web UI's log
     * page, which on a PoE-powered board is often the only place anyone reads
     * it. */
    bool on_cable_said = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        espos_net_status_t st;
        if (espos_net_get_status(&st) != ESP_OK) {
            continue;
        }
        const bool on_cable = st.up && st.iface == ESPOS_NET_IF_ETH;
        if (on_cable && !on_cable_said) {
            ESP_LOGI(TAG, "on the cable as %s -- http://%s.local", st.ip, st.hostname);
            on_cable_said = true;
        } else if (!on_cable && on_cable_said) {
            ESP_LOGW(TAG, "off the cable (link %s)", espos_eth_link_up() ? "up, no address" : "down");
            on_cable_said = false;
        }
    }
}
