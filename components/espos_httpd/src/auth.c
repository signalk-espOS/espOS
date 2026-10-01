/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * REST authentication: the port of espos_httpd_auth_policy to a device, the
 * central check every protected handler goes through (espos_httpd.c's
 * trampoline calls espos_httpd_auth_enforce), and /api/v1/auth/{login,logout,status}.
 *
 *   Authorization: Bearer <httpd.api_key>   machine clients, the designer, curl
 *   Cookie: espos_sid=<id>                   the web UI after POST /auth/login;
 *                                            EventSource sends it by itself
 *
 * Over plain http the key crosses the LAN the way the SignalK token does:
 * this stops casual and accidental access, not a sniffer (docs/security.md).
 *
 * A request on the soft-AP interface is exempt only while no key is set, or
 * while a recovery window is open; it used to be exempt always, which served
 * the whole API to anyone in radio range of an unattended access point
 * (espOS #154). The window is what the key-is-lost path uses now, and this
 * file is where physical presence is established for it: a run of power
 * cycles, counted in NVS across the cycles themselves because RTC memory does
 * not survive one.
 */
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "sdkconfig.h"
#if !CONFIG_IDF_TARGET_LINUX
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include <netinet/in.h>
#endif
#include "cJSON.h"

#include "espos_cfg_keys.h"
#include "espos_config.h"
#include "espos_httpd.h"
#include "espos_httpd_auth_policy.h"
#include "espos_httpd_priv.h"

static const char *TAG = "espos_auth";

#ifndef CONFIG_ESPOS_HTTPD_MAX_SESSIONS
#define CONFIG_ESPOS_HTTPD_MAX_SESSIONS 4
#endif
#ifdef CONFIG_ESPOS_HTTPD_AUTH_REQUIRED
#define AUTH_REQUIRED true
#else
#define AUTH_REQUIRED false
#endif

#if CONFIG_ESPOS_HTTPD_PORTAL_RECOVERY && !CONFIG_IDF_TARGET_LINUX
#define RECOVERY 1
/* Beside skstate rather than in the config namespace: this is not a setting,
 * it is not exported with the configuration, and an operator has no business
 * editing it. */
#define RECOVERY_NS  "httpdauth"
#define RECOVERY_KEY "pwrcycles"
#else
#define RECOVERY 0
#endif

#define COOKIE_NAME "espos_sid"
/* "Bearer " + the longest key + room to notice a longer one */
#define AUTH_HDR_MAX (7 + ESPOS_HTTPD_AUTH_KEY_MAX + 8)
#define HOST_MAX     80
/* scheme + host + port; a Referer is cut here too — only its authority matters */
#define ORIGIN_MAX 112

static struct {
    SemaphoreHandle_t lock;
    espos_httpd_auth_policy_t policy;
    espos_httpd_auth_session_t sessions[CONFIG_ESPOS_HTTPD_MAX_SESSIONS];
} s;

/* ----------------------------------------------------------------- port */

static uint32_t port_now_s(void *ctx)
{
    (void)ctx;
#if CONFIG_IDF_TARGET_LINUX
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec;
#else
    return (uint32_t)(esp_timer_get_time() / 1000000);
#endif
}

/* esp_random(): the hardware RNG on a chip, getentropy() on the linux target. */
static uint32_t port_random(void *ctx)
{
    (void)ctx;
    return esp_random();
}

static const espos_httpd_auth_port_t PORT = {
    .now_s = port_now_s,
    .random = port_random,
};

static void lock(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s.lock);
}

static void load_key(void)
{
    char key[ESPOS_HTTPD_AUTH_KEY_MAX + 1] = { 0 };
    (void)espos_config_get_str(ESPOS_CFG_NS_HTTPD, ESPOS_CFG_HTTPD_API_KEY, key, sizeof(key), NULL);
    size_t n = strlen(key);
    lock();
    espos_httpd_auth_policy_set_key(&s.policy, key);
    unlock();
    if (n == 0) {
        ESP_LOGW(TAG, "no API key set: the REST API is open to the network (httpd.api_key)");
    } else if (n < ESPOS_HTTPD_AUTH_KEY_MIN) {
        ESP_LOGW(TAG, "API key is %u characters; %d or more is the minimum worth having", (unsigned)n, ESPOS_HTTPD_AUTH_KEY_MIN);
    } else {
        ESP_LOGI(TAG, "API key set: protected endpoints need Bearer or a login");
    }
}

