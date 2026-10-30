// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
/*
 * wifiStalled: the station has been trying, without one success, for longer
 * than any outage a restart would not cure.
 *
 * The case it exists for (espOS #136): an ESP32-P4 panel retried a known-good
 * AP every 11 s for 45 hours and recovered at once on a reset. Nothing else
 * watches that. netDown is a warning by design, and skLinkStalled needs WiFi to
 * report CONNECTED, which a station that never associates never does.
 *
 * "Without success" means without associating. A station that associates and
 * then gets no lease has a working radio and a broken DHCP server, which a
 * restart does not mend.
 *
 * Split out of espos_wifi.c so the decision is host-tested; every way of
 * getting it wrong is a restart nobody wanted. Not trying, and so never
 * stalled:
 *  - the station disabled, or no network configured: there is nothing a
 *    restart would let it reach;
 *  - another transport carrying the network: the device is reachable, and a
 *    restart would cut a working link to fix an idle one;
 *  - someone on the setup access point: they are about to fix the cause, and a
 *    restart under them throws their page away;
 *  - suppressed: an earlier boot was already restarted for this and nothing
 *    has connected since. That is the AP being off -- a marina switching the
 *    router off, a boat left on battery -- and a second restart would only
 *    start a loop, one every stall_s, for as long as it stays off.
 */

#include "wifi_stall.h"

#include <stddef.h>

bool espos_wifi_stall_update(espos_wifi_stall_t *t, const espos_wifi_stall_in_t *in, uint32_t now_ms,
                             uint32_t stall_ms)
{
    if (!t || !in) {
        return false;
    }
    bool trying = in->sta_enabled && in->has_networks && !in->associated && !in->other_net_up &&
                  in->portal_clients <= 0 && !in->suppressed;
    if (!trying || stall_ms == 0) {
        t->since_ms = 0;
        return false;
    }
    if (t->since_ms == 0) {
        t->since_ms = now_ms ? now_ms : 1; /* 0 means "not trying" */
        return false;
    }
    return now_ms - t->since_ms >= stall_ms;
}

void espos_wifi_stall_reset(espos_wifi_stall_t *t)
{
    if (t) {
        t->since_ms = 0;
    }
}
