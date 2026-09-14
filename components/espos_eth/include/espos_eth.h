/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_eth — wired Ethernet as an espos_net transport.
 *
 * The chip's internal EMAC and an RMII PHY, with DHCP. Nothing above the
 * network seam knows it exists: the transport registers its netif and reports
 * its link into espos_net, and espos_net decides the default route. When the
 * WiFi station is up too, Ethernet carries the route as soon as the cable has
 * an address -- the preference is
 * static (Ethernet over WiFi over Thread, docs/net.md), because a wired link is
 * the one somebody ran a cable for.
 *
 * Pins are not configured here. The chip's EMAC defaults (MDC, MDIO, the RMII
 * data lines and the reference clock) are IDF's ETH_ESP32_EMAC_DEFAULT_CONFIG,
 * which on the ESP32-P4 are exactly the Waveshare ESP32-P4-WIFI6-POE-ETH
 * wiring; the two things that genuinely vary per board -- the PHY address and
 * its reset GPIO -- are Kconfig (menu "espOS Ethernet").
 *
 * The PHY is driven by IDF's generic IEEE 802.3 driver. IDF 6 moved the named
 * PHY drivers (IP101, LAN87xx, ...) out to the component registry; the generic
 * one covers link, speed and duplex from the standard registers, which is all
 * this transport needs, and adds no dependency.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bring Ethernet up: EMAC, PHY, netif, DHCP. Returns once the driver has
 * started -- the link and the address arrive later, and are reported into
 * espos_net as they do. Requires espos_net_start() first (espos_start() does
 * both, in order): the netif takes its hostname from espos_net before the
 * first DHCP request goes out.
 *
 * ESP_ERR_NOT_SUPPORTED on a chip without an internal EMAC, or with
 * CONFIG_ETH_USE_ESP32_EMAC off. Idempotent. */
esp_err_t espos_eth_start(void);

/** Stop the driver and report the link down. The netif and the driver stay
 * allocated: IDF's Ethernet teardown does not reliably return to a state a
 * later start can build on, and a transport that cannot restart is worse than
 * one that keeps a few KB. A later espos_eth_start() restarts it. */
esp_err_t espos_eth_stop(void);

/** True while the PHY reports a valid link. Independent of addressing: a
 * cable can be plugged in with no DHCP server answering. */
bool espos_eth_link_up(void);

#ifdef __cplusplus
}
#endif