static void load_ttl(void)
{
    int32_t ttl = 86400;
    (void)espos_config_get_i32(ESPOS_CFG_NS_HTTPD, ESPOS_CFG_HTTPD_SESSION_TTL_S, &ttl);
    lock();
    espos_httpd_auth_policy_set_ttl(&s.policy, (uint32_t)ttl);
    unlock();
}

/* ------------------------------------------------- power-cycle recovery */

#if RECOVERY
static esp_timer_handle_t s_clear_timer;
/* A non-zero count is in NVS and wants clearing once this boot has lasted. */
static bool s_cycles_stored;

static uint8_t cycles_read(void)
{
    nvs_handle_t h;
    if (nvs_open_from_partition(CONFIG_ESPOS_CONFIG_NVS_PARTITION, RECOVERY_NS, NVS_READONLY, &h) != ESP_OK) {
        return 0; /* no namespace yet: no cycles counted yet */
    }
    uint8_t n = 0;
    if (nvs_get_u8(h, RECOVERY_KEY, &n) != ESP_OK) {
        n = 0;
    }
    nvs_close(h);
    return n;
}

static void cycles_write(uint8_t n)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(CONFIG_ESPOS_CONFIG_NVS_PARTITION, RECOVERY_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "recovery counter not stored: %s", esp_err_to_name(err));
        return;
    }
    if (nvs_set_u8(h, RECOVERY_KEY, n) == ESP_OK) {
        (void)nvs_commit(h);
        s_cycles_stored = n != 0;
    }
    nvs_close(h);
}

/* The device has been up long enough that the last power cycle was not
 * somebody at the breaker, so the run ends here. */
static void clear_cycles_task(void *arg)
{
    (void)arg;
    cycles_write(0);
    vTaskDelete(NULL);
}

static void clear_cycles_cb(void *arg)
{
    (void)arg;
    /* Writes flash, so not on the esp_timer task: that stack is 3584 bytes by
     * default and is shared by every timer in the firmware, and a flash erase
     * would hold up all of them. A task that exists for a few milliseconds,
     * once per boot, on a device that was power-cycled into this. */
    if (s_cycles_stored) {
        (void)xTaskCreate(clear_cycles_task, "authrec_clr", 3072, NULL, 3, NULL);
    }
}

