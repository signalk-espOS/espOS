/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_power on the linux target: every boot is a power-on, every image is
 * confirmed, and a request to sleep is refused -- the process has no deep
 * sleep to enter and no memory that outlives it.
 */
#include "power_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "espos_power";

bool espos_power_port_timer_wake(void)
{
    return false;
}

bool espos_power_port_image_unconfirmed(void)
{
    return false;
}

uint32_t espos_power_port_uptime_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void espos_power_port_rtc_load(uint32_t *wake_count, uint32_t *last_awake_ms)
{
    *wake_count = 0;
    *last_awake_ms = 0;
}

void espos_power_port_rtc_save(uint32_t wake_count, uint32_t last_awake_ms)
{
    (void)wake_count;
    (void)last_awake_ms;
}

esp_err_t espos_power_port_deep_sleep(uint64_t us)
{
    ESP_LOGW(TAG, "would deep-sleep for %llu ms; the host cannot", (unsigned long long)(us / 1000));
    return ESP_ERR_NOT_SUPPORTED;
}
