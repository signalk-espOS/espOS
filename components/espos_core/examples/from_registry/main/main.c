/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * from_registry — the same application as `minimal`, built without the
 * prologue: espOS installed from the component registry, nothing on disk but
 * this directory. The C is deliberately identical, because the point of the
 * example is the four files around it, not the code.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "espos.h"
#include "espos_sk.h"

static const char *TAG = "from_registry";

/* A Signal K path the server already knows the unit of (kelvin), so nothing
 * has to be declared. A path of your own needs espos_sk_declare_meta() once,
 * before the first publish. */
#define PATH "environment.inside.temperature"

void app_main(void)
{
    /* log → config → httpd → wifi → sk → ota, in the one order that works.
     * The network may still be coming up when this returns; the portal, the
     * server search and the access request happen in the background and are
     * narrated on the monitor. */
    ESP_ERROR_CHECK(espos_start(NULL));
    ESP_LOGI(TAG, "publishing %s every second", PATH);

    for (;;) {
        /* 293.65 K is 20.5 °C. Never blocks: until the stream is up the value
         * is batched, and the backlog drains oldest first once it is. */
        espos_sk_publish_number(PATH, 293.65);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
