/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_health — condition table + sink registry. See espos_health.h; the
 * watchdog policy that reads this table is health_policy.c / health_watchdog.c.
 *
 * Everything is a fixed table: the set of conditions a firmware can raise and
 * the set of things that care are both decided at build time, so there is
 * nothing here worth a heap allocation and nothing that can fragment.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "espos_health.h"
#include "health_port.h"

static const char *TAG = "espos_health";

#define MAX_CONDITIONS CONFIG_ESPOS_HEALTH_MAX_CONDITIONS
#define MAX_SINKS      CONFIG_ESPOS_HEALTH_MAX_SINKS

typedef struct {
    espos_health_sink_t fn;
    void *arg;
} sink_t;

static struct {
    espos_health_condition_t cond[MAX_CONDITIONS];
    size_t cond_n;
    sink_t sink[MAX_SINKS];
    size_t sink_n;
    SemaphoreHandle_t lock;
    /* The one synthetic condition (espos_health_report_test). One, not a set: a
     * drill on a live boat should have a bounded blast radius. */
    char test_key[ESPOS_HEALTH_KEY_MAX];
    uint32_t test_deadline_ms;
    bool test_armed;
    /* Serialises a whole drill transition, which spans several report_ex() calls
     * and so cannot be done under `lock` -- that one must not be held across a
     * sink. LOCK ORDER: test_lock first, then `lock`, never the reverse. */
    SemaphoreHandle_t test_lock;
} s;

/* Created before app_main by the C runtime, so espos_health_report() works
 * from anywhere without an init call and without the double-checked locking a
 * lazy mutex needs — that pattern relies on an unsynchronised read, which is
 * exactly the kind of thing that works until it does not. */
static void __attribute__((constructor)) health_init(void)
{
    s.lock = xSemaphoreCreateMutex();
    s.test_lock = xSemaphoreCreateMutex();
}

static bool lock(void)
{
    /* A failure here means the constructor did not run (host builds that link
     * only part of the runtime); report rather than crash. */
    if (!s.lock) return false;
    return xSemaphoreTake(s.lock, pdMS_TO_TICKS(200)) == pdTRUE;
}

static void unlock(void)
{
    xSemaphoreGive(s.lock);
}

const char *espos_health_state_str(espos_health_state_t state)
{
    switch (state) {
    case ESPOS_HEALTH_ALARM: return "alarm";
    case ESPOS_HEALTH_WARN: return "warn";
    default: return "normal";
    }
}

/* Call every sink with the lock released: a sink may report a condition of its
 * own, and espos_sk's sink publishes a delta while holding its own lock. */
static void fan_out(const char *key, espos_health_state_t state, const char *message)
{
    sink_t snapshot[MAX_SINKS];
    size_t n;

    if (!lock()) return;
    n = s.sink_n;
    memcpy(snapshot, s.sink, n * sizeof(snapshot[0]));
    unlock();

    for (size_t i = 0; i < n; i++) {
        snapshot[i].fn(key, state, message, snapshot[i].arg);
    }
}

esp_err_t espos_health_report(const char *key, espos_health_state_t state, const char *message)
{
    return espos_health_report_ex(key, state, message, 0);
}

esp_err_t espos_health_report_ex(const char *key, espos_health_state_t state, const char *message, uint32_t flags)
{
    if (!key || !key[0]) return ESP_ERR_INVALID_ARG;
    if (flags & ~ESPOS_HEALTH_F_REBOOT_ON_ALARM) return ESP_ERR_INVALID_ARG;
    if (!message) message = "";
    if (strlen(key) >= ESPOS_HEALTH_KEY_MAX) return ESP_ERR_INVALID_SIZE;
    if (strlen(message) >= ESPOS_HEALTH_MSG_MAX) return ESP_ERR_INVALID_SIZE;
    if (state != ESPOS_HEALTH_NORMAL && state != ESPOS_HEALTH_WARN &&
        state != ESPOS_HEALTH_ALARM) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!lock()) return ESP_ERR_TIMEOUT;

    espos_health_condition_t *c = NULL;
    for (size_t i = 0; i < s.cond_n; i++) {
        if (strcmp(s.cond[i].key, key) == 0) {
            c = &s.cond[i];
            break;
        }
    }
    if (!c) {
        /* No early-out for a first NORMAL, even though recording "nothing is
         * wrong" looks wasteful: after a reboot a sink's far end may still hold
         * an alert this device raised before it restarted, and the clear that
         * retires it is exactly a first report with NORMAL. */
        if (s.cond_n >= MAX_CONDITIONS) {
            unlock();
            ESP_LOGW(TAG, "no slot for condition '%s' (max %d)", key, MAX_CONDITIONS);
            return ESP_ERR_NO_MEM;
        }
        c = &s.cond[s.cond_n++];
        snprintf(c->key, sizeof(c->key), "%s", key);
        c->state = (espos_health_state_t)-1;  /* forces the first fan-out */
        c->message[0] = '\0';
        c->flags = 0;
    } else if (c->state == state && strcmp(c->message, message) == 0) {
        /* Unchanged for the sinks — stay quiet. The flags still follow the
         * latest report: they are the policy's business, not a sink's. */
        c->flags = flags;
        unlock();
        return ESP_OK;
    }

    c->state = state;
    c->flags = flags;
    snprintf(c->message, sizeof(c->message), "%s", message);
    unlock();

    if (state == ESPOS_HEALTH_NORMAL) {
        ESP_LOGI(TAG, "%s: normal", key);
    } else {
        ESP_LOGW(TAG, "%s: %s (%s)", key, espos_health_state_str(state), message);
    }
    fan_out(key, state, message);
    return ESP_OK;
}

