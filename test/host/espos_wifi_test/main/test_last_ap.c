// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
/*
 * Whether a remembered AP may be used to skip the channel scan (espOS #136).
 *
 * Every case here costs a failed association if judged wrongly, which is the
 * delay the caller is trying to remove in the first place.
 */

#include <string.h>

#include "unity.h"
#include "wifi_last_ap.h"

static const uint8_t BSSID_A[6] = { 0x1c, 0x0b, 0x8b, 0x90, 0xda, 0x8f };

static espos_wifi_last_ap_t good(void)
{
    espos_wifi_last_ap_t r;
    memset(&r, 0, sizeof(r));
    espos_wifi_last_ap_store(&r, "MOIN", BSSID_A, 161);
    return r;
}

TEST_CASE("a stored record is usable for the same SSID", "[last_ap]")
{
    espos_wifi_last_ap_t r = good();
    TEST_ASSERT_TRUE(espos_wifi_last_ap_usable(&r, "MOIN", 3, false));
    TEST_ASSERT_EQUAL_STRING("MOIN", r.ssid);
    TEST_ASSERT_EQUAL_UINT8(161, r.channel);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BSSID_A, r.bssid, 6);
}

TEST_CASE("an unwritten record is not usable", "[last_ap]")
{
    /* What RTC memory looks like after a cold power-on: all zeroes here, but the
     * point is that no magic was ever written. */
    espos_wifi_last_ap_t r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_FALSE(espos_wifi_last_ap_usable(&r, "MOIN", 3, false));
}

TEST_CASE("garbage that happens to carry the magic is rejected by the checksum", "[last_ap]")
{
    /* The reason the checksum is not redundant with the magic: uninitialised RTC
     * RAM can hold any word, including this one. Acting on the garbage channel
     * would spend a real association attempt on every boot. */
    espos_wifi_last_ap_t r;
    memset(&r, 0xA5, sizeof(r));
    r.magic = ESPOS_WIFI_LAST_AP_MAGIC;
    TEST_ASSERT_FALSE(espos_wifi_last_ap_usable(&r, "MOIN", 3, false));
}

TEST_CASE("a record for another SSID is not usable", "[last_ap]")
{
    /* Reconnecting to a different network in the list must not inherit this
     * one's BSSID -- that would pin the wrong AP entirely. */
    espos_wifi_last_ap_t r = good();
    TEST_ASSERT_FALSE(espos_wifi_last_ap_usable(&r, "OTHER", 3, false));
}

TEST_CASE("an exhausted attempt budget falls back to a full scan", "[last_ap]")
{
    espos_wifi_last_ap_t r = good();
    TEST_ASSERT_TRUE(espos_wifi_last_ap_usable(&r, "MOIN", 1, false));
    TEST_ASSERT_FALSE(espos_wifi_last_ap_usable(&r, "MOIN", 0, false));
}

TEST_CASE("an operator-pinned BSSID always wins over the cache", "[last_ap]")
{
    /* has_bssid means "only ever this BSSID". The cache must not override a
     * deliberate setting, even when it holds a perfectly good record. */
    espos_wifi_last_ap_t r = good();
    TEST_ASSERT_FALSE(espos_wifi_last_ap_usable(&r, "MOIN", 3, true));
}

TEST_CASE("channel 0 and out-of-range channels are rejected", "[last_ap]")
{
    espos_wifi_last_ap_t r = good();
    /* Re-checksummed, so these fail on the range test and not on the checksum:
     * a record can be internally consistent and still name no real channel. */
    espos_wifi_last_ap_store(&r, "MOIN", BSSID_A, 0);
    TEST_ASSERT_FALSE(espos_wifi_last_ap_usable(&r, "MOIN", 3, false));
    espos_wifi_last_ap_store(&r, "MOIN", BSSID_A, 200);
    TEST_ASSERT_FALSE(espos_wifi_last_ap_usable(&r, "MOIN", 3, false));
    /* 5 GHz channel 161 is the one the reporter's AP uses; 13 is 2.4 GHz. */
    espos_wifi_last_ap_store(&r, "MOIN", BSSID_A, 13);
    TEST_ASSERT_TRUE(espos_wifi_last_ap_usable(&r, "MOIN", 3, false));
}

TEST_CASE("a 32-character SSID round-trips and stays NUL-terminated", "[last_ap]")
{
    /* wifi_sta_config_t.ssid need not be NUL-terminated when full, so a 32-char
     * SSID is the case where a naive copy overruns or leaves the string open. */
    const char *max = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"; /* 32 */
    espos_wifi_last_ap_t r;
    memset(&r, 0, sizeof(r));
    espos_wifi_last_ap_store(&r, max, BSSID_A, 36);
    TEST_ASSERT_EQUAL_size_t(32, strlen(r.ssid));
    TEST_ASSERT_TRUE(espos_wifi_last_ap_usable(&r, max, 3, false));
}

TEST_CASE("a NULL record or SSID is not usable", "[last_ap]")
{
    espos_wifi_last_ap_t r = good();
    TEST_ASSERT_FALSE(espos_wifi_last_ap_usable(NULL, "MOIN", 3, false));
    TEST_ASSERT_FALSE(espos_wifi_last_ap_usable(&r, NULL, 3, false));
}
