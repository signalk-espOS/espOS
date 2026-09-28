/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * /api/v1/health — read what the device thinks is wrong, and rehearse a fault.
 *
 * Until this existed espos_health reached the outside world only as SignalK
 * notifications and whatever `last_reset` showed in /api/v1/system/info, so "why
 * is this device's LED red" was answerable only by reading its log (espOS #137).
 *
 * Lives in espos_httpd rather than espos_health because espos_httpd already
 * PRIV_REQUIRES espos_health: the other direction is a dependency cycle.
 */
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "espos_health.h"
#include "espos_httpd.h"
#include "espos_httpd_priv.h"

/* Enough for CONFIG_ESPOS_HEALTH_MAX_CONDITIONS; snapshot() reports the true
 * count, so a table grown past this is noticed rather than silently truncated. */
#define SNAPSHOT_MAX 32

static esp_err_t health_get(httpd_req_t *req)
{
    /* Nothing else drives the backstop when the policy is not running, and a
     * reader asking what is wrong should not be told about a drill that has
     * already timed out. */
    espos_health_test_expire();

    espos_health_condition_t cond[SNAPSHOT_MAX];
    size_t total = espos_health_snapshot(cond, SNAPSHOT_MAX);
    size_t shown = total < SNAPSHOT_MAX ? total : SNAPSHOT_MAX;

    cJSON *j = cJSON_CreateObject();
    if (!j) {
        return espos_httpd_send_error(req, "500 Internal Server Error", "no_memory", "out of memory");
    }
    cJSON_AddStringToObject(j, "worst", espos_health_state_str(espos_health_worst()));

    /* What the policy would restart for, if anything -- the one thing a reader
     * cannot work out from the list, since it depends on the flags a condition
     * was raised with rather than on its state. */
    espos_health_condition_t fatal;
    if (espos_health_fatal_alarm(&fatal)) {
        cJSON_AddStringToObject(j, "fatal", fatal.key);
    } else {
        cJSON_AddNullToObject(j, "fatal");
    }

    cJSON *arr = cJSON_AddArrayToObject(j, "conditions");
    if (!arr) {
        cJSON_Delete(j);
        return espos_httpd_send_error(req, "500 Internal Server Error", "no_memory", "out of memory");
    }
    for (size_t i = 0; i < shown; i++) {
        cJSON *c = cJSON_CreateObject();
        if (!c) {
            cJSON_Delete(j);
            return espos_httpd_send_error(req, "500 Internal Server Error", "no_memory", "out of memory");
        }
        cJSON_AddStringToObject(c, "key", cond[i].key);
        cJSON_AddStringToObject(c, "state", espos_health_state_str(cond[i].state));
        cJSON_AddStringToObject(c, "message", cond[i].message);
        /* The flag, not the whole bitmask: a client wants to know whether this
         * one can restart the device, not to decode ESPOS_HEALTH_F_* values it
         * would then have to keep in step with the firmware. */
        cJSON_AddBoolToObject(c, "reboot_on_alarm", (cond[i].flags & ESPOS_HEALTH_F_REBOOT_ON_ALARM) != 0);
        cJSON_AddItemToArray(arr, c);
    }
    if (total > shown) {
        cJSON_AddNumberToObject(j, "truncated", (double)(total - shown));
    }

    char *out = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!out) {
        return espos_httpd_send_error(req, "500 Internal Server Error", "no_memory", "out of memory");
    }
    esp_err_t err = espos_httpd_send_json(req, NULL, out);
    free(out);
    return err;
}

/* POST {"key":"test.buzzer","state":"alarm","message":"drill","ttl_s":45}
 *
 * Exists so a hardware test can exercise the buzzer/LED/notification path on a
 * boxed-up board without causing a real fault. Every built-in alarm carries
 * ESPOS_HEALTH_F_REBOOT_ON_ALARM, so triggering one ends the observation it was
 * supposed to allow. espos_health_report_test() forces flags to 0, so this
 * endpoint is structurally incapable of arming that path.
 */
