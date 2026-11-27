/*
 * SPDX-FileCopyrightText: 2026 Marcel Verpaalen
 * SPDX-License-Identifier: Apache-2.0
 *
 * What the scheme probe does after its plain leg. The https leg is for a port
 * that took the connection and did not speak http; after a connection that
 * never came up it would dial the same endpoint, and on a lossy link or a
 * down host it only holds heap for its full timeout.
 */
#include "unity.h"
#include "espos_sk_priv.h"

TEST_CASE("an http answer on the plain leg means plain", "[sk_probe]")
{
    TEST_ASSERT_EQUAL(ESPOS_SK_PROBE_NEXT_PLAIN, espos_sk_probe_after_plain(true, 200, false));
    TEST_ASSERT_EQUAL(ESPOS_SK_PROBE_NEXT_PLAIN, espos_sk_probe_after_plain(true, 404, false));
    /* a status with a failed perform (a 401 the client gave up on, a body
     * that did not fit) is still the server's answer */
    TEST_ASSERT_EQUAL(ESPOS_SK_PROBE_NEXT_PLAIN, espos_sk_probe_after_plain(false, 401, false));
    TEST_ASSERT_EQUAL(ESPOS_SK_PROBE_NEXT_PLAIN, espos_sk_probe_after_plain(false, 500, false));
}

TEST_CASE("a failed connect skips the https leg", "[sk_probe]")
{
    /* connect timeout, refused, unresolved: NO_ANSWER, so the scheme stays
     * provisional and is asked again later */
    TEST_ASSERT_EQUAL(ESPOS_SK_PROBE_NEXT_NO_ANSWER, espos_sk_probe_after_plain(false, 0, true));
}

TEST_CASE("connected without an http answer tries https", "[sk_probe]")
{
    /* the shape of a TLS-only port: TCP up, then a reset, a timeout or
     * bytes that are not http */
    TEST_ASSERT_EQUAL(ESPOS_SK_PROBE_NEXT_TRY_TLS, espos_sk_probe_after_plain(false, 0, false));
}
