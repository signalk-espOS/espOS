/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * The authentication policy (espos_httpd_auth_policy.h): sessions, the
 * failure throttle and the decision. No platform calls — auth.c is the port.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "espos_httpd_auth_policy.h"

/* Compare two strings without an early exit: the loop runs over `width`
 * bytes whatever the inputs, reading a zero past either string's end, so
 * the time taken depends on neither content nor length. The lengths are
 * folded in as well, or "abc" would match "abc\0..." only by luck. */
static bool ct_equal(const char *a, size_t alen, const char *b, size_t blen, size_t width)
{
    unsigned diff = (unsigned)(alen ^ blen);
    for (size_t i = 0; i < width; i++) {
        unsigned ca = i < alen ? (unsigned char)a[i] : 0;
        unsigned cb = i < blen ? (unsigned char)b[i] : 0;
        diff |= ca ^ cb;
    }
    return diff == 0;
}

static uint32_t now_s(const espos_httpd_auth_policy_t *p)
{
    return p->port->now_s(p->ctx);
}

/* a is at or after b on a wrapping clock */
static bool reached(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) >= 0;
}

void espos_httpd_auth_policy_init(espos_httpd_auth_policy_t *p, const espos_httpd_auth_port_t *port, void *ctx,
                                  espos_httpd_auth_session_t *sessions, size_t session_count, uint32_t ttl_s,
                                  bool require_key)
{
    memset(p, 0, sizeof(*p));
    p->port = port;
    p->ctx = ctx;
    p->sessions = sessions;
    p->session_count = sessions ? session_count : 0;
    p->ttl_s = ttl_s;
    p->require_key = require_key;
    if (p->sessions) {
        memset(p->sessions, 0, sizeof(*p->sessions) * p->session_count);
    }
}

void espos_httpd_auth_policy_set_key(espos_httpd_auth_policy_t *p, const char *key)
{
    if (!key) {
        key = "";
    }
    snprintf(p->key, sizeof(p->key), "%s", key);
    /* A new key means new logins; the old sessions were minted under the
     * old one and nobody holding a stale cookie should ride it across. The
     * throttle is left alone: changing the key is not a way out of a lockout. */
    espos_httpd_auth_policy_sessions_clear(p);
}

void espos_httpd_auth_policy_set_ttl(espos_httpd_auth_policy_t *p, uint32_t ttl_s)
{
    p->ttl_s = ttl_s;
}

bool espos_httpd_auth_policy_configured(const espos_httpd_auth_policy_t *p)
{
    return p->key[0] != '\0';
}

bool espos_httpd_auth_policy_required(const espos_httpd_auth_policy_t *p)
{
    return p->require_key || espos_httpd_auth_policy_configured(p);
}

/* ------------------------------------------------------------- throttle */

bool espos_httpd_auth_policy_throttled(const espos_httpd_auth_policy_t *p)
{
    return p->locked && !reached(now_s(p), p->lockout_until_s);
}

uint32_t espos_httpd_auth_policy_retry_after_s(const espos_httpd_auth_policy_t *p)
{
    if (!espos_httpd_auth_policy_throttled(p)) {
        return 0;
    }
    return p->lockout_until_s - now_s(p);
}

espos_httpd_auth_verdict_t espos_httpd_auth_policy_check_key(espos_httpd_auth_policy_t *p, const char *presented)
{
    uint32_t now = now_s(p);
    if (p->locked) {
        if (!reached(now, p->lockout_until_s)) {
            return ESPOS_HTTPD_AUTH_THROTTLED;
        }
        p->locked = false;
    }
    if (!espos_httpd_auth_policy_configured(p)) {
        return ESPOS_HTTPD_AUTH_UNAUTHORIZED; /* nothing to guess at; not a strike */
    }
    if (!presented) {
        presented = "";
    }
    size_t plen = strnlen(presented, ESPOS_HTTPD_AUTH_KEY_MAX + 1);
    if (ct_equal(p->key, strlen(p->key), presented, plen, ESPOS_HTTPD_AUTH_KEY_MAX + 1)) {
        p->fail_count = 0;
        return ESPOS_HTTPD_AUTH_ALLOW;
    }
    if (p->fail_count == 0 || now - p->fail_first_s >= ESPOS_HTTPD_AUTH_FAIL_WINDOW_S) {
        p->fail_count = 0;
        p->fail_first_s = now;
    }
    if (++p->fail_count >= ESPOS_HTTPD_AUTH_FAIL_MAX) {
        p->locked = true;
        p->lockout_until_s = now + ESPOS_HTTPD_AUTH_LOCKOUT_S;
        p->fail_count = 0;
    }
    return ESPOS_HTTPD_AUTH_UNAUTHORIZED;
}

