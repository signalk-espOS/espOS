// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The health key. */
#define ESPOS_WIFI_STALL_KEY "wifiStalled"

/* Below this a restart would land before the portal has had a chance to come up
 * and be joined, so a non-zero wifi.stall_s is raised to it. */
#define ESPOS_WIFI_STALL_MIN_MS (300u * 1000u)

/* The value of the driver's stall latch while wifiStalled is raised or held
 * off; anything else is "not latched". */
#define ESPOS_WIFI_STALL_LATCHED 0x5354414cu /* "STAL" */

typedef struct {
    uint32_t since_ms; /* when the current run of trying began (0 = not trying) */
} espos_wifi_stall_t;

/* What the station is doing right now, as the tracker needs it. */
typedef struct {
    bool sta_enabled;
    bool has_networks;
    bool associated;     /* the machine is in OBTAINING_IP or CONNECTED */
    bool other_net_up;   /* another transport carries the network */
    int portal_clients;  /* stations joined to the setup access point */
    bool suppressed;     /* an earlier boot was restarted for this, and nothing has connected since */
} espos_wifi_stall_in_t;

/* Feed one observation; true when the station has been trying without success
 * for at least stall_ms (0 = never). Anything that is not "trying" starts the
 * count again, so the run has to be unbroken. */
bool espos_wifi_stall_update(espos_wifi_stall_t *t, const espos_wifi_stall_in_t *in, uint32_t now_ms,
                             uint32_t stall_ms);

/* Start the count again, for a configuration change: new credentials deserve
 * the full allowance rather than inheriting the time the old ones spent. */
void espos_wifi_stall_reset(espos_wifi_stall_t *t);

#ifdef __cplusplus
}
#endif
