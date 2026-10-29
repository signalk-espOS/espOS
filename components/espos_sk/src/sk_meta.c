/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * What one meta reconciliation leg means for the pass, and the table that
 * tracks which paths are done. Pure, so the host tests can drive it.
 *
 * A leg that gets no HTTP answer at all has already spent its whole timeout,
 * and every further path would spend another: on a server that accepts the
 * stream but is too busy to answer REST -- the usual state of a signalk-server
 * still starting its plugins -- a round over every declared path holds the
 * client task, and the token machine with it, for minutes. So no answer or a
 * server error (5xx, about the server and not the path) ends the round, a
 * refused token ends it too (every following leg carries the same token), and
 * any other answer leaves that path for the retry while the round carries on.
 * A reply whose meta cannot be read is the server's meta all the same:
 * PUTting ours over it would replace what the user configured.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "espos_sk_priv.h"

espos_sk_meta_step_t espos_sk_meta_after_get(int status, espos_sk_meta_seen_t seen)
{
    if (status <= 0 || status >= 500) {
        return ESPOS_SK_META_BACK_OFF;
    }
    if (status == 401 || status == 403) {
        return ESPOS_SK_META_UNAUTHORIZED;
    }
    if (status == 404 || (status == 200 && (seen == ESPOS_SK_META_NONE || seen == ESPOS_SK_META_OURS))) {
        return ESPOS_SK_META_PUT;
    }
    if (status == 200) {
        return ESPOS_SK_META_DONE; /* present, or unreadable: the server's either way */
    }
    return ESPOS_SK_META_LATER;
}

bool espos_sk_meta_holds(const char *server_json, const char *ours_json)
{
    /* Field by field, not the text: the server answers with its own
     * formatting and may add fields of its own. */
    cJSON *srv = cJSON_Parse(server_json);
    cJSON *ours = cJSON_Parse(ours_json);
    bool holds = cJSON_IsObject(srv) && cJSON_IsObject(ours);
    for (const cJSON *f = holds ? ours->child : NULL; f && holds; f = f->next) {
        holds = cJSON_Compare(f, cJSON_GetObjectItemCaseSensitive(srv, f->string), true);
    }
    cJSON_Delete(srv);
    cJSON_Delete(ours);
    return holds;
}

espos_sk_meta_step_t espos_sk_meta_after_put(int status)
{
    if (status >= 200 && status < 300) {
        return ESPOS_SK_META_DONE;
    }
    if (status <= 0 || status >= 500) {
        return ESPOS_SK_META_BACK_OFF;
    }
    if (status == 401 || status == 403) {
        return ESPOS_SK_META_UNAUTHORIZED;
    }
    return ESPOS_SK_META_LATER;
}

esp_err_t espos_sk_meta_declare(espos_sk_meta_table_t *t, const char *path, char *meta_json, uint32_t period_ms)
{
    espos_sk_meta_entry_t *e = NULL;
    for (size_t i = 0; i < t->n; i++) {
        if (strcmp(t->e[i].path, path) == 0) {
            e = &t->e[i];
            break;
        }
    }
    if (e && e->meta_json && strcmp(e->meta_json, meta_json) == 0 && e->period_ms == period_ms) {
        /* Unchanged: nothing for the server to hear, and no reason to cut a
         * running backoff short. */
        free(meta_json);
        return ESP_OK;
    }
    if (!e) {
        if (t->n >= ESPOS_SK_META_CAP) {
            free(meta_json);
            return ESP_ERR_NO_MEM;
        }
        e = &t->e[t->n++];
        snprintf(e->path, sizeof(e->path), "%s", path);
    }
    free(e->meta_json);
    e->meta_json = meta_json;
    e->period_ms = period_ms;
    e->gen++;
    e->reconciled = false;
    t->epoch++;
    t->dirty = true;
    t->round = 0; /* a new declaration does not wait out an old backoff */
    return ESP_OK;
}

void espos_sk_meta_connected(espos_sk_meta_table_t *t)
{
    /* The server may be another one, or have lost what it was sent. */
    for (size_t i = 0; i < t->n; i++) {
        t->e[i].reconciled = false;
        free(t->e[i].stale);
        t->e[i].stale = NULL;
        t->e[i].gen++;
    }
    t->conn++;
    t->epoch++;
    t->dirty = t->n > 0;
    t->round = 0;
}

uint32_t espos_sk_meta_wait(const espos_sk_meta_table_t *t, bool connected, uint32_t now_ms)
{
    if (!connected || !t->dirty) {
        return UINT32_MAX;
    }
    int32_t left = (int32_t)(t->retry_at_ms - now_ms);
    return t->round == 0 || left <= 0 ? 0 : (uint32_t)left;
}

size_t espos_sk_meta_index(const espos_sk_meta_table_t *t, size_t n, size_t k)
{
    /* From where the last round stopped, so a path the server keeps failing
     * with a 5xx cannot hold back the ones declared after it. */
    size_t start = t->next < n ? t->next : 0;
    return (start + k) % n;
}

void espos_sk_meta_leg_done(espos_sk_meta_table_t *t, size_t i, uint32_t gen, espos_sk_meta_step_t step)
{
    if (step == ESPOS_SK_META_DONE) {
        /* Unless it was redeclared, or the stream reconnected, while the legs
         * ran: the server was asked about something that no longer holds. */
        if (t->e[i].gen == gen) {
            t->e[i].reconciled = true;
            free(t->e[i].stale);
            t->e[i].stale = NULL;
        }
    } else if (step == ESPOS_SK_META_UNAUTHORIZED || step == ESPOS_SK_META_BACK_OFF) {
        t->next = i + 1;
    }
}

void espos_sk_meta_sent(espos_sk_meta_table_t *t, size_t i, uint32_t gen, uint32_t conn, char *sent)
{
    /* Redeclared while the PUT ran: the server now has the old text, which
     * the next round's GET would take for the server's own and keep. That
     * round overwrites it, unless the server's meta no longer holds it: an
     * edit made there meanwhile wins as any other does. After a reconnect the
     * server may be another one, and this one never had it. */
    free(t->e[i].stale);
    t->e[i].stale = NULL;
    if (t->e[i].gen != gen && t->conn == conn) {
        t->e[i].stale = sent;
    } else {
        free(sent);
    }
}

uint32_t espos_sk_meta_round_end(espos_sk_meta_table_t *t, uint32_t now_ms, uint32_t delay_ms)
{
    bool any = false;
    for (size_t i = 0; i < t->n; i++) {
        any |= !t->e[i].reconciled;
    }
    t->dirty = any;
    if (!any) {
        t->round = 0;
        return 0;
    }
    if (t->round < 30) {
        t->round++;
    }
    t->retry_at_ms = now_ms + delay_ms;
    return delay_ms;
}