/* ------------------------------------------------------------- recovery */

void espos_httpd_auth_policy_recovery_open(espos_httpd_auth_policy_t *p, uint32_t seconds)
{
    if (seconds == 0) {
        p->recovery = false;
        p->recovery_until_s = 0;
        return;
    }
    if (seconds > ESPOS_HTTPD_AUTH_RECOVERY_MAX_S) {
        seconds = ESPOS_HTTPD_AUTH_RECOVERY_MAX_S;
    }
    p->recovery = true;
    p->recovery_until_s = now_s(p) + seconds;
}

static bool recovery_open(const espos_httpd_auth_policy_t *p)
{
    return p->recovery && !reached(now_s(p), p->recovery_until_s);
}

uint32_t espos_httpd_auth_policy_recovery_s_left(const espos_httpd_auth_policy_t *p)
{
    if (!recovery_open(p)) {
        return 0;
    }
    return p->recovery_until_s - now_s(p);
}

/* ------------------------------------------------------------- sessions */

/* Free every session past its expiry; done on each table access rather than
 * on a timer the machine does not have. */
static void sweep(espos_httpd_auth_policy_t *p, uint32_t now)
{
    for (size_t i = 0; i < p->session_count; i++) {
        espos_httpd_auth_session_t *s = &p->sessions[i];
        if (s->used && reached(now, s->expires_s)) {
            memset(s, 0, sizeof(*s));
        }
    }
}

bool espos_httpd_auth_policy_session_open(espos_httpd_auth_policy_t *p, char *id_out, size_t id_size)
{
    if (p->session_count == 0 || !id_out || id_size < ESPOS_HTTPD_AUTH_SID_LEN + 1) {
        return false;
    }
    uint32_t now = now_s(p);
    sweep(p, now);
    espos_httpd_auth_session_t *slot = NULL;
    for (size_t i = 0; i < p->session_count; i++) {
        espos_httpd_auth_session_t *s = &p->sessions[i];
        if (!s->used) {
            slot = s;
            break;
        }
        /* Full: the one idle longest goes. Its browser logs in again. */
        if (!slot || (int32_t)(s->last_seen_s - slot->last_seen_s) < 0) {
            slot = s;
        }
    }
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < ESPOS_HTTPD_AUTH_SID_LEN; i += 8) {
        uint32_t r = p->port->random(p->ctx);
        for (size_t j = 0; j < 8; j++) {
            slot->id[i + j] = hex[(r >> (4 * j)) & 0xF];
        }
    }
    slot->id[ESPOS_HTTPD_AUTH_SID_LEN] = '\0';
    slot->expires_s = now + p->ttl_s;
    slot->last_seen_s = now;
    slot->used = true;
    memcpy(id_out, slot->id, ESPOS_HTTPD_AUTH_SID_LEN + 1);
    return true;
}

static espos_httpd_auth_session_t *find(espos_httpd_auth_policy_t *p, const char *id, uint32_t now)
{
    if (!id) {
        return NULL;
    }
    sweep(p, now);
    size_t len = strnlen(id, ESPOS_HTTPD_AUTH_SID_LEN + 1);
    espos_httpd_auth_session_t *hit = NULL;
    /* Every slot is compared, hit or not, so the position of a match does
     * not show in the timing either. */
    for (size_t i = 0; i < p->session_count; i++) {
        espos_httpd_auth_session_t *s = &p->sessions[i];
        bool eq = ct_equal(s->id, ESPOS_HTTPD_AUTH_SID_LEN, id, len, ESPOS_HTTPD_AUTH_SID_LEN + 1);
        if (s->used && eq) {
            hit = s;
        }
    }
    return hit;
}

bool espos_httpd_auth_policy_session_valid(espos_httpd_auth_policy_t *p, const char *id)
{
    uint32_t now = now_s(p);
    espos_httpd_auth_session_t *s = find(p, id, now);
    if (!s) {
        return false;
    }
    s->last_seen_s = now;
    return true;
}

void espos_httpd_auth_policy_session_close(espos_httpd_auth_policy_t *p, const char *id)
{
    espos_httpd_auth_session_t *s = find(p, id, now_s(p));
    if (s) {
        memset(s, 0, sizeof(*s));
    }
}

void espos_httpd_auth_policy_sessions_clear(espos_httpd_auth_policy_t *p)
{
    if (p->sessions) {
        memset(p->sessions, 0, sizeof(*p->sessions) * p->session_count);
    }
}