static void recovery_boot_check(void)
{
    /* Once per boot. espos_httpd_start() is a no-op while the server is up,
     * but an app may stop and start it again, and counting that as a power
     * cycle would let three restarts open a window. */
    static bool checked;
    if (checked) {
        return;
    }
    checked = true;

    /* Nothing to protect and nothing to recover: an open device would only be
     * paying flash writes for it. The count starts the first time a device
     * with a key is power-cycled. */
    lock();
    bool configured = espos_httpd_auth_policy_configured(&s.policy);
    unlock();
    if (!configured) {
        return;
    }

    /* Only a power cycle counts. A watchdog reboot, a panic or a software
     * restart is not a person at the device, and a device stuck in a reboot
     * loop must not count its way into an open window. A brown-out is excluded
     * for the same reason, and it is the one worth knowing about: a supply that
     * sags instead of dropping cleanly is why somebody at the breaker can cycle
     * the power and see the count never move. Those boots leave the stored
     * count alone and let the timer below clear it. */
    const esp_reset_reason_t reason = esp_reset_reason();
    uint8_t n = cycles_read();
    s_cycles_stored = n != 0;
    if (reason != ESP_RST_POWERON) {
        ESP_LOGD(TAG, "reset reason %d is not a power cycle: portal recovery count unchanged at %u", (int)reason,
                 (unsigned)n);
    }
    if (reason == ESP_RST_POWERON) {
        if (n < 255) {
            n++;
        }
        if (n >= CONFIG_ESPOS_HTTPD_RECOVERY_CYCLES) {
            /* Zeroed before the window opens, not after it closes: the window
             * lives in RAM, so a device that loses power mid-window would
             * otherwise open another one on its next boot, and another. */
            cycles_write(0);
            lock();
            espos_httpd_auth_policy_recovery_open(&s.policy, CONFIG_ESPOS_HTTPD_RECOVERY_WINDOW_S);
            unlock();
            ESP_LOGW(TAG, "%d power cycles: the setup access point is exempt from the API key for %d s — set a new one",
                     CONFIG_ESPOS_HTTPD_RECOVERY_CYCLES, CONFIG_ESPOS_HTTPD_RECOVERY_WINDOW_S);
            return;
        }
        cycles_write(n);
        ESP_LOGI(TAG, "power cycle %u of %d toward portal recovery", (unsigned)n, CONFIG_ESPOS_HTTPD_RECOVERY_CYCLES);
    }

    if (!s_cycles_stored) {
        return; /* nothing to clear: no timer, no task */
    }
    if (!s_clear_timer) {
        const esp_timer_create_args_t args = { .callback = clear_cycles_cb, .name = "authrec" };
        if (esp_timer_create(&args, &s_clear_timer) != ESP_OK) {
            s_clear_timer = NULL;
            return;
        }
    }
    (void)esp_timer_start_once(s_clear_timer, (uint64_t)CONFIG_ESPOS_HTTPD_RECOVERY_CLEAR_S * 1000000ULL);
}
#endif /* RECOVERY */

esp_err_t espos_httpd_auth_recovery_open(uint32_t seconds)
{
    if (!s.lock) {
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    espos_httpd_auth_policy_recovery_open(&s.policy, seconds);
    /* Read back rather than echo the argument: the policy clamps to
     * ESPOS_HTTPD_AUTH_RECOVERY_MAX_S, and a log line that names a window
     * longer than the one that is actually open is the kind of thing somebody
     * later trusts. */
    uint32_t opened = espos_httpd_auth_policy_recovery_s_left(&s.policy);
    unlock();
    if (opened) {
        ESP_LOGW(TAG, "setup access point exempt from the API key for %u s", (unsigned)opened);
    } else {
        ESP_LOGI(TAG, "setup access point recovery window closed");
    }
    return ESP_OK;
}

uint32_t espos_httpd_auth_recovery_s_left(void)
{
    if (!s.lock) {
        return 0;
    }
    lock();
    uint32_t left = espos_httpd_auth_policy_recovery_s_left(&s.policy);
    unlock();
    return left;
}

esp_err_t espos_httpd_auth_init(void)
{
    if (!s.lock) {
        s.lock = xSemaphoreCreateMutex();
        if (!s.lock) {
            return ESP_ERR_NO_MEM;
        }
        espos_httpd_auth_policy_init(&s.policy, &PORT, NULL, s.sessions, CONFIG_ESPOS_HTTPD_MAX_SESSIONS, 86400,
                                     AUTH_REQUIRED);
    }
    load_key();
    load_ttl();
#if RECOVERY
    /* After load_key(), which is what decides whether counting applies. */
    recovery_boot_check();
#endif
    return ESP_OK;
}

void espos_httpd_auth_config_changed(const char *ns, const char *key)
{
    if (!s.lock || strcmp(ns, ESPOS_CFG_NS_HTTPD) != 0) {
        return;
    }
    if (strcmp(key, ESPOS_CFG_HTTPD_API_KEY) == 0) {
        load_key();
    } else if (strcmp(key, ESPOS_CFG_HTTPD_SESSION_TTL_S) == 0) {
        load_ttl();
    }
}

bool espos_httpd_auth_required(void)
{
    if (!s.lock) {
        return false;
    }
    lock();
    bool r = espos_httpd_auth_policy_required(&s.policy);
    unlock();
    return r;
}

/* -------------------------------------------------------------- request */

/* The request came in on the soft-AP: its local socket address is the AP
 * netif's, and that netif is up. Looked up by interface key so espos_httpd
 * does not depend on espos_wifi; a firmware without a portal has no such
 * netif and the exemption never applies. Off on the host: the harness must
 * see the real refusals. */
static bool from_portal(httpd_req_t *req)
{
#if CONFIG_IDF_TARGET_LINUX
    (void)req;
    return false;
#else
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap || !esp_netif_is_netif_up(ap)) {
        return false;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(ap, &info) != ESP_OK || info.ip.addr == 0) {
        return false;
    }
    int fd = httpd_req_to_sockfd(req);
    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    if (fd < 0 || getsockname(fd, (struct sockaddr *)&ss, &len) != 0) {
        return false;
    }
    uint32_t local = 0;
    if (ss.ss_family == AF_INET) {
        local = ((struct sockaddr_in *)&ss)->sin_addr.s_addr;
    }
#if CONFIG_LWIP_IPV6
    else if (ss.ss_family == AF_INET6) {
        /* The listening socket is dual-stack; a v4 peer shows as ::ffff:a.b.c.d. */
        const uint8_t *a = ((struct sockaddr_in6 *)&ss)->sin6_addr.s6_addr;
        static const uint8_t mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
        if (memcmp(a, mapped, sizeof(mapped)) == 0) {
            memcpy(&local, a + 12, 4);
        }
    }
#endif
    return local != 0 && local == info.ip.addr;
#endif
}

