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
 *
 * Which controller carries the cable is menu "espOS Ethernet": the internal
 * EMAC, or a W5500 on SPI. Only new_driver() differs between them; the netif,
 * the events and the reporting are the same code.
 */
#include "sdkconfig.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>

#include "espos_eth.h"

#if CONFIG_ESPOS_ETH_CONTROLLER_EMAC || CONFIG_ESPOS_ETH_CONTROLLER_W5500

#include "esp_eth.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "espos_net.h"
#if CONFIG_ESPOS_ETH_CONTROLLER_EMAC
#include "esp_eth_mac_esp.h"
#else
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "esp_mac.h"
#endif

static const char *TAG = "espos_eth";

static struct {
    bool installed; /* driver, netif and event handlers exist */
    /* Both read from any task by espos_eth_link_up(); link is written on the
     * default event loop's task, started by whoever calls start/stop. */
    atomic_bool started; /* esp_eth_start() succeeded and no stop() since */
    atomic_bool link;
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
    /* ETH_EVENT is shared by every Ethernet driver in the process; an app that
     * installs its own (a W5500 beside the EMAC, say) must not move this
     * transport's link. The payload is the posting driver's handle. */
    const esp_eth_handle_t *eth = data;
    if (!eth || *eth != s.eth) {
        return;
    }
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        atomic_store(&s.link, true);
        ESP_LOGI(TAG, "link up; waiting for an address");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        atomic_store(&s.link, false);
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
    /* The same for IP_EVENT_ETH_*: any Ethernet netif raises them, and both
     * carry an ip_event_got_ip_t naming it. */
    const ip_event_got_ip_t *e = data;
    if (!e || e->esp_netif != s.netif) {
        return;
    }
    if (id == IP_EVENT_ETH_GOT_IP) {
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

#if CONFIG_ESPOS_ETH_CONTROLLER_EMAC

/* The usual cause of a failed install on real hardware: the PHY did not
 * answer on MDIO -- wrong address, wrong reset GPIO, or no PHY on this board.
 * The error code alone says none of that, so the log names the settings. */
#define WIRING_FMT  "PHY address %d, reset GPIO %d"
#define WIRING_ARGS CONFIG_ESPOS_ETH_PHY_ADDR, CONFIG_ESPOS_ETH_PHY_RST_GPIO

static esp_err_t new_driver(void)
{
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
        return ESP_ERR_NO_MEM;
    }
    s.phy = esp_eth_phy_new_generic(&phy_cfg);
    if (!s.phy) {
        ESP_LOGE(TAG, "could not create the PHY driver");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* The EMAC takes its address from eFuse by itself. */
static esp_err_t set_mac_address(void)
{
    return ESP_OK;
}

#else /* CONFIG_ESPOS_ETH_CONTROLLER_W5500 */

/* A W5500 that does not answer is wired differently from what menu "espOS
 * Ethernet" says, or clocked too fast for the board; name what was used. */
#define WIRING_FMT "W5500 on SPI%d: SCLK %d, MOSI %d, MISO %d, CS %d, INT %d, %d MHz"
#define WIRING_ARGS                                                                                          \
    CONFIG_ESPOS_ETH_W5500_SPI_HOST + 1, CONFIG_ESPOS_ETH_W5500_SCLK_GPIO, CONFIG_ESPOS_ETH_W5500_MOSI_GPIO, \
        CONFIG_ESPOS_ETH_W5500_MISO_GPIO, CONFIG_ESPOS_ETH_W5500_CS_GPIO, CONFIG_ESPOS_ETH_W5500_INT_GPIO,   \
        CONFIG_ESPOS_ETH_W5500_SPI_MHZ

static esp_err_t new_driver(void)
{
    const spi_host_device_t host = (spi_host_device_t)CONFIG_ESPOS_ETH_W5500_SPI_HOST;
    spi_bus_config_t bus = {
        .sclk_io_num = CONFIG_ESPOS_ETH_W5500_SCLK_GPIO,
        .mosi_io_num = CONFIG_ESPOS_ETH_W5500_MOSI_GPIO,
        .miso_io_num = CONFIG_ESPOS_ETH_W5500_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    /* ESP_ERR_INVALID_STATE: the bus is already up, set up by the app for
     * another device on the same lines, or by an earlier start that failed
     * further on. Either way the W5500 joins it as a device. */
    esp_err_t err = spi_bus_initialize(host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }
    /* The driver attaches its interrupt through the shared GPIO ISR service,
     * which somebody has to install; an app may already have. */
    if (CONFIG_ESPOS_ETH_W5500_INT_GPIO >= 0) {
        err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(err));
            return err;
        }
    }

    spi_device_interface_config_t dev = {
        .mode = 0,
        .clock_speed_hz = CONFIG_ESPOS_ETH_W5500_SPI_MHZ * 1000 * 1000,
        .queue_size = 20,
        .spics_io_num = CONFIG_ESPOS_ETH_W5500_CS_GPIO,
    };
    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(host, &dev);
    w5500_cfg.base.int_gpio_num = CONFIG_ESPOS_ETH_W5500_INT_GPIO;
    /* The driver takes exactly one of the two: an interrupt line, or a poll
     * period. */
    w5500_cfg.base.poll_period_ms = CONFIG_ESPOS_ETH_W5500_INT_GPIO < 0 ? CONFIG_ESPOS_ETH_W5500_POLL_MS : 0;
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = CONFIG_ESPOS_ETH_W5500_RST_GPIO;

    s.mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);
    if (!s.mac) {
        ESP_LOGE(TAG, "could not create the W5500 MAC");
        return ESP_FAIL;
    }
    s.phy = esp_eth_phy_new_w5500(&phy_cfg);
    if (!s.phy) {
        ESP_LOGE(TAG, "could not create the W5500 PHY driver");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* A W5500 has no address of its own that anyone can rely on, so it gets the
 * one the chip's eFuse derives for Ethernet: stable across reboots, and
 * distinct from the station's. Before esp_eth_start(), as the driver wants. */
static esp_err_t set_mac_address(void)
{
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_ETH);
    if (err == ESP_OK) {
        err = esp_eth_ioctl(s.eth, ETH_CMD_S_MAC_ADDR, mac);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "setting the W5500's MAC address: %s", esp_err_to_name(err));
    }
    return err;
}

#endif

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