size_t espos_httpd_auth_policy_sessions_live(espos_httpd_auth_policy_t *p)
{
    sweep(p, now_s(p));
    size_t n = 0;
    for (size_t i = 0; i < p->session_count; i++) {
        n += p->sessions[i].used;
    }
    return n;
}

/* ------------------------------------------------------------- decision */

/* Does a request on the soft-AP network skip authentication?
 *
 * Only in the two cases that were ever the point of it, because "arrived on
 * the access point" is not evidence of anybody standing at the device: the
 * access point is open unless wifi.portal_psk was set, and espos_wifi brings
 * it up by itself -- permanently with no station network configured, and
 * wifi.portal_after_s after a station link drops. An Ethernet-only board
 * therefore served its whole API to radio range, with a key set (espOS #154).
 */
static bool portal_exempt(const espos_httpd_auth_policy_t *p)
{
    /* Nothing to authenticate against yet. This is how the first key is set,
     * and how a device set up again after a factory reset is reached --
     * including on a build with ESPOS_HTTPD_AUTH_REQUIRED, which answers 403
     * everywhere else until a key exists. */
    if (!espos_httpd_auth_policy_configured(p)) {
        return true;
    }
    /* A key is set, so this device has an operator who chose one. Only proof
     * of physical presence gets past it now. */
    return recovery_open(p);
}

espos_httpd_auth_verdict_t espos_httpd_auth_policy_decide(espos_httpd_auth_policy_t *p,
                                                          const espos_httpd_auth_request_t *rq,
                                                          espos_httpd_auth_method_t *method)
{
    espos_httpd_auth_method_t m = ESPOS_HTTPD_AUTH_NONE;
    espos_httpd_auth_verdict_t v;
    if (rq->from_portal && portal_exempt(p)) {
        m = ESPOS_HTTPD_AUTH_PORTAL;
        v = ESPOS_HTTPD_AUTH_ALLOW;
    } else if (!espos_httpd_auth_policy_configured(p)) {
        /* Open device. A stray Bearer header is not judged: the designer
         * configured with a key must keep working against a device that
         * has none yet. */
        v = p->require_key ? ESPOS_HTTPD_AUTH_UNCONFIGURED : ESPOS_HTTPD_AUTH_ALLOW;
    } else if (rq->bearer) {
        v = espos_httpd_auth_policy_check_key(p, rq->bearer);
        if (v == ESPOS_HTTPD_AUTH_ALLOW) {
            m = ESPOS_HTTPD_AUTH_BEARER;
        }
    } else if (rq->cookie_sid && espos_httpd_auth_policy_session_valid(p, rq->cookie_sid)) {
        m = ESPOS_HTTPD_AUTH_COOKIE;
        /* The cookie rides along with anything the browser sends, a hostile
         * page's form post included. The JSON content-type guard already
         * stops fetch() from other origins; this closes what is left. */
        v = rq->state_changing && !espos_httpd_auth_origin_matches(rq->origin, rq->host)
                ? ESPOS_HTTPD_AUTH_FORBIDDEN_ORIGIN
                : ESPOS_HTTPD_AUTH_ALLOW;
    } else {
        v = ESPOS_HTTPD_AUTH_UNAUTHORIZED;
    }
    if (method) {
        *method = m;
    }
    return v;
}

/* [start, start+len) of an authority, with a default ":80" dropped. */
static void authority(const char *s, const char **start, size_t *len)
{
    const char *p = strstr(s, "://");
    p = p ? p + 3 : s;
    size_t n = strcspn(p, "/?#");
    if (n >= 3 && strncmp(p + n - 3, ":80", 3) == 0) {
        n -= 3;
    }
    *start = p;
    *len = n;
}

bool espos_httpd_auth_origin_matches(const char *origin_or_referer, const char *host)
{
    if (!origin_or_referer || !host || !*origin_or_referer || !*host) {
        return false;
    }
    const char *a, *b;
    size_t alen, blen;
    authority(origin_or_referer, &a, &alen);
    authority(host, &b, &blen);
    if (alen == 0 || alen != blen) {
        return false;
    }
    for (size_t i = 0; i < alen; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

const char *espos_httpd_auth_method_str(espos_httpd_auth_method_t m)
{
    switch (m) {
    case ESPOS_HTTPD_AUTH_BEARER: return "bearer";
    case ESPOS_HTTPD_AUTH_COOKIE: return "cookie";
    case ESPOS_HTTPD_AUTH_PORTAL: return "portal";
    case ESPOS_HTTPD_AUTH_NONE:
    default: return "none";
    }
}
