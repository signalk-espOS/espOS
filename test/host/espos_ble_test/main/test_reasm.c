/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reassembly of chained BLE 5 extended advertisements. A payload goes out
 * whole or not at all: one cut at the buffer size would end in the middle of
 * an AD structure and the server would parse the stub as data.
 */
#include <string.h>

#include "ble_proto.h"
#include "unity.h"

static const uint8_t kA[6] = { 1, 2, 3, 4, 5, 6 };
static const uint8_t kB[6] = { 9, 9, 9, 9, 9, 9 };

TEST_CASE("a single complete report passes straight through", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t in[] = { 0x02, 0x01, 0x06 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_TRUE(espos_ble_reasm_feed(&r, kA, 0, ESPOS_BLE_EXT_DATA_COMPLETE, in,
                                          sizeof(in), &out, &len));
    TEST_ASSERT_EQUAL_PTR(in, out);
    TEST_ASSERT_EQUAL_UINT16(sizeof(in), len);
    TEST_ASSERT_EQUAL_UINT32(0, r.dropped);
}

TEST_CASE("a chain is joined in order and handed out once", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t p1[] = { 1, 2, 3 }, p2[] = { 4, 5 }, p3[] = { 6 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_FALSE(espos_ble_reasm_feed(&r, kA, 3, ESPOS_BLE_EXT_DATA_INCOMPLETE, p1, 3, &out, &len));
    TEST_ASSERT_FALSE(espos_ble_reasm_feed(&r, kA, 3, ESPOS_BLE_EXT_DATA_INCOMPLETE, p2, 2, &out, &len));
    TEST_ASSERT_TRUE(espos_ble_reasm_feed(&r, kA, 3, ESPOS_BLE_EXT_DATA_COMPLETE, p3, 1, &out, &len));
    const uint8_t want[] = { 1, 2, 3, 4, 5, 6 };
    TEST_ASSERT_EQUAL_UINT16(sizeof(want), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, sizeof(want));
    TEST_ASSERT_FALSE(r.active);
    TEST_ASSERT_EQUAL_UINT32(0, r.dropped);
}

TEST_CASE("a chain longer than the buffer is dropped and counted", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    uint8_t big[ESPOS_BLE_ADV_DATA_MAX];
    memset(big, 0xAA, sizeof(big));
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_FALSE(espos_ble_reasm_feed(&r, kA, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, big,
                                           sizeof(big), &out, &len));
    TEST_ASSERT_FALSE(espos_ble_reasm_feed(&r, kA, 0, ESPOS_BLE_EXT_DATA_COMPLETE, big, 1, &out, &len));
    TEST_ASSERT_EQUAL_UINT32(1, r.dropped);
    TEST_ASSERT_FALSE(r.active);
}

TEST_CASE("a single report longer than the buffer is dropped", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    uint8_t big[ESPOS_BLE_ADV_DATA_MAX + 1] = { 0 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_FALSE(espos_ble_reasm_feed(&r, kA, 0, ESPOS_BLE_EXT_DATA_COMPLETE, big,
                                           sizeof(big), &out, &len));
    TEST_ASSERT_EQUAL_UINT32(1, r.dropped);
}

TEST_CASE("a truncated chain is dropped, not forwarded cut", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t p[] = { 1, 2 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_FALSE(espos_ble_reasm_feed(&r, kA, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, p, 2, &out, &len));
    TEST_ASSERT_FALSE(espos_ble_reasm_feed(&r, kA, 0, ESPOS_BLE_EXT_DATA_TRUNCATED, p, 2, &out, &len));
    TEST_ASSERT_EQUAL_UINT32(1, r.dropped);
    TEST_ASSERT_FALSE(r.active);
}

TEST_CASE("another advertiser mid-chain abandons the pending one", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t p[] = { 1, 2 }, q[] = { 7 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    TEST_ASSERT_FALSE(espos_ble_reasm_feed(&r, kA, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, p, 2, &out, &len));
    TEST_ASSERT_TRUE(espos_ble_reasm_feed(&r, kB, 0, ESPOS_BLE_EXT_DATA_COMPLETE, q, 1, &out, &len));
    TEST_ASSERT_EQUAL_PTR(q, out);
    TEST_ASSERT_EQUAL_UINT32(1, r.dropped);
    /* Same address, other SID: a different advertising set, also abandons. */
    TEST_ASSERT_FALSE(espos_ble_reasm_feed(&r, kA, 1, ESPOS_BLE_EXT_DATA_INCOMPLETE, p, 2, &out, &len));
    TEST_ASSERT_FALSE(espos_ble_reasm_feed(&r, kA, 2, ESPOS_BLE_EXT_DATA_INCOMPLETE, p, 2, &out, &len));
    TEST_ASSERT_EQUAL_UINT32(2, r.dropped);
}

TEST_CASE("reset forgets a pending chain without counting it", "[reasm]")
{
    espos_ble_reasm_t r = { 0 };
    const uint8_t p[] = { 1 };
    const uint8_t *out = NULL;
    uint16_t len = 0;
    espos_ble_reasm_feed(&r, kA, 0, ESPOS_BLE_EXT_DATA_INCOMPLETE, p, 1, &out, &len);
    espos_ble_reasm_reset(&r);
    TEST_ASSERT_FALSE(r.active);
    TEST_ASSERT_EQUAL_UINT32(0, r.dropped);
    TEST_ASSERT_TRUE(espos_ble_reasm_feed(&r, kA, 0, ESPOS_BLE_EXT_DATA_COMPLETE, p, 1, &out, &len));
}
