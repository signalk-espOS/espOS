/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * One meta leg's outcome for the reconciliation round: a leg without an
 * answer or with a server error ends the round, and a reply the device cannot
 * read is never taken for "no meta", which would overwrite what the user set
 * on the server.
 */
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
