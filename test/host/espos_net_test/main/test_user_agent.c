/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * The User-Agent is a wire contract: signalk-server tells espOS from other
 * ESP-IDF firmware by it, so its shape is pinned here.
 */
#include "unity.h"
#include "espos_net.h"

TEST_CASE("user agent: espOS version, then app name and version", "[identity]")
{
    TEST_ASSERT_EQUAL_STRING("espOS/1.2.3 demo-app/4.5.6", espos_net_user_agent());
}
