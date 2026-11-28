/*
 * SPDX-FileCopyrightText: 2026 Marcel Verpaalen
 * SPDX-License-Identifier: Apache-2.0
 *
 * When the client may start something on the network. A firmware download
 * holds it back like a missing network does: no token leg, probe, metadata
 * fetch or stream connect beside the download.
 */
#include "unity.h"
#include "espos_sk_priv.h"

TEST_CASE("the client dials with a network and no update running", "[sk_dial]")
{
    TEST_ASSERT_TRUE(espos_sk_may_dial(true, false));
}

TEST_CASE("a firmware download holds the client back", "[sk_dial]")
{
    TEST_ASSERT_FALSE(espos_sk_may_dial(true, true));
}

TEST_CASE("no network holds the client back, update or not", "[sk_dial]")
{
    TEST_ASSERT_FALSE(espos_sk_may_dial(false, false));
    TEST_ASSERT_FALSE(espos_sk_may_dial(false, true));
}

TEST_CASE("a lost FINISHED is let go once the update owners say it is over", "[sk_dial]")
{
    const uint32_t late = ESPOS_SK_UPDATE_HOLD_CHECK_MS;
    /* still downloading: kept, however long */
    TEST_ASSERT_FALSE(espos_sk_update_hold_stale(10 * late, true, true));
    /* over, but only just started: the event may still be on its way */
    TEST_ASSERT_FALSE(espos_sk_update_hold_stale(late - 1, true, false));
    /* over, and long enough: let go */
    TEST_ASSERT_TRUE(espos_sk_update_hold_stale(late, true, false));
    /* nobody to ask: only the event ends it */
    TEST_ASSERT_FALSE(espos_sk_update_hold_stale(10 * late, false, false));
}