static bool state_changing(int method)
{
    return method != HTTP_GET && method != HTTP_HEAD && method != HTTP_OPTIONS;
}

typedef struct {
    char auth[AUTH_HDR_MAX];
    char sid[ESPOS_HTTPD_AUTH_SID_LEN + 1];
    char host[HOST_MAX];
    char origin[ORIGIN_MAX];
    espos_httpd_auth_request_t rq;
} gathered_t;

/* Read what the decision needs out of the request. Header values longer
 * than the buffers are kept truncated: a truncated key is simply wrong, a
 * truncated Origin still starts with its authority. */
static void gather(httpd_req_t *req, gathered_t *g)
{
    memset(g, 0, sizeof(*g));
    g->rq.state_changing = state_changing(req->method);
    g->rq.from_portal = from_portal(req);
    esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", g->auth, sizeof(g->auth));
    if ((err == ESP_OK || err == ESP_ERR_HTTPD_RESULT_TRUNC) && strncasecmp(g->auth, "Bearer ", 7) == 0) {
        const char *tok = g->auth + 7;
        while (*tok == ' ') {
            tok++;
        }
        g->rq.bearer = tok;
    }
    size_t n = sizeof(g->sid);
    if (httpd_req_get_cookie_val(req, COOKIE_NAME, g->sid, &n) == ESP_OK) {
        g->rq.cookie_sid = g->sid;
    }
    if (g->rq.state_changing) {
        if (httpd_req_get_hdr_value_str(req, "Host", g->host, sizeof(g->host)) == ESP_OK) {
            g->rq.host = g->host;
        }
        err = httpd_req_get_hdr_value_str(req, "Origin", g->origin, sizeof(g->origin));
        if (err != ESP_OK && err != ESP_ERR_HTTPD_RESULT_TRUNC) {
            err = httpd_req_get_hdr_value_str(req, "Referer", g->origin, sizeof(g->origin));
        }
        if (err == ESP_OK || err == ESP_ERR_HTTPD_RESULT_TRUNC) {
            g->rq.origin = g->origin;
        }
    }
}

static espos_httpd_auth_verdict_t decide(httpd_req_t *req, espos_httpd_auth_method_t *method)
{
    gathered_t g;
    gather(req, &g);
    lock();
    espos_httpd_auth_verdict_t v = espos_httpd_auth_policy_decide(&s.policy, &g.rq, method);
    unlock();
    return v;
}

