/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_n2k::Node's identity arithmetic: the NAME's unique number, the
 * serial code, the instance split, and which candump frames may share the bus
 * with the node.
 */
#include <cstring>

#include "node_identity.h"
#include "unity.h"

using namespace espos_n2k;
using namespace espos_n2k::detail;

TEST_CASE("the unique number is the MAC's low 21 bits", "[node]")
{
    const uint8_t mac[6] = { 0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF };
    TEST_ASSERT_EQUAL_HEX32(0x0BCDEF, unique_number_from_mac(mac));
    const uint8_t max[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    TEST_ASSERT_EQUAL_HEX32(kUniqueNumberMask, unique_number_from_mac(max));
}

TEST_CASE("the serial code is the MAC in hex", "[node]")
{
    const uint8_t mac[6] = { 0x24, 0x6F, 0x28, 0x0A, 0xCD, 0x01 };
    char s[13];
    serial_from_mac(mac, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("246F280ACD01", s);
}

TEST_CASE("the device instance splits into lower 3 and upper 5 bits", "[node]")
{
    for (int i = 0; i < 256; i++) {
        const uint8_t lo = device_instance_lower(i);
        const uint8_t hi = device_instance_upper(i);
        TEST_ASSERT_TRUE(lo < 8 && hi < 32);
        TEST_ASSERT_EQUAL_INT(i, (hi << 3) | lo);
    }
}

namespace
{
CanFrame frame(uint32_t id, bool extended = true)
{
    CanFrame f;
    f.id = id;
    f.extended = extended;
    return f;
}
}  // namespace

TEST_CASE("a client frame with the node's own source address is refused", "[node]")
{
    /* 127501 from source 34, priority 3. */
    TEST_ASSERT_FALSE(foreign_frame_allowed(frame(0x0DF20D22), 34));
    TEST_ASSERT_TRUE(foreign_frame_allowed(frame(0x0DF20D23), 34));
    /* An address claim (60928) in the node's name is refused the same way:
     * it would be the node contesting its own address. */
    TEST_ASSERT_FALSE(foreign_frame_allowed(frame(0x18EEFF22), 34));
}

TEST_CASE("without an address the node guards nothing", "[node]")
{
    TEST_ASSERT_TRUE(foreign_frame_allowed(frame(0x0DF20DFE), kNullAddress));
    TEST_ASSERT_TRUE(foreign_frame_allowed(frame(0x0DF20D22), 255));
}

TEST_CASE("standard (11-bit) frames are not NMEA 2000 and pass", "[node]")
{
    TEST_ASSERT_TRUE(foreign_frame_allowed(frame(0x122, false), 0x22));
}
