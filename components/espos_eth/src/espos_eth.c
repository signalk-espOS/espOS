/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_eth -- see espos_eth.h.
 *
 * The whole transport is the four steps docs/net.md lists under "How a
 * transport plugs in": start after espos_net, register the netif, report every
 * change, and never post NETWORK_UP / NETWORK_DOWN itself -- espos_net decides
 * whether the default route moved, and a cable arriving while WiFi carries the
 * route is a route change to it, not a network event to anyone above the seam.
 *
 * mDNS needs nothing from here. The netif is IDF's default Ethernet netif
 * (if_key "ETH_DEF"), which the mdns component follows by itself when
 * CONFIG_MDNS_PREDEF_NETIF_ETH is on -- its default -- so <hostname>.local
 * answers on the cable as it does on the station.
 */
#include "sdkconfig.h"

#include <stdbool.h>
#include <stdio.h>

#include "espos_eth.h"

#if CONFIG_ETH_USE_ESP32_EMAC

#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "espos_net.h"

static const char *TAG = "espos_eth";

static struct {
    bool installed; /* driver, netif and event handlers exist */
    bool started;   /* esp_eth_start() succeeded and no stop() since */
    /* Written on the default event loop's task, read from any task by
     * espos_eth_link_up(); a single bool, so volatile is enough. */
    volatile bool link;
    esp_eth_handle_t eth;
    esp_eth_mac_t *mac;
    esp_eth_phy_t *phy;
    esp_netif_t *netif;
    esp_eth_netif_glue_handle_t glue;
} s;

static void report_down(void)
{
    espos_net_report(ESPOS_NET_IF_ETH, false, NULL, NULL, NULL, 0);
}

static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        s.link = true;
        ESP_LOGI(TAG, "link up; waiting for an address");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        s.link = false;
        /* Reported on the link as well as on LOST_IP, so the route leaves an
         * unplugged cable at once rather than whenever the address is
         * declared lost. espos_net treats the repeat as the no-op it is. */
        ESP_LOGW(TAG, "link down");
        report_down();
        break;
    default:
        break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_ETH_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        char ip[ESPOS_NET_IP_MAX];
        char netmask[ESPOS_NET_IP_MAX];
        char gateway[ESPOS_NET_IP_MAX];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        snprintf(netmask, sizeof(netmask), IPSTR, IP2STR(&e->ip_info.netmask));
        snprintf(gateway, sizeof(gateway), IPSTR, IP2STR(&e->ip_info.gw));
        ESP_LOGI(TAG, "address %s", ip);
        /* From this handler, on the event loop's task, as the checklist asks:
         * espos_net runs its subscribers on the caller before returning. */
        espos_net_report(ESPOS_NET_IF_ETH, true, ip, netmask, gateway, 0);
    } else if (id == IP_EVENT_ETH_LOST_IP) {
        ESP_LOGW(TAG, "lost the address");
        report_down();
    }
}

static void unregister_handlers(void)
{
    esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip_event);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_LOST_IP, on_ip_event);
}

/* Undo whatever install() got through, in reverse. Only used on a failed
 * install: once installed, the driver is kept for the life of the process
 * (see espos_eth_stop() in the header). */
static void uninstall_partial(void)
{
    unregister_handlers();
    if (s.glue) {
        esp_eth_del_netif_glue(s.glue);
        s.glue = NULL;
    }
    if (s.netif) {
        esp_netif_destroy(s.netif);
        s.netif = NULL;
    }
    if (s.eth) {
        esp_eth_driver_uninstall(s.eth);
        s.eth = NULL;
    }
    if (s.phy) {
        s.phy->del(s.phy);
        s.phy = NULL;
    }
    if (s.mac) {
        s.mac->del(s.mac);
        s.mac = NULL;
    }
}