static esp_err_t send_verdict(httpd_req_t *req, espos_httpd_auth_verdict_t v)
{
    switch (v) {
    case ESPOS_HTTPD_AUTH_UNAUTHORIZED:
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer realm=\"espOS\"");
        return espos_httpd_send_error(req, "401 Unauthorized", "unauthorized",
                                      "authentication required: Authorization: Bearer <key>, or log in at /api/v1/auth/login");
    case ESPOS_HTTPD_AUTH_FORBIDDEN_ORIGIN:
        return espos_httpd_send_error(req, "403 Forbidden", "forbidden",
                                      "cross-site request: Origin does not match Host");
    case ESPOS_HTTPD_AUTH_UNCONFIGURED:
        return espos_httpd_send_error(req, "403 Forbidden", "auth_unconfigured",
                                      "this build requires an API key and none is set; set httpd.api_key from the setup portal");
    case ESPOS_HTTPD_AUTH_THROTTLED: {
        char after[12];
        lock();
        uint32_t secs = espos_httpd_auth_policy_retry_after_s(&s.policy);
        unlock();
        snprintf(after, sizeof(after), "%u", (unsigned)(secs ? secs : 1));
        httpd_resp_set_hdr(req, "Retry-After", after);
        return espos_httpd_send_error(req, "429 Too Many Requests", "too_many_attempts",
                                      "too many failed keys; wait before trying again");
    }
    case ESPOS_HTTPD_AUTH_ALLOW:
    default:
        return ESP_OK;
    }
}

bool espos_httpd_auth_enforce(httpd_req_t *req)
{
    if (!s.lock) {
        return true; /* not started: nothing to enforce with */
    }
    espos_httpd_auth_verdict_t v = decide(req, NULL);
    if (v == ESPOS_HTTPD_AUTH_ALLOW) {
        return true;
    }
    if (v == ESPOS_HTTPD_AUTH_UNAUTHORIZED) {
        ESP_LOGD(TAG, "%s refused: no valid credential", req->uri);
    } else {
        ESP_LOGW(TAG, "%s refused: %s", req->uri,
                 v == ESPOS_HTTPD_AUTH_THROTTLED          ? "too many failed keys"
                 : v == ESPOS_HTTPD_AUTH_FORBIDDEN_ORIGIN ? "cross-site request"
                                                          : "no API key configured");
    }
    send_verdict(req, v);
    return false;
}

bool espos_httpd_request_authenticated(httpd_req_t *req)
{
    if (!req || !s.lock) {
        return false;
    }
    return decide(req, NULL) == ESPOS_HTTPD_AUTH_ALLOW;
}

/* ------------------------------------------------------------- handlers */

static void set_cookie(httpd_req_t *req, const char *id, uint32_t max_age, char *buf, size_t size)
{
    /* HttpOnly: no script reads it. SameSite=Strict: no other site sends it,
     * not even on a top-level navigation. Path=/: the UI and the API share it. */
    snprintf(buf, size, COOKIE_NAME "=%s; HttpOnly; SameSite=Strict; Path=/; Max-Age=%u", id, (unsigned)max_age);
    httpd_resp_set_hdr(req, "Set-Cookie", buf);
}

static esp_err_t send_no_content(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, NULL, 0);
}

