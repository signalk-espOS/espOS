// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
/*
 * wifiStalled (espOS #136). Every false positive here is a restart nobody
 * wanted, so most cases are about when it must NOT fire.
 */

#include "unity.h"
#include "wifi_stall.h"

#define MIN_MS 60000u
#define STALL  (30u * MIN_MS)

static espos_wifi_stall_in_t trying(void)
{
    return (espos_wifi_stall_in_t){
        .sta_enabled = true,
        .has_networks = true,
    };
}

TEST_CASE("an unbroken run of trying stalls at the threshold", "[stall]")
{
    espos_wifi_stall_t t = { 0 };
    espos_wifi_stall_in_t in = trying();
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000, STALL));
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000 + STALL - 1, STALL));
    TEST_ASSERT_TRUE(espos_wifi_stall_update(&t, &in, 1000 + STALL, STALL));
    TEST_ASSERT_TRUE(espos_wifi_stall_update(&t, &in, 1000 + 2 * STALL, STALL));
}

TEST_CASE("an association starts the count again", "[stall]")
{
    espos_wifi_stall_t t = { 0 };
    espos_wifi_stall_in_t in = trying();
    espos_wifi_stall_update(&t, &in, 1000, STALL);
    in.associated = true;
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000 + STALL / 2, STALL));
    in.associated = false;
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000 + STALL / 2 + 1, STALL));
    /* Half the allowance was used before the connection; it is not carried over. */
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000 + STALL, STALL));
    TEST_ASSERT_TRUE(espos_wifi_stall_update(&t, &in, 1000 + STALL / 2 + 1 + STALL, STALL));
}

TEST_CASE("nothing to reach is never a stall", "[stall]")
{
    espos_wifi_stall_t t = { 0 };
    espos_wifi_stall_in_t in = trying();
    in.sta_enabled = false;
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000, STALL));
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000 + 10 * STALL, STALL));
    in = trying();
    in.has_networks = false;
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000, STALL));
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000 + 10 * STALL, STALL));
}

TEST_CASE("another transport carrying the network is never a stall", "[stall]")
{
    /* An Ethernet device with stale WiFi credentials is reachable; a restart
     * would cut the working link to retry the idle one. */
    espos_wifi_stall_t t = { 0 };
    espos_wifi_stall_in_t in = trying();
    in.other_net_up = true;
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000, STALL));
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000 + 10 * STALL, STALL));
}

TEST_CASE("someone on the portal holds it off and starts the count again", "[stall]")
{
    espos_wifi_stall_t t = { 0 };
    espos_wifi_stall_in_t in = trying();
    espos_wifi_stall_update(&t, &in, 1000, STALL);
    in.portal_clients = 1;
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000 + STALL, STALL));
    in.portal_clients = 0;
    uint32_t left = 1000 + STALL + 5;
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, left, STALL));
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, left + STALL - 1, STALL));
    TEST_ASSERT_TRUE(espos_wifi_stall_update(&t, &in, left + STALL, STALL));
}

TEST_CASE("a boot after a stall restart does not restart again", "[stall]")
{
    /* The AP the restart did not bring back is off: a second restart would be
     * the first of a loop, one every stall_s, for as long as it stays off. */
    espos_wifi_stall_t t = { 0 };
    espos_wifi_stall_in_t in = trying();
    in.suppressed = true;
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000, STALL));
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000 + 10 * STALL, STALL));
}

TEST_CASE("zero turns it off", "[stall]")
{
    espos_wifi_stall_t t = { 0 };
    espos_wifi_stall_in_t in = trying();
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000, 0));
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000 + 10 * STALL, 0));
}

TEST_CASE("a reset gives new credentials the full allowance", "[stall]")
{
    espos_wifi_stall_t t = { 0 };
    espos_wifi_stall_in_t in = trying();
    espos_wifi_stall_update(&t, &in, 1000, STALL);
    espos_wifi_stall_reset(&t);
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000 + STALL, STALL));
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, 1000 + 2 * STALL - 1, STALL));
    TEST_ASSERT_TRUE(espos_wifi_stall_update(&t, &in, 1000 + 2 * STALL, STALL));
}

TEST_CASE("the count survives the tick counter wrapping", "[stall]")
{
    espos_wifi_stall_t t = { 0 };
    espos_wifi_stall_in_t in = trying();
    uint32_t start = UINT32_MAX - MIN_MS;
    espos_wifi_stall_update(&t, &in, start, STALL);
    TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, start + STALL - 1, STALL));
    TEST_ASSERT_TRUE(espos_wifi_stall_update(&t, &in, start + STALL, STALL));
}

TEST_CASE("a run that starts at tick 0 is still counted", "[stall]")
{
    /* 0 is the "not trying" marker, so a start at exactly 0 must not read as
     * one, or the next observation would begin the run all over again. */
    espos_wifi_stall_t t = { 0 };
    espos_wifi_stall_in_t in = trying();
    espos_wifi_stall_update(&t, &in, 0, STALL);
    TEST_ASSERT_NOT_EQUAL(0, t.since_ms);
    TEST_ASSERT_TRUE(espos_wifi_stall_update(&t, &in, STALL + 1, STALL));
}

TEST_CASE("a station that associates but gets no lease is not stalled", "[stall]")
{
    /* The radio works; a restart would not mend a dead DHCP server. Each
     * association starts the count again, so alternating association and
     * DHCP timeout never reaches the threshold. */
    espos_wifi_stall_t t = { 0 };
    espos_wifi_stall_in_t in = trying();
    uint32_t now = 1000;
    for (int i = 0; i < 100; i++) {
        in.associated = false;
        TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, now, STALL));
        now += STALL / 4;
        in.associated = true;
        TEST_ASSERT_FALSE(espos_wifi_stall_update(&t, &in, now, STALL));
        now += 15000;
    }
}
