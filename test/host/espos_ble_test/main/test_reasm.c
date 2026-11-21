/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reassembly of chained BLE 5 extended advertisements. A payload goes out
 * whole or not at all: one cut short would end in the middle of an AD
 * structure and the server would parse the stub as data.
 */
#include <string.h>

#include "ble_proto.h"
#include "unity.h"

static const uint8_t kA[6] = { 1, 2, 3, 4, 5, 6 };
static const uint8_t kB[6] = { 9, 9, 9, 9, 9, 9 };

/* Flags AD (3 bytes) split as 2 + 1, and a one-byte AD of length 0x00. */
static const uint8_t kHead[] = { 0x02, 0x01 };
static const uint8_t kTail[] = { 0x06 };
static const uint8_t kWhole[] = { 0x02, 0x01, 0x06 };

static bool feed(espos_ble_reasm_t *r, uint32_t t, const uint8_t *addr, uint8_t sid, int st,
                 const uint8_t *d, uint16_t n, const uint8_t **out, uint16_t *len)
{
    return espos_ble_reasm_feed(r, t, addr, sid, st, d, n, out, len);
}

TEST_CASE("AD structures are validated", "[reasm]")
{
    TEST_ASSERT_TRUE(espos_ble_ad_valid(kWhole, sizeof(kWhole)));
    TEST_ASSERT_TRUE(espos_ble_ad_valid(NULL, 0));
    const uint8_t padded[] = { 0x02, 0x01, 0x06, 0x00, 0xFF };
    TEST_ASSERT_TRUE(espos_ble_ad_valid(padded, sizeof(padded)));
    TEST_ASSERT_FALSE(espos_ble_ad_valid(kHead, sizeof(kHead)));
    const uint8_t over[] = { 0x07 };
    TEST_ASSERT_FALSE(espos_ble_ad_valid(over, sizeof(over)));
}

TEST_CASE("a single complete report passes straight through", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_TRUE(feed(&r, 0, kA, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kWhole, sizeof(kWhole), &out, &len));
    TEST_ASSERT_EQUAL_PTR(kWhole, out);
    TEST_ASSERT_EQUAL_UINT16(sizeof(kWhole), len);
    TEST_ASSERT_EQUAL_UINT32(0, r.dropped);
}

TEST_CASE("a chain is joined in order and handed out once", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_FALSE(feed(&r, 0, kA, 3, ESPOS_BLE_EXT_DATA_INCOMPLETE, kHead, sizeof(kHead), &out, &len));
    TEST_ASSERT_TRUE(feed(&r, 1, kA, 3, ESPOS_BLE_EXT_DATA_COMPLETE, kTail, sizeof(kTail), &out, &len));
    TEST_ASSERT_EQUAL_UINT16(sizeof(kWhole), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kWhole, out, sizeof(kWhole));
    TEST_ASSERT_EQUAL_UINT32(0, r.dropped);
}

TEST_CASE("interleaved chains from two advertisers both complete", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_FALSE(feed(&r, 0, kA, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, kHead, 2, &out, &len));
    TEST_ASSERT_FALSE(feed(&r, 1, kB, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, kHead, 2, &out, &len));
    TEST_ASSERT_TRUE(feed(&r, 2, kA, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kTail, 1, &out, &len));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kWhole, out, sizeof(kWhole));
    TEST_ASSERT_TRUE(feed(&r, 3, kB, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kTail, 1, &out, &len));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kWhole, out, sizeof(kWhole));
    TEST_ASSERT_EQUAL_UINT32(0, r.dropped);
}

TEST_CASE("a stale head is not glued to the next event", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_FALSE(feed(&r, 0, kA, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, kHead, 2, &out, &len));
    /* A final report after the age limit, even one that parses on its own,
     * is taken for the late tail: dropped, never forwarded as a whole. */
    TEST_ASSERT_FALSE(feed(&r, 101 + 1, kA, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kWhole, 3, &out, &len));
    TEST_ASSERT_EQUAL_UINT32(1, r.dropped);
    /* The event after that goes through. */
    TEST_ASSERT_TRUE(feed(&r, 120, kA, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kWhole, 3, &out, &len));
    TEST_ASSERT_EQUAL_PTR(kWhole, out);
}

TEST_CASE("a joined payload that does not parse is dropped", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    const uint8_t bad[] = { 0x05, 0x01 };
    TEST_ASSERT_FALSE(feed(&r, 0, kA, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, bad, 2, &out, &len));
    TEST_ASSERT_FALSE(feed(&r, 1, kA, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kTail, 1, &out, &len));
    TEST_ASSERT_EQUAL_UINT32(1, r.dropped);
    /* And a malformed single report too. */
    TEST_ASSERT_FALSE(feed(&r, 2, kB, 0, ESPOS_BLE_EXT_DATA_COMPLETE, bad, 2, &out, &len));
    TEST_ASSERT_EQUAL_UINT32(2, r.dropped);
}

