/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * One meta leg's outcome for the reconciliation round: a leg without an
 * answer or with a server error ends the round, and a reply the device cannot
 * read is never taken for "no meta", which would overwrite what the user set
 * on the server. Then the table the round works through: redeclarations,
 * answers that arrive for a text or a connection that no longer holds, the
 * backoff and where the next round starts.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "espos_sk_priv.h"

TEST_CASE("meta: no answer or a server error ends the round", "[sk_meta]")
{
    TEST_ASSERT_EQUAL(ESPOS_SK_META_BACK_OFF, espos_sk_meta_after_get(0, ESPOS_SK_META_NONE));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_BACK_OFF, espos_sk_meta_after_put(0));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_BACK_OFF, espos_sk_meta_after_get(503, ESPOS_SK_META_NONE));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_BACK_OFF, espos_sk_meta_after_put(500));
}

TEST_CASE("meta: the server's own meta is kept, none means PUT ours", "[sk_meta]")
{
    TEST_ASSERT_EQUAL(ESPOS_SK_META_DONE, espos_sk_meta_after_get(200, ESPOS_SK_META_PRESENT));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_PUT, espos_sk_meta_after_get(200, ESPOS_SK_META_NONE));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_PUT, espos_sk_meta_after_get(404, ESPOS_SK_META_NONE));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_DONE, espos_sk_meta_after_put(200));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_DONE, espos_sk_meta_after_put(202));
}

TEST_CASE("meta: an unreadable reply is the server's meta, never overwritten", "[sk_meta]")
{
    TEST_ASSERT_EQUAL(ESPOS_SK_META_DONE, espos_sk_meta_after_get(200, ESPOS_SK_META_UNREADABLE));
}

TEST_CASE("meta: a refused token ends the round", "[sk_meta]")
{
    TEST_ASSERT_EQUAL(ESPOS_SK_META_UNAUTHORIZED, espos_sk_meta_after_get(401, ESPOS_SK_META_NONE));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_UNAUTHORIZED, espos_sk_meta_after_get(403, ESPOS_SK_META_NONE));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_UNAUTHORIZED, espos_sk_meta_after_put(401));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_UNAUTHORIZED, espos_sk_meta_after_put(403));
}

TEST_CASE("meta: a 4xx about the path leaves it for the retry, the round goes on", "[sk_meta]")
{
    TEST_ASSERT_EQUAL(ESPOS_SK_META_LATER, espos_sk_meta_after_get(400, ESPOS_SK_META_NONE));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_LATER, espos_sk_meta_after_get(405, ESPOS_SK_META_NONE));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_LATER, espos_sk_meta_after_put(400));
    TEST_ASSERT_EQUAL(ESPOS_SK_META_LATER, espos_sk_meta_after_put(405));
}

/* The table below is what espos_sk_ws_meta_round() drives: one entry per
 * declared path, the legs' outcomes recorded under the stream's lock. */

static espos_sk_meta_table_t t;

static void table_reset(void)
{
    for (size_t i = 0; i < t.n; i++) {
        free(t.e[i].meta_json);
    }
    memset(&t, 0, sizeof(t));
}

static void declare(const char *path, const char *json)
{
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_meta_declare(&t, path, strdup(json), 0));
}

static size_t reconciled(void)
{
    size_t n = 0;
    for (size_t i = 0; i < t.n; i++) {
        n += t.e[i].reconciled;
    }
    return n;
}

TEST_CASE("meta table: a round is due at once on a connection, never without one", "[sk_meta]")
{
    table_reset();
    TEST_ASSERT_EQUAL(UINT32_MAX, espos_sk_meta_wait(&t, true, 1000));
    declare("a.b", "{\"units\":\"V\"}");
    TEST_ASSERT_EQUAL(UINT32_MAX, espos_sk_meta_wait(&t, false, 1000));
    TEST_ASSERT_EQUAL(0, espos_sk_meta_wait(&t, true, 1000));
    espos_sk_meta_leg_done(&t, 0, t.e[0].gen, ESPOS_SK_META_DONE);
    TEST_ASSERT_EQUAL(0, espos_sk_meta_round_end(&t, 1000, 1000));
    TEST_ASSERT_EQUAL(UINT32_MAX, espos_sk_meta_wait(&t, true, 1000));
    table_reset();
}

