/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "unity.h"
#include "espos_sk.h"
#include "espos_sk_priv.h"

/* A delta frame for one path, fed through the same entry point the stream
 * task uses, so dispatch runs exactly as on a device. */
static void feed(const char *path)
{
    char json[160];
    snprintf(json, sizeof(json), "{\"context\":\"vessels.self\",\"updates\":[{\"values\":[{\"path\":\"%s\",\"value\":1}]}]}", path);
    TEST_ASSERT_TRUE(espos_sk_inbound_handle_frame(json, strlen(json), NULL, 0));
}

/* ------------------------------------------- unsubscribe from another task */

static struct {
    SemaphoreHandle_t entered;   /* the callback has started */
    SemaphoreHandle_t release;   /* lets the callback return */
    SemaphoreHandle_t done;      /* a helper task has finished */
    volatile int step;           /* order of events */
    volatile int cb_end_step;
    volatile int unsub_end_step;
    volatile int calls;
    volatile esp_err_t unsub_err[2];
    volatile int unsub_end[2];
    int handle;
} w;

static void slow_cb(const espos_sk_update_t *u, void *arg)
{
    (void)u;
    (void)arg;
    w.calls++;
    xSemaphoreGive(w.entered);
    xSemaphoreTake(w.release, portMAX_DELAY);
    w.cb_end_step = ++w.step;
}

static void stream_task(void *arg)
{
    feed((const char *)arg);
    xSemaphoreGive(w.done);
    vTaskDelete(NULL);
}

static void unsub_task(void *arg)
{
    int k = (int)(intptr_t)arg;
    /* Unity's asserts unwind the test task only, so the result is checked
     * there. */
    w.unsub_err[k] = espos_sk_unsubscribe(w.handle);
    w.unsub_end[k] = ++w.step;
    xSemaphoreGive(w.done);
    vTaskDelete(NULL);
}

TEST_CASE("unsubscribe waits for a callback that is already running", "[inbound]")
{
    memset(&w, 0, sizeof(w));
    w.entered = xSemaphoreCreateBinary();
    w.release = xSemaphoreCreateBinary();
    w.done = xSemaphoreCreateCounting(3, 0);
    w.handle = espos_sk_subscribe("a.slow", 0, slow_cb, NULL);
    TEST_ASSERT_GREATER_THAN(0, w.handle);

    xTaskCreate(stream_task, "stream", 4096, (void *)"a.slow", 5, NULL);
    TEST_ASSERT_TRUE(xSemaphoreTake(w.entered, pdMS_TO_TICKS(2000)));
    /* Two teardown paths unsubscribing the same handle: the second finds
     * no entry, but its caller may free arg just the same. */
    xTaskCreate(unsub_task, "unsub0", 4096, (void *)(intptr_t)0, 5, NULL);
    vTaskDelay(pdMS_TO_TICKS(20));
    xTaskCreate(unsub_task, "unsub1", 4096, (void *)(intptr_t)1, 5, NULL);

    /* While the callback is held, neither unsubscribe may return. */
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(0, w.unsub_end[0]);
    TEST_ASSERT_EQUAL(0, w.unsub_end[1]);

    xSemaphoreGive(w.release);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(xSemaphoreTake(w.done, pdMS_TO_TICKS(2000)));
    }
    TEST_ASSERT_EQUAL(1, w.cb_end_step);
    TEST_ASSERT_GREATER_THAN(1, w.unsub_end[0]);
    TEST_ASSERT_GREATER_THAN(1, w.unsub_end[1]);
    TEST_ASSERT_EQUAL(ESP_OK, w.unsub_err[0]);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, w.unsub_err[1]);

    /* And nothing arrives for the old handle afterwards. */
    feed("a.slow");
    TEST_ASSERT_EQUAL(1, w.calls);

    vSemaphoreDelete(w.entered);
    vSemaphoreDelete(w.release);
    vSemaphoreDelete(w.done);
}

