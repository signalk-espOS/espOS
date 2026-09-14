/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * What espos_power needs from the platform: why this boot happened, whether
 * the running image is confirmed, a clock, a scrap of memory that survives
 * deep sleep, and the sleep itself. port_idf.c on chips; port_sim.c on the
 * linux target, where nothing sleeps and nothing outlives the process.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** True when this boot is a wake from deep sleep caused by the timer. */
bool espos_power_port_timer_wake(void);

/** True while the running OTA image is still pending verification. */
bool espos_power_port_image_unconfirmed(void);

/** Milliseconds since boot. */
uint32_t espos_power_port_uptime_ms(void);

/**
 * The counters a previous boot saved before sleeping. Zeros unless this boot
 * is a wake from deep sleep and the record is intact: after a power-on the RTC
 * memory holds whatever the last power cycle left.
 */
void espos_power_port_rtc_load(uint32_t *wake_count, uint32_t *last_awake_ms);
void espos_power_port_rtc_save(uint32_t wake_count, uint32_t last_awake_ms);

/** Deep-sleep for `us` microseconds with a timer wake. Does not return on a
 * chip; ESP_ERR_NOT_SUPPORTED on the host. */
esp_err_t espos_power_port_deep_sleep(uint64_t us);
