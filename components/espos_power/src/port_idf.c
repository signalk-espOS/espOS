/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_power on a chip: reset reason and wake cause, the OTA image state,
 * RTC memory and deep sleep.
 */
#include "power_port.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"

static const char *TAG = "espos_power";

#define RTC_MAGIC 0x50575231u /* "PWR1" */

/* RTC_NOINIT: survives deep sleep, holds garbage after a power-on. The magic
 * and the check are what tell the two apart, together with the reset reason. */
static RTC_NOINIT_ATTR struct {
    uint32_t magic;
    uint32_t wake_count;
    uint32_t last_awake_ms;
    uint32_t check;
} s_rtc;

static uint32_t rtc_check(uint32_t wake_count, uint32_t last_awake_ms)
{
    return (RTC_MAGIC ^ (wake_count * 2654435761u) ^ last_awake_ms) + 0x9e3779b9u;
}

bool espos_power_port_timer_wake(void)
{
    return esp_reset_reason() == ESP_RST_DEEPSLEEP && (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_TIMER));
}

bool espos_power_port_image_unconfirmed(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    return running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
}

uint32_t espos_power_port_uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void espos_power_port_rtc_load(uint32_t *wake_count, uint32_t *last_awake_ms)
{
    *wake_count = 0;
    *last_awake_ms = 0;
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP || s_rtc.magic != RTC_MAGIC ||
        s_rtc.check != rtc_check(s_rtc.wake_count, s_rtc.last_awake_ms)) {
        return;
    }
    *wake_count = s_rtc.wake_count;
    *last_awake_ms = s_rtc.last_awake_ms;
}

void espos_power_port_rtc_save(uint32_t wake_count, uint32_t last_awake_ms)
{
    s_rtc.wake_count = wake_count;
    s_rtc.last_awake_ms = last_awake_ms;
    s_rtc.check = rtc_check(wake_count, last_awake_ms);
    s_rtc.magic = RTC_MAGIC;
}

esp_err_t espos_power_port_deep_sleep(uint64_t us)
{
    esp_err_t err = esp_sleep_enable_timer_wakeup(us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_sleep_enable_timer_wakeup(%llu): %s", (unsigned long long)us, esp_err_to_name(err));
        return err;
    }
    esp_deep_sleep_start();
}
