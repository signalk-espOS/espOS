// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A Zigbee or Thread build: the 802.15.4 driver is in the build and the
 * software arbiter shares the radio between it and the station. */
#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE && CONFIG_SOC_IEEE802154_SUPPORTED && CONFIG_IEEE802154_ENABLED
#define ESPOS_WIFI_I154_COEX 1
#else
#define ESPOS_WIFI_I154_COEX 0
#endif

/* The power-save mode the driver gets for `configured` (wifi.ps_mode). With
 * 802.15.4 on the same radio, "none" (and anything the driver would read as
 * "none") runs as "min": a station that never sleeps leaves 802.15.4 too
 * little air time to hear a device that is trying to join. "min" and "max"
 * pass through unchanged. */
const char *espos_wifi_ps_effective(const char *configured, bool i154_coex);

#ifdef __cplusplus
}
#endif