TEST_CASE("a chain longer than the buffer is dropped and counted", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    uint8_t big[ESPOS_BLE_ADV_DATA_MAX];
    memset(big, 0, sizeof(big));
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_FALSE(feed(&r, 0, kA, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, big, sizeof(big), &out, &len));
    TEST_ASSERT_FALSE(feed(&r, 1, kA, 0, ESPOS_BLE_EXT_DATA_COMPLETE, big, 1, &out, &len));
    TEST_ASSERT_EQUAL_UINT32(1, r.dropped);
}

TEST_CASE("a single report longer than the buffer is dropped", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    uint8_t big[ESPOS_BLE_ADV_DATA_MAX + 1] = { 0 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_FALSE(feed(&r, 0, kA, 0, ESPOS_BLE_EXT_DATA_COMPLETE, big, sizeof(big), &out, &len));
    TEST_ASSERT_EQUAL_UINT32(1, r.dropped);
}

TEST_CASE("a truncated chain is dropped, not forwarded cut", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_FALSE(feed(&r, 0, kA, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, kHead, 2, &out, &len));
    TEST_ASSERT_FALSE(feed(&r, 1, kA, 0, ESPOS_BLE_EXT_DATA_TRUNCATED, kTail, 1, &out, &len));
    TEST_ASSERT_EQUAL_UINT32(1, r.dropped);
    TEST_ASSERT_FALSE(feed(&r, 2, kB, 0, ESPOS_BLE_EXT_DATA_TRUNCATED, kHead, 2, &out, &len));
    TEST_ASSERT_EQUAL_UINT32(2, r.dropped);
}

TEST_CASE("a fifth chain evicts the oldest", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    uint8_t addr[6] = { 0 };
    for (uint8_t i = 0; i < ESPOS_BLE_REASM_SLOTS + 1; i++) {
        addr[5] = i;
        TEST_ASSERT_FALSE(feed(&r, i, addr, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, kHead, 2, &out, &len));
    }
    TEST_ASSERT_EQUAL_UINT32(1, r.dropped);
    /* The evicted chain's tail is discarded, even one that would parse on
     * its own, and not counted twice. */
    addr[5] = 0;
    TEST_ASSERT_FALSE(feed(&r, 9, addr, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kWhole, 3, &out, &len));
    TEST_ASSERT_EQUAL_UINT32(1, r.dropped);
    /* Its next event is accepted again. */
    TEST_ASSERT_TRUE(feed(&r, 10, addr, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kWhole, 3, &out, &len));
    addr[5] = ESPOS_BLE_REASM_SLOTS;
    TEST_ASSERT_TRUE(feed(&r, 9, addr, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kTail, 1, &out, &len));
}

TEST_CASE("more chains than both arrays hold resynchronise", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    uint8_t addr[6] = { 0 };
    /* Slots fill with 0..3, then 4..7 evict them into the skip list, and 8
     * evicts 4 with the skip list full. */
    for (uint8_t i = 0; i < 3 * ESPOS_BLE_REASM_SLOTS + 1; i++) {
        addr[5] = i;
        TEST_ASSERT_FALSE(feed(&r, 0, addr, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, kHead, 2, &out, &len));
    }
    /* 4's tail is untracked, so no lone final report goes out for now ... */
    addr[5] = 4;
    TEST_ASSERT_FALSE(feed(&r, 1, addr, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kWhole, 3, &out, &len));
    /* ... while chains still being followed complete normally ... */
    addr[5] = 3 * ESPOS_BLE_REASM_SLOTS;
    TEST_ASSERT_TRUE(feed(&r, 2, addr, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kTail, 1, &out, &len));
    /* ... and once the age limit has passed, singles flow again. */
    addr[5] = 200;
    TEST_ASSERT_TRUE(feed(&r, 500, addr, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kWhole, 3, &out, &len));
}

TEST_CASE("reset forgets pending chains without counting them", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    feed(&r, 0, kA, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, kHead, 2, &out, &len);
    espos_ble_reasm_reset(&r);
    TEST_ASSERT_EQUAL_UINT32(0, r.dropped);
    TEST_ASSERT_TRUE(feed(&r, 1, kA, 0, ESPOS_BLE_EXT_DATA_COMPLETE, kWhole, 3, &out, &len));
}