TEST_CASE("unsubscribe does not wait for another handle's callback", "[inbound]")
{
    memset(&w, 0, sizeof(w));
    w.entered = xSemaphoreCreateBinary();
    w.release = xSemaphoreCreateBinary();
    w.done = xSemaphoreCreateCounting(2, 0);
    int busy = espos_sk_subscribe("a.busy", 0, slow_cb, NULL);
    TEST_ASSERT_GREATER_THAN(0, busy);
    w.handle = espos_sk_subscribe("a.idle", 0, slow_cb, NULL);
    TEST_ASSERT_GREATER_THAN(0, w.handle);

    xTaskCreate(stream_task, "stream", 4096, (void *)"a.busy", 5, NULL);
    TEST_ASSERT_TRUE(xSemaphoreTake(w.entered, pdMS_TO_TICKS(2000)));
    xTaskCreate(unsub_task, "unsub0", 4096, (void *)(intptr_t)0, 5, NULL);
    TEST_ASSERT_TRUE(xSemaphoreTake(w.done, pdMS_TO_TICKS(2000)));
    TEST_ASSERT_EQUAL(1, w.unsub_end[0]);   /* returned while a.busy still runs */
    TEST_ASSERT_EQUAL(ESP_OK, w.unsub_err[0]);

    xSemaphoreGive(w.release);
    TEST_ASSERT_TRUE(xSemaphoreTake(w.done, pdMS_TO_TICKS(2000)));
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(busy));

    vSemaphoreDelete(w.entered);
    vSemaphoreDelete(w.release);
    vSemaphoreDelete(w.done);
}

/* ------------------------------------------------ unsubscribe in a callback */

/* Callbacks record what they saw instead of asserting: a failed assert
 * would unwind out of dispatch() and leave its running-callback state set,
 * so a later test would hang rather than report the failure. */
static int s_self_calls;
static int s_self_handle;
static esp_err_t s_self_err;

static void self_unsub_cb(const espos_sk_update_t *u, void *arg)
{
    (void)u;
    (void)arg;
    s_self_calls++;
    s_self_err = espos_sk_unsubscribe(s_self_handle);
}

TEST_CASE("a callback can unsubscribe itself without waiting on itself", "[inbound]")
{
    s_self_calls = 0;
    s_self_err = ESP_FAIL;
    s_self_handle = espos_sk_subscribe("b.self", 0, self_unsub_cb, NULL);
    TEST_ASSERT_GREATER_THAN(0, s_self_handle);
    feed("b.self");   /* would hang here if unsubscribe waited for its caller */
    feed("b.self");
    TEST_ASSERT_EQUAL(1, s_self_calls);
    TEST_ASSERT_EQUAL(ESP_OK, s_self_err);
}

TEST_CASE("unsubscribe of handle 0 returns at once while nothing runs", "[inbound]")
{
    /* 0 is what cb_handle reads when no callback runs; it is never a valid
     * handle and must not be mistaken for a running one. */
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, espos_sk_unsubscribe(0));
}

static int s_first_calls, s_second_calls;
static int s_second_handle;
static esp_err_t s_second_err;

static void first_cb(const espos_sk_update_t *u, void *arg)
{
    (void)u;
    (void)arg;
    s_first_calls++;
    if (s_second_handle > 0) {
        s_second_err = espos_sk_unsubscribe(s_second_handle);
        s_second_handle = 0;
    }
}

static void second_cb(const espos_sk_update_t *u, void *arg)
{
    (void)u;
    (void)arg;
    s_second_calls++;
}

TEST_CASE("a handle unsubscribed by an earlier callback gets no call", "[inbound]")
{
    s_first_calls = s_second_calls = 0;
    s_second_err = ESP_FAIL;
    int first = espos_sk_subscribe("c.*", 0, first_cb, NULL);
    TEST_ASSERT_GREATER_THAN(0, first);
    s_second_handle = espos_sk_subscribe("c.x", 0, second_cb, NULL);
    TEST_ASSERT_GREATER_THAN(0, s_second_handle);

    /* Both match; the first one, called first, removes the second before
     * its turn in the same dispatch. */
    feed("c.x");
    TEST_ASSERT_EQUAL(1, s_first_calls);
    TEST_ASSERT_EQUAL(0, s_second_calls);
    TEST_ASSERT_EQUAL(ESP_OK, s_second_err);
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(first));
}
