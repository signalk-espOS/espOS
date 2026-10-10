/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * esp_hosted's Wi-Fi + BLE co-processor (examples/bluetooth/esp_hosted_nimble/
 * bleprph_wifi_coex/cp): the transport, Wi-Fi and the BLE controller all start
 * from esp_hosted's own init, so the application only brings up NVS and
 * confirms the image once a host has talked to it.
 */
#include "eh_cp_event.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "espos_cp";

/* The bootloader on the C6 is whatever it shipped with, and an update over the
 * link does not replace it. If that one was built with app rollback, an image
 * left unconfirmed is rolled back at the next reset, which the P4 host
 * performs at every start. So the image is confirmed only once a host has
 * negotiated with it over the transport: one that boots but never reaches the
 * host stays unconfirmed and the host's next reset takes it back to the image
 * that worked. A no-op when rollback is off. */
static void on_rpc_ready(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGI(TAG, "host connected; image confirmed");
    }
}

void app_main(void)
{
    /* First: esp_hosted's init runs in its own task from a constructor, and
     * the host's negotiation can only come after this chip announced itself
     * and the host restarted to meet it, which is seconds away. Already
     * created by esp_hosted is fine. */
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }
    ESP_ERROR_CHECK(esp_event_handler_register(EH_CP_EVENT, EH_CP_EVT_PRIVATE_RPC_READY, on_rpc_ready, NULL));

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "espOS co-processor (esp_hosted Wi-Fi + BLE) running");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
