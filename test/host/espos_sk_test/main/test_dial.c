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
