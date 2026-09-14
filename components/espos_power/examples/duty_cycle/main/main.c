/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * duty_cycle — a device on a battery: wake, publish, sleep, again.
 *
 * espos_start() brings the device up and, because espos_power is in the
 * build, starts the cycle's task last. This file only publishes what the wake
 * is for; espos_power decides when the stream has carried it, flushes, and
 * deep-sleeps for power.interval_s.
 *
 * The cycle is OFF until power.mode is set to "cycle" — in the web UI's Power
 * section, or PUT /api/v1/config {"power": {"mode": "cycle"}}. Deliberately: a
 * freshly flashed device has to stay reachable until it knows its network and
 * its SignalK server, and a device that is asleep is not reachable.
 *
 * Two rules keep it reachable afterwards (docs/power.md): it stays awake for
 * power.window_s after every power-on or update, and it never sleeps while an
 * update is still unconfirmed.
 */
#include "esp_log.h"
#include "espos.h"
#include "espos_power.h"
#include "espos_sk.h"

static const char *TAG = "duty_cycle";

void app_main(void)
{
    ESP_ERROR_CHECK(espos_start(NULL));

    /* Not specification paths, so the server is told what they mean once. */
    (void)espos_sk_declare_meta("sensors.dutyCycle.wakeCount",
                                "{\"description\":\"Timer wakes since the last power-on\"}", 0);
    (void)espos_sk_declare_meta("sensors.dutyCycle.lastAwake",
                                "{\"units\":\"s\",\"description\":\"How long the previous wake lasted\"}", 0);

    /* The reading this wake exists for. A real sensor that needs time to
     * settle takes espos_power_hold() before it starts and
     * espos_power_release() once it has published, so the device does not
     * sleep in between. These two values are ready at once. */
    espos_power_status_t st;
    if (espos_power_get_status(&st) == ESP_OK) {
        ESP_LOGI(TAG, "%s %u, previous wake took %u ms", st.timer_wake ? "wake" : "power-on, wakes so far",
                 (unsigned)st.wake_count, (unsigned)st.last_awake_ms);
        (void)espos_sk_publish_number("sensors.dutyCycle.wakeCount", st.wake_count);
        (void)espos_sk_publish_number("sensors.dutyCycle.lastAwake", st.last_awake_ms / 1000.0);
    }
    /* Buffered until the stream is up, then flushed by espos_power before the
     * device sleeps. app_main may return: espOS's tasks keep running. */
}