esp_err_t espos_health_add_sink(espos_health_sink_t sink, void *arg)
{
    if (!sink) return ESP_ERR_INVALID_ARG;
    if (!lock()) return ESP_ERR_TIMEOUT;

    for (size_t i = 0; i < s.sink_n; i++) {
        if (s.sink[i].fn == sink && s.sink[i].arg == arg) {
            unlock();
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (s.sink_n >= MAX_SINKS) {
        unlock();
        return ESP_ERR_NO_MEM;
    }
    s.sink[s.sink_n].fn = sink;
    s.sink[s.sink_n].arg = arg;
    s.sink_n++;

    unlock();

    /* Replay with the lock released — same reason as fan_out(). One condition
     * at a time rather than a copy of the whole table: this runs on the
     * caller's task, and a table's worth of conditions is a kilobyte of stack
     * a small task has better uses for. The sink is registered before the
     * replay starts, so a condition reported meanwhile can reach it twice —
     * harmless, because a condition is a level and not an edge. Losing one
     * would not be, which is why the registration comes first. */
    for (size_t i = 0;; i++) {
        espos_health_condition_t c;
        if (!lock()) break;
        if (i >= s.cond_n) {
            unlock();
            break;
        }
        c = s.cond[i];
        unlock();
        sink(c.key, c.state, c.message, arg);
    }
    return ESP_OK;
}

esp_err_t espos_health_remove_sink(espos_health_sink_t sink, void *arg)
{
    if (!lock()) return ESP_ERR_TIMEOUT;
    for (size_t i = 0; i < s.sink_n; i++) {
        if (s.sink[i].fn == sink && s.sink[i].arg == arg) {
            s.sink[i] = s.sink[--s.sink_n];
            unlock();
            return ESP_OK;
        }
    }
    unlock();
    return ESP_ERR_NOT_FOUND;
}

size_t espos_health_snapshot(espos_health_condition_t *out, size_t max)
{
    if (!lock()) return 0;
    size_t n = s.cond_n;
    if (out) {
        size_t copy = n < max ? n : max;
        memcpy(out, s.cond, copy * sizeof(*out));
    }
    unlock();
    return n;
}

espos_health_state_t espos_health_worst(void)
{
    espos_health_state_t worst = ESPOS_HEALTH_NORMAL;
    if (!lock()) return worst;
    for (size_t i = 0; i < s.cond_n; i++) {
        if (s.cond[i].state > worst) worst = s.cond[i].state;
    }
    unlock();
    return worst;
}

bool espos_health_fatal_alarm(espos_health_condition_t *out)
{
    if (!lock()) return false;
    for (size_t i = 0; i < s.cond_n; i++) {
        const espos_health_condition_t *c = &s.cond[i];
        if (c->state == ESPOS_HEALTH_ALARM && (c->flags & ESPOS_HEALTH_F_REBOOT_ON_ALARM)) {
            if (out) *out = *c;
            unlock();
            return true;
        }
    }
    unlock();
    return false;
}

/* ------------------------------------------------------- synthetic conditions */

esp_err_t espos_health_report_test(const char *key, espos_health_state_t state, const char *message,
                                   uint32_t ttl_ms)
{
    if (!key || strncmp(key, ESPOS_HEALTH_TEST_PREFIX, strlen(ESPOS_HEALTH_TEST_PREFIX)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Nothing past the prefix would give a condition named just "test.", which
     * reads as a bug rather than as a drill of something. */
    if (key[strlen(ESPOS_HEALTH_TEST_PREFIX)] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (state != ESPOS_HEALTH_NORMAL && state != ESPOS_HEALTH_WARN && state != ESPOS_HEALTH_ALARM) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool raising = (state != ESPOS_HEALTH_NORMAL);
    if (raising && (ttl_ms == 0 || ttl_ms > ESPOS_HEALTH_TEST_TTL_MAX_MS)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* The same limits espos_health_report_ex() enforces, checked here so that an
     * oversized string is refused BEFORE anything changes. Otherwise a malformed
     * request would end a drill that is currently running, which is a side
     * effect no caller asked for. */
    if (strlen(key) >= ESPOS_HEALTH_KEY_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (message && strlen(message) >= ESPOS_HEALTH_MSG_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* One drill transition at a time. Without this two concurrent callers can
     * each clear the other's drill and then both arm, leaving one key raised that
     * s.test_key no longer names -- a fake alarm no ttl will ever clear. */
    if (!s.test_lock || xSemaphoreTake(s.test_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    char previous[ESPOS_HEALTH_KEY_MAX] = { 0 };
    if (!lock()) {
        xSemaphoreGive(s.test_lock);
        return ESP_ERR_TIMEOUT;
    }
    if (s.test_armed && strcmp(s.test_key, key) != 0) {
        snprintf(previous, sizeof(previous), "%s", s.test_key);
    }
    /* Armed in the SAME critical section that read the old state, before the
     * report rather than after it. Arming afterwards needs a second lock(), and a
     * lock() that times out once the report has already gone out leaves a drill
     * raised with nothing tracking it -- an alarm no ttl will ever clear, which is
     * the failure this bookkeeping exists to prevent.
     *
     * Doing it first means an in-memory rollback if the report then fails, and a
     * rollback that cannot itself leave anything raised. */
    char saved_key[ESPOS_HEALTH_KEY_MAX];
    memcpy(saved_key, s.test_key, sizeof(saved_key));
    const uint32_t saved_deadline = s.test_deadline_ms;
    const bool saved_armed = s.test_armed;
    if (raising) {
        snprintf(s.test_key, sizeof(s.test_key), "%s", key);
        s.test_deadline_ms = espos_health_port_now_ms() + ttl_ms;
        s.test_armed = true;
    } else if (strcmp(s.test_key, key) == 0) {
        s.test_armed = false;
        s.test_key[0] = '\0';
    }
    unlock();

    /* The new report before clearing the previous drill, so that a failure here --
     * the condition table being full is the one that survives the checks above --
     * leaves the running drill exactly as it was. The cost is a moment in which
     * both keys are raised, which a sink may observe; preserving somebody's
     * running drill is worth more than the order of two fan-outs, and no observer
     * outside this call can see the overlap.
     *
     * flags 0, always: this cannot arm ESPOS_HEALTH_F_REBOOT_ON_ALARM whatever a
     * real condition of the same name would carry. Structural, not a promise. */
    esp_err_t err = espos_health_report_ex(key, state, message ? message : "", 0);
    if (err != ESP_OK) {
        /* Put the bookkeeping back. Should this lock() time out too, the state
         * names a key that is not raised, so the worst the next tick does is
         * report NORMAL for something already normal -- a no-op, not an orphan. */
        if (lock()) {
            memcpy(s.test_key, saved_key, sizeof(s.test_key));
            s.test_deadline_ms = saved_deadline;
            s.test_armed = saved_armed;
            unlock();
        }
        xSemaphoreGive(s.test_lock);
        return err;
    }
    if (previous[0]) {
        espos_health_report_ex(previous, ESPOS_HEALTH_NORMAL, "", 0);
    }
    xSemaphoreGive(s.test_lock);
    return ESP_OK;
}

bool espos_health_test_expire(void)
{
    /* Same lock as report_test, in the same order: without it a drill re-armed
     * at the moment its predecessor fell due could be cleared by this call
     * instead, which would stop a buzzer somebody had just started. */
    if (!s.test_lock || xSemaphoreTake(s.test_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return false;
    }
    char due[ESPOS_HEALTH_KEY_MAX] = { 0 };
    if (!lock()) {
        xSemaphoreGive(s.test_lock);
        return false;
    }
    /* Subtraction, not `now >= deadline`: now_ms is a uint32 of milliseconds and
     * wraps every 49.7 days, so a comparison would fire the whole ttl early for
     * a drill started just before the wrap. The difference is correct across it. */
    if (s.test_armed && (int32_t)(espos_health_port_now_ms() - s.test_deadline_ms) >= 0) {
        snprintf(due, sizeof(due), "%s", s.test_key);
        s.test_armed = false;
        s.test_key[0] = '\0';
    }
    unlock();
    if (!due[0]) {
        xSemaphoreGive(s.test_lock);
        return false;
    }
    /* Outside `lock`: the fan-out calls sinks, which may do anything. Still
     * inside test_lock, so no drill can be armed mid-clear. */
    ESP_LOGI(TAG, "%s: test condition expired", due);
    espos_health_report_ex(due, ESPOS_HEALTH_NORMAL, "", 0);
    xSemaphoreGive(s.test_lock);
    return true;
}

void espos_health_reset(void)
{
    if (!lock()) return;
    s.cond_n = 0;
    s.sink_n = 0;
    s.test_armed = false;
    s.test_key[0] = '\0';
    unlock();
}