static esp_err_t health_test_post(httpd_req_t *req)
{
    if (!espos_httpd_require_json(req)) {
        return ESP_OK;
    }
    char *body = NULL;
    size_t len = 0;
    if (espos_httpd_read_body(req, &body, &len) != ESP_OK) {
        return ESP_FAIL;
    }
    cJSON *j = cJSON_ParseWithLength(body, len);
    free(body);
    if (!j) {
        return espos_httpd_send_error(req, "400 Bad Request", "bad_request", "not JSON");
    }

    const cJSON *key = cJSON_GetObjectItem(j, "key");
    const cJSON *state = cJSON_GetObjectItem(j, "state");
    const cJSON *message = cJSON_GetObjectItem(j, "message");
    const cJSON *ttl = cJSON_GetObjectItem(j, "ttl_s");

    if (!cJSON_IsString(key) || !cJSON_IsString(state)) {
        cJSON_Delete(j);
        return espos_httpd_send_error(req, "400 Bad Request", "validation", "key and state are required strings");
    }

    espos_health_state_t st;
    if (strcmp(state->valuestring, "normal") == 0) {
        st = ESPOS_HEALTH_NORMAL;
    } else if (strcmp(state->valuestring, "warn") == 0) {
        st = ESPOS_HEALTH_WARN;
    } else if (strcmp(state->valuestring, "alarm") == 0) {
        st = ESPOS_HEALTH_ALARM;
    } else {
        cJSON_Delete(j);
        return espos_httpd_send_error(req, "400 Bad Request", "validation", "state must be normal, warn or alarm");
    }

    /* Default 45 s rather than 30: a repeating alarm has to be watched for
     * several cycles to be believed, and an espOS consumer measured its own
     * morse-code buzzer loop at 10-11 s per pass (espOS #137), so 30 s catches
     * two cycles and not comfortably three. Rejected rather than clamped when
     * out of range -- silently doing something other than what was asked is how
     * a test ends up proving the wrong thing. */
    uint32_t ttl_ms = 45u * 1000u;
    if (cJSON_IsNumber(ttl)) {
        double v = ttl->valuedouble;
        if (v < 1.0 || v > (double)(ESPOS_HEALTH_TEST_TTL_MAX_MS / 1000u)) {
            cJSON_Delete(j);
            return espos_httpd_send_error(req, "400 Bad Request", "validation",
                                          "ttl_s must be 1..300");
        }
        ttl_ms = (uint32_t)(v * 1000.0);
    } else if (ttl && !cJSON_IsNull(ttl)) {
        cJSON_Delete(j);
        return espos_httpd_send_error(req, "400 Bad Request", "validation", "ttl_s must be a number");
    }

    /* Same treatment as ttl_s below: a field that is present but the wrong type
     * is a caller mistake, and silently substituting "" would hide it. */
    if (message && !cJSON_IsNull(message) && !cJSON_IsString(message)) {
        cJSON_Delete(j);
        return espos_httpd_send_error(req, "400 Bad Request", "validation", "message must be a string");
    }

    esp_err_t err = espos_health_report_test(key->valuestring, st, cJSON_IsString(message) ? message->valuestring : "",
                                             ttl_ms);
    cJSON_Delete(j);

    if (err == ESP_ERR_INVALID_ARG) {
        return espos_httpd_send_error(req, "400 Bad Request", "validation",
                                      "key must start with \"" ESPOS_HEALTH_TEST_PREFIX "\" and name something");
    }
    if (err == ESP_ERR_INVALID_SIZE) {
        /* espos_health_report_ex() refuses rather than truncates. A caller's
         * oversized string is its mistake to fix, so 400 -- reporting it as a
         * 500 would send someone looking for a fault in the device. */
        return espos_httpd_send_error(req, "400 Bad Request", "validation",
                                      "key must be under 24 bytes and message under 96");
    }
    if (err == ESP_ERR_NO_MEM) {
        /* Not a 500: the caller can fix this, either by reusing a key it has
         * already used this boot (a key keeps its slot) or by raising
         * CONFIG_ESPOS_HEALTH_MAX_CONDITIONS. */
        return espos_httpd_send_error(req, "507 Insufficient Storage", "no_slot",
                                      "the condition table is full; reuse a test key or raise "
                                      "CONFIG_ESPOS_HEALTH_MAX_CONDITIONS");
    }
    if (err != ESP_OK) {
        return espos_httpd_send_error(req, "500 Internal Server Error", "report_failed", esp_err_to_name(err));
    }
    return health_get(req);
}

esp_err_t espos_httpd_register_health_api(void)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/v1/health", .method = HTTP_GET, .handler = health_get },
        { .uri = "/api/v1/health/test", .method = HTTP_POST, .handler = health_test_post },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        /* Protected, both of them. Reading tells an unauthenticated caller what
         * is wrong with the boat; writing puts a fake alarm on a chartplotter. */
        esp_err_t err = espos_httpd_register_ex(&uris[i], ESPOS_HTTPD_PROTECTED);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