    err = new_driver();
    if (err != ESP_OK) {
        uninstall_partial();
        return err;
    }
    esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(s.mac, s.phy);
    err = esp_eth_driver_install(&cfg, &s.eth);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_driver_install: %s (" WIRING_FMT " -- check menu \"espOS Ethernet\")",
                 esp_err_to_name(err), WIRING_ARGS);
        s.eth = NULL;
        uninstall_partial();
        return err;
    }
    err = set_mac_address();
    if (err != ESP_OK) {
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

    /* Last, so a failure above never leaves espos_net holding a netif that
     * uninstall_partial() destroyed. Still before the driver starts: espos_net
     * sets the hostname on the netif now, and the first DHCP request has to
     * carry it. */
    err = espos_net_register_if(ESPOS_NET_IF_ETH, s.netif);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "espos_net_register_if: %s", esp_err_to_name(err));
        uninstall_partial();
        return err;
    }
    s.installed = true;
    return ESP_OK;
}

esp_err_t espos_eth_start(void)
{
    if (atomic_load(&s.started)) {
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
    atomic_store(&s.started, true);
    /* Not "connected": a cable may not be plugged in, and that is not an
     * error. The link and the address are logged when they arrive. */
    ESP_LOGI(TAG, "started (" WIRING_FMT "); waiting for a link", WIRING_ARGS);
    return ESP_OK;
}

esp_err_t espos_eth_stop(void)
{
    if (!atomic_load(&s.started)) {
        return ESP_OK;
    }
    esp_err_t err = esp_eth_stop(s.eth);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_stop: %s", esp_err_to_name(err));
        return err;
    }
    atomic_store(&s.started, false);
    atomic_store(&s.link, false);
    report_down();
    ESP_LOGI(TAG, "stopped");
    return ESP_OK;
}

bool espos_eth_link_up(void)
{
    return atomic_load(&s.started) && atomic_load(&s.link);
}

#else /* menu "espOS Ethernet" names no controller (the default on a chip without an EMAC) */

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
