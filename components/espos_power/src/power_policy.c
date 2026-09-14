/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_power policy — see espos_power_policy.h. The order of the checks is
 * the specification: each earlier rule wins over every later one.
 */
#include "espos_power_policy.h"

#include <stddef.h>

static espos_power_decision_t decided(espos_power_why_t *why, espos_power_why_t w)
{
    if (why) {
        *why = w;
    }
    return (w == ESPOS_POWER_WHY_DONE || w == ESPOS_POWER_WHY_DEADLINE) ? ESPOS_POWER_SLEEP : ESPOS_POWER_STAY;
}

uint32_t espos_power_policy_deadline_ms(const espos_power_policy_cfg_t *cfg, bool timer_wake)
{
    if (!cfg) {
        return UINT32_MAX;
    }
    if (timer_wake) {
        return cfg->awake_max_ms;
    }
    return cfg->window_ms > UINT32_MAX - cfg->awake_max_ms ? UINT32_MAX : cfg->window_ms + cfg->awake_max_ms;
}

espos_power_decision_t espos_power_policy_decide(const espos_power_policy_cfg_t *cfg, const espos_power_policy_in_t *in,
                                                 espos_power_why_t *why)
{
    if (!cfg || !in || !in->enabled) {
        return decided(why, ESPOS_POWER_WHY_OFF);
    }
    /* Before the deadline, deliberately: sleeping on an unconfirmed image
     * rolls the update back at the next boot. If the image cannot confirm,
     * espos_ota rolls it back itself after ota.confirm_tmo_s. */
    if (in->image_unconfirmed) {
        return decided(why, ESPOS_POWER_WHY_UNCONFIRMED);
    }
    /* Also before the deadline: a wake lasts seconds, and an update longer
     * than one would otherwise be cut off on every wake and never finish.
     * espos_ota's own timeouts end a download that hangs. */
    if (in->ota_busy) {
        return decided(why, ESPOS_POWER_WHY_OTA);
    }
    if (!in->timer_wake && in->uptime_ms < cfg->window_ms) {
        return decided(why, ESPOS_POWER_WHY_WINDOW);
    }
    if (in->uptime_ms >= espos_power_policy_deadline_ms(cfg, in->timer_wake)) {
        return decided(why, ESPOS_POWER_WHY_DEADLINE);
    }
    if (!in->net_up) {
        return decided(why, ESPOS_POWER_WHY_NETWORK);
    }
    if (in->have_stream && !in->stream_connected) {
        return decided(why, ESPOS_POWER_WHY_STREAM);
    }
    uint32_t ready_for = in->have_stream ? in->stream_up_for_ms : in->net_up_for_ms;
    if (ready_for < cfg->publish_ms) {
        return decided(why, ESPOS_POWER_WHY_PUBLISHING);
    }
    if (in->holds > 0) {
        return decided(why, ESPOS_POWER_WHY_HOLD);
    }
    return decided(why, ESPOS_POWER_WHY_DONE);
}

const char *espos_power_why_str(espos_power_why_t why)
{
    static const char *const names[ESPOS_POWER_WHY_MAX] = {
        [ESPOS_POWER_WHY_OFF] = "off",
        [ESPOS_POWER_WHY_UNCONFIRMED] = "unconfirmed",
        [ESPOS_POWER_WHY_WINDOW] = "window",
        [ESPOS_POWER_WHY_NETWORK] = "network",
        [ESPOS_POWER_WHY_STREAM] = "stream",
        [ESPOS_POWER_WHY_PUBLISHING] = "publishing",
        [ESPOS_POWER_WHY_HOLD] = "hold",
        [ESPOS_POWER_WHY_DONE] = "done",
        [ESPOS_POWER_WHY_DEADLINE] = "deadline",
        [ESPOS_POWER_WHY_OTA] = "ota",
    };
    return (why >= 0 && why < ESPOS_POWER_WHY_MAX) ? names[why] : "?";
}