static esp_err_t install(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    /* The chip's EMAC defaults are the board wiring on the ESP32-P4 (the
     * Waveshare PoE board: MDC 31, MDIO 52, RMII clock in on GPIO 50). What
     * varies per board is the PHY's address and reset line. */
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = CONFIG_ESPOS_ETH_PHY_ADDR;
    phy_cfg.reset_gpio_num = CONFIG_ESPOS_ETH_PHY_RST_GPIO;

    s.mac = esp_eth_mac_new_esp32(&emac_cfg, &mac_cfg);
    if (!s.mac) {
        ESP_LOGE(TAG, "could not create the EMAC");
        uninstall_partial();
        return ESP_ERR_NO_MEM;
    }
    s.phy = esp_eth_phy_new_generic(&phy_cfg);
    if (!s.phy) {
        ESP_LOGE(TAG, "could not create the PHY driver");
        uninstall_partial();
        return ESP_ERR_NO_MEM;
    }
    esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(s.mac, s.phy);
    err = esp_eth_driver_install(&cfg, &s.eth);
    if (err != ESP_OK) {
        /* The usual cause on real hardware: the PHY did not answer on MDIO --
         * wrong address, wrong reset GPIO, or no PHY on this board. Name the
         * two settings, because the error code alone says none of that. */
        ESP_LOGE(TAG, "esp_eth_driver_install: %s (PHY address %d, reset GPIO %d -- check menu \"espOS Ethernet\")",
                 esp_err_to_name(err), CONFIG_ESPOS_ETH_PHY_ADDR, CONFIG_ESPOS_ETH_PHY_RST_GPIO);
        s.eth = NULL;
        uninstall_partial();
        return err;
    }

    esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_ETH();
    s.netif = esp_netif_new(&ncfg);
    if (s.netif) {
        s.glue = esp_eth_new_netif_glue(s.eth);
    }
    if (!s.netif || !s.glue || esp_netif_attach(s.netif, s.glue) != ESP_OK) {
        ESP_LOGE(TAG, "could not attach the Ethernet netif");
        uninstall_partial();
        return ESP_FAIL;
    }

    /* Before the driver starts: espos_net sets the hostname on the netif now,
     * and the first DHCP request has to carry it. */
    (void)espos_net_register_if(ESPOS_NET_IF_ETH, s.netif);

    err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, NULL);
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip_event, NULL);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, on_ip_event, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "event handlers: %s", esp_err_to_name(err));
        uninstall_partial();
        return err;
    }
    s.installed = true;
    return ESP_OK;
}

esp_err_t espos_eth_start(void)
{
    if (s.started) {
        return ESP_OK;
    }
    /* The hostname the DHCP request carries is espos_net's, so the order is
     * enforced exactly as espos_wifi_start() enforces it. */
    espos_net_status_t ns;
    if (espos_net_get_status(&ns) != ESP_OK) {
        ESP_LOGE(TAG, "espos_eth_start: call espos_net_start() first (or espos_start())");
        return ESP_ERR_INVALID_STATE;
    }
    if (!s.installed) {
        esp_err_t err = install();
        if (err != ESP_OK) {
            return err;
        }
    }
    esp_err_t err = esp_eth_start(s.eth);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_start: %s", esp_err_to_name(err));
        return err;
    }
    s.started = true;
    /* Not "connected": a cable may not be plugged in, and that is not an
     * error. The link and the address are logged when they arrive. */
    ESP_LOGI(TAG, "started (PHY address %d, reset GPIO %d); waiting for a link", CONFIG_ESPOS_ETH_PHY_ADDR,
             CONFIG_ESPOS_ETH_PHY_RST_GPIO);
    return ESP_OK;
}

esp_err_t espos_eth_stop(void)
{
    if (!s.started) {
        return ESP_OK;
    }
    esp_err_t err = esp_eth_stop(s.eth);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_stop: %s", esp_err_to_name(err));
        return err;
    }
    s.started = false;
    s.link = false;
    report_down();
    ESP_LOGI(TAG, "stopped");
    return ESP_OK;
}

bool espos_eth_link_up(void)
{
    return s.started && s.link;
}

#else /* no internal EMAC on this chip, or CONFIG_ETH_USE_ESP32_EMAC is off */

esp_err_t espos_eth_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t espos_eth_stop(void)
{
    return ESP_OK;
}

bool espos_eth_link_up(void)
{
    return false;
}

#endif
