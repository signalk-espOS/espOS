/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_power — a duty cycle for devices on a battery: wake, get on the
 * network, publish, flush, deep-sleep for power.interval_s, again.
 *
 * Off by default (power.mode = off). With the component in the build,
 * espos_start() starts it last; it then watches the network, the SignalK
 * stream and the application's holds, and sleeps when the policy in
 * espos_power_policy.h says so. The rules that keep a sleeping device
 * reachable are there too: no sleep while an update is unconfirmed, an awake
 * window after every boot that was not the cycle's own wake, and a deadline on
 * every wake.
 *
 * What is carried through deep sleep is small and lives in RTC memory: the
 * wake counter and how long the previous wake took. The wall clock is carried
 * by espos_time. A power-on starts the counter again.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "espos_power_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the duty cycle's task. espos_start() calls this when the component is
 * built; call it yourself after espos_start_network() in a two-phase boot.
 * Reads the power config namespace and follows changes to it. Idempotent.
 */
esp_err_t espos_power_start(void);

/**
 * Keep the device awake while the application does something the policy
 * cannot see — a sensor that needs two seconds to settle, a reading still on
 * its way. Counted: every hold needs one release. A hold does not outlast the
 * wake's deadline (power.awake_max_s). Safe from any task.
 */
void espos_power_hold(void);
void espos_power_release(void);

/**
 * Flush the SignalK stream and sleep for power.interval_s now, whatever the
 * policy would say, except that an unconfirmed update still refuses
 * (ESP_ERR_INVALID_STATE): sleeping would roll it back. Does not return on
 * success. ESP_ERR_NOT_SUPPORTED where there is no deep sleep (the host).
 */
esp_err_t espos_power_sleep_now(void);

typedef struct {
    bool enabled;              /* power.mode is cycle */
    bool timer_wake;           /* this boot is the cycle's own wake */
    uint32_t wake_count;       /* timer wakes since the last power-on */
    uint32_t last_awake_ms;    /* how long the previous wake lasted; 0 when unknown */
    uint32_t interval_s;       /* the sleep between wakes */
    uint32_t deadline_ms;      /* uptime at which this boot sleeps regardless */
    uint32_t uptime_ms;
    uint32_t holds;
    espos_power_decision_t decision; /* the policy's latest answer */
    espos_power_why_t why;
} espos_power_status_t;

/** A snapshot for the application and GET /api/v1/power. Safe from any task.
 * ESP_ERR_INVALID_STATE before espos_power_start(). */
esp_err_t espos_power_get_status(espos_power_status_t *out);

#ifdef __cplusplus
}
#endif