TEST_CASE("meta table: an unchanged declaration keeps the path and the backoff", "[sk_meta]")
{
    table_reset();
    declare("a.b", "{\"units\":\"V\"}");
    espos_sk_meta_leg_done(&t, 0, t.e[0].gen, ESPOS_SK_META_DONE);
    espos_sk_meta_round_end(&t, 0, 1000);
    uint32_t gen = t.e[0].gen;
    declare("a.b", "{\"units\":\"V\"}");
    TEST_ASSERT_EQUAL(1, t.n);
    TEST_ASSERT_EQUAL(gen, t.e[0].gen);
    TEST_ASSERT_EQUAL(1, reconciled());
    declare("a.b", "{\"units\":\"A\"}");
    TEST_ASSERT_EQUAL(0, reconciled());
    TEST_ASSERT_EQUAL(0, espos_sk_meta_wait(&t, true, 0));
    table_reset();
}

TEST_CASE("meta table: a changed declaration or a reconnect voids the round's backoff", "[sk_meta]")
{
    table_reset();
    declare("a.b", "{\"units\":\"V\"}");
    uint32_t epoch = t.epoch; /* a round starts */
    declare("a.b", "{\"units\":\"V\"}");
    TEST_ASSERT_EQUAL(epoch, t.epoch);
    declare("a.b", "{\"units\":\"A\"}");
    TEST_ASSERT_NOT_EQUAL(epoch, t.epoch);
    epoch = t.epoch;
    espos_sk_meta_connected(&t);
    TEST_ASSERT_NOT_EQUAL(epoch, t.epoch);
    table_reset();
}

TEST_CASE("meta table: an answer for a text redeclared meanwhile is not taken", "[sk_meta]")
{
    table_reset();
    declare("a.b", "{\"units\":\"V\"}");
    uint32_t gen = t.e[0].gen; /* the leg starts */
    declare("a.b", "{\"units\":\"A\"}");
    espos_sk_meta_leg_done(&t, 0, gen, ESPOS_SK_META_DONE);
    TEST_ASSERT_EQUAL(0, reconciled());
    TEST_ASSERT_EQUAL(1000, espos_sk_meta_round_end(&t, 0, 1000));
    table_reset();
}

TEST_CASE("meta table: a text redeclared during its PUT is sent again without asking", "[sk_meta]")
{
    table_reset();
    declare("a.b", "{\"units\":\"V\"}");
    uint32_t gen = t.e[0].gen, conn = t.conn; /* the PUT starts */
    espos_sk_meta_sent(&t, 0, gen, conn);
    TEST_ASSERT_FALSE(t.e[0].resend);
    declare("a.b", "{\"units\":\"A\"}");
    espos_sk_meta_sent(&t, 0, gen, conn);
    TEST_ASSERT_TRUE(t.e[0].resend);
    espos_sk_meta_sent(&t, 0, t.e[0].gen, conn);
    TEST_ASSERT_FALSE(t.e[0].resend);
    /* After a reconnect the server may be another one: ask it first. */
    gen = t.e[0].gen;
    espos_sk_meta_connected(&t);
    espos_sk_meta_sent(&t, 0, gen, conn);
    TEST_ASSERT_FALSE(t.e[0].resend);
    table_reset();
}

TEST_CASE("meta table: a reconnect asks every path again and voids answers in flight", "[sk_meta]")
{
    table_reset();
    declare("a.b", "{\"units\":\"V\"}");
    declare("c.d", "{\"units\":\"A\"}");
    espos_sk_meta_leg_done(&t, 0, t.e[0].gen, ESPOS_SK_META_DONE);
    uint32_t gen = t.e[1].gen; /* a leg for c.d is running */
    espos_sk_meta_connected(&t);
    espos_sk_meta_leg_done(&t, 1, gen, ESPOS_SK_META_DONE);
    TEST_ASSERT_EQUAL(0, reconciled());
    TEST_ASSERT_EQUAL(0, espos_sk_meta_wait(&t, true, 0));
    table_reset();
}

