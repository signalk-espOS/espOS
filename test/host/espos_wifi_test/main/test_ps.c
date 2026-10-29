// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
/*
 * The power-save mode a Zigbee or Thread build hands the driver. With "none"
 * the station kept the shared radio and no device could join a coordinator,
 * so every input that the driver would read as "none" must come out as "min".
 */

#include "unity.h"
#include "wifi_ps.h"

TEST_CASE("without 802.15.4 the configured mode passes through", "[ps]")
{
    TEST_ASSERT_EQUAL_STRING("none", espos_wifi_ps_effective("none", false));
    TEST_ASSERT_EQUAL_STRING("min", espos_wifi_ps_effective("min", false));
    TEST_ASSERT_EQUAL_STRING("max", espos_wifi_ps_effective("max", false));
    TEST_ASSERT_EQUAL_STRING("none", espos_wifi_ps_effective(NULL, false));
}

TEST_CASE("with 802.15.4 none runs as min", "[ps]")
{
    TEST_ASSERT_EQUAL_STRING("min", espos_wifi_ps_effective("none", true));
}

TEST_CASE("with 802.15.4 min and max are kept", "[ps]")
{
    TEST_ASSERT_EQUAL_STRING("min", espos_wifi_ps_effective("min", true));
    TEST_ASSERT_EQUAL_STRING("max", espos_wifi_ps_effective("max", true));
}

TEST_CASE("with 802.15.4 what the driver reads as none runs as min", "[ps]")
{
    /* The device port maps every string but "min" and "max" to WIFI_PS_NONE. */
    TEST_ASSERT_EQUAL_STRING("min", espos_wifi_ps_effective("", true));
    TEST_ASSERT_EQUAL_STRING("min", espos_wifi_ps_effective("off", true));
    TEST_ASSERT_EQUAL_STRING("min", espos_wifi_ps_effective(NULL, true));
}