/* POST /api/v1/auth/login {"key": "..."} → 204 + Set-Cookie */
static esp_err_t login_post(httpd_req_t *req)
{
    if (!espos_httpd_require_json(req)) {
        return ESP_OK;
    }
    lock();
    bool configured = espos_httpd_auth_policy_configured(&s.policy);
    bool required = espos_httpd_auth_policy_required(&s.policy);
    unlock();
    if (!configured) {
        if (required) {
            return send_verdict(req, ESPOS_HTTPD_AUTH_UNCONFIGURED);
        }
        return espos_httpd_send_error(req, "409 Conflict", "auth_open", "no API key is configured; the API is open");
    }
    char *body = NULL;
    size_t len = 0;
    if (espos_httpd_read_body(req, &body, &len) != ESP_OK) {
        return ESP_FAIL;
    }
    cJSON *j = cJSON_ParseWithLength(body, len);
    free(body);
    const cJSON *key = j ? cJSON_GetObjectItem(j, "key") : NULL;
    if (!cJSON_IsString(key)) {
        cJSON_Delete(j);
        return espos_httpd_send_error(req, "400 Bad Request", "validation", "expected {\"key\": \"...\"}");
    }
    char sid[ESPOS_HTTPD_AUTH_SID_LEN + 1];
    uint32_t ttl;
    lock();
    espos_httpd_auth_verdict_t v = espos_httpd_auth_policy_check_key(&s.policy, key->valuestring);
    bool opened = v == ESPOS_HTTPD_AUTH_ALLOW && espos_httpd_auth_policy_session_open(&s.policy, sid, sizeof(sid));
    ttl = s.policy.ttl_s;
    unlock();
    cJSON_Delete(j);
    if (v != ESPOS_HTTPD_AUTH_ALLOW) {
        ESP_LOGW(TAG, "login refused: %s", v == ESPOS_HTTPD_AUTH_THROTTLED ? "too many failed keys" : "wrong key");
        return send_verdict(req, v);
    }
    if (!opened) {
        return espos_httpd_send_error(req, "500 Internal Server Error", "no_session", "session table unavailable");
    }
    ESP_LOGI(TAG, "login: session opened (%u s)", (unsigned)ttl);
    char cookie[ESPOS_HTTPD_AUTH_SID_LEN + 80];
    set_cookie(req, sid, ttl, cookie, sizeof(cookie));
    return send_no_content(req);
}

/* POST /api/v1/auth/logout → 204, the session is gone and the cookie cleared */
static esp_err_t logout_post(httpd_req_t *req)
{
    if (!espos_httpd_require_json(req)) {
        return ESP_OK;
    }
    char sid[ESPOS_HTTPD_AUTH_SID_LEN + 1];
    size_t n = sizeof(sid);
    if (httpd_req_get_cookie_val(req, COOKIE_NAME, sid, &n) == ESP_OK) {
        lock();
        espos_httpd_auth_policy_session_close(&s.policy, sid);
        unlock();
        ESP_LOGI(TAG, "logout: session closed");
    }
    char cookie[80];
    set_cookie(req, "", 0, cookie, sizeof(cookie));
    return send_no_content(req);
}

/* GET /api/v1/auth/status → {"required","configured","authenticated","method","recovery_s"} */
static esp_err_t status_get(httpd_req_t *req)
{
    espos_httpd_auth_method_t m = ESPOS_HTTPD_AUTH_NONE;
    (void)decide(req, &m);
    lock();
    bool required = espos_httpd_auth_policy_required(&s.policy);
    bool configured = espos_httpd_auth_policy_configured(&s.policy);
    /* Readable without a credential, like the rest of this endpoint: it says
     * that the portal is currently exempt, which anyone on the portal can
     * establish by making one request anyway, and an operator who has just
     * power-cycled their way in needs to see that it worked. */
    uint32_t recovery_s = espos_httpd_auth_policy_recovery_s_left(&s.policy);
    unlock();
    char body[160];
    snprintf(body, sizeof(body),
             "{\"required\":%s,\"configured\":%s,\"authenticated\":%s,\"method\":\"%s\",\"recovery_s\":%u}",
             required ? "true" : "false", configured ? "true" : "false",
             m != ESPOS_HTTPD_AUTH_NONE ? "true" : "false", espos_httpd_auth_method_str(m), (unsigned)recovery_s);
    return espos_httpd_send_json(req, NULL, body);
}

esp_err_t espos_httpd_register_auth_api(void)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/v1/auth/login", .method = HTTP_POST, .handler = login_post },
        { .uri = "/api/v1/auth/logout", .method = HTTP_POST, .handler = logout_post },
        { .uri = "/api/v1/auth/status", .method = HTTP_GET, .handler = status_get },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = espos_httpd_register_ex(&uris[i], ESPOS_HTTPD_PUBLIC);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