TEST_CASE("meta table: paths left back off, longer each round, at once after a reconnect", "[sk_meta]")
{
    table_reset();
    declare("a.b", "{\"units\":\"V\"}");
    espos_sk_meta_leg_done(&t, 0, t.e[0].gen, ESPOS_SK_META_BACK_OFF);
    TEST_ASSERT_EQUAL(1000, espos_sk_meta_round_end(&t, 5000, 1000));
    TEST_ASSERT_EQUAL(1, t.round);
    TEST_ASSERT_EQUAL(400, espos_sk_meta_wait(&t, true, 5600));
    TEST_ASSERT_EQUAL(0, espos_sk_meta_wait(&t, true, 6000));
    espos_sk_meta_round_end(&t, 6000, 2000);
    TEST_ASSERT_EQUAL(2, t.round);
    TEST_ASSERT_EQUAL(2000, espos_sk_meta_wait(&t, true, 6000));
    espos_sk_meta_connected(&t);
    TEST_ASSERT_EQUAL(0, t.round);
    TEST_ASSERT_EQUAL(0, espos_sk_meta_wait(&t, true, 6000));
    table_reset();
}

TEST_CASE("meta table: the next round starts past the path that ended the last", "[sk_meta]")
{
    table_reset();
    declare("a", "{\"units\":\"V\"}");
    declare("b", "{\"units\":\"V\"}");
    declare("c", "{\"units\":\"V\"}");
    TEST_ASSERT_EQUAL(0, espos_sk_meta_index(&t, 3, 0));
    espos_sk_meta_leg_done(&t, 0, t.e[0].gen, ESPOS_SK_META_BACK_OFF);
    espos_sk_meta_round_end(&t, 0, 1000);
    TEST_ASSERT_EQUAL(1, espos_sk_meta_index(&t, 3, 0));
    TEST_ASSERT_EQUAL(2, espos_sk_meta_index(&t, 3, 1));
    TEST_ASSERT_EQUAL(0, espos_sk_meta_index(&t, 3, 2));
    /* a refused token moves the start the same way */
    espos_sk_meta_leg_done(&t, 2, t.e[2].gen, ESPOS_SK_META_UNAUTHORIZED);
    TEST_ASSERT_EQUAL(0, espos_sk_meta_index(&t, 3, 0));
    TEST_ASSERT_EQUAL(0, reconciled());
    table_reset();
}

TEST_CASE("meta table: a 4xx leaves the path open and the start where it was", "[sk_meta]")
{
    table_reset();
    declare("a", "{\"units\":\"V\"}");
    declare("b", "{\"units\":\"V\"}");
    espos_sk_meta_leg_done(&t, 0, t.e[0].gen, ESPOS_SK_META_LATER);
    espos_sk_meta_leg_done(&t, 1, t.e[1].gen, ESPOS_SK_META_DONE);
    TEST_ASSERT_EQUAL(0, espos_sk_meta_index(&t, 2, 0));
    TEST_ASSERT_EQUAL(1, reconciled());
    TEST_ASSERT_EQUAL(1000, espos_sk_meta_round_end(&t, 0, 1000));
    table_reset();
}

TEST_CASE("meta table: a full table refuses a new path, not a known one", "[sk_meta]")
{
    table_reset();
    char p[16];
    for (int i = 0; i < ESPOS_SK_META_CAP; i++) {
        snprintf(p, sizeof(p), "p%d", i);
        declare(p, "{\"units\":\"V\"}");
    }
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, espos_sk_meta_declare(&t, "extra", strdup("{}"), 0));
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_meta_declare(&t, "p0", strdup("{\"units\":\"A\"}"), 0));
    table_reset();
}
