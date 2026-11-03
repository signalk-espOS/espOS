/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * GATT frame queue. A BMS answers each command once over its notify pipe, so
 * a frame lost between the Bluetooth task and the control socket is a lost
 * reply: order, retry at the head and the drop count are what is tested.
 */
#include <stdlib.h>
#include <string.h>

#include "ble_proto.h"
#include "unity.h"

static char *frame(const char *text)
{
    char *f = malloc(strlen(text) + 1);
    TEST_ASSERT_NOT_NULL(f);
    strcpy(f, text);
    return f;
}

static void expect_pop(espos_ble_frameq_t *q, const char *want)
{
    char *f = espos_ble_frameq_pop(q);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_STRING(want, f);
    free(f);
}

TEST_CASE("empty frame queue pops nothing", "[frameq]")
{
    char *slots[3];
    espos_ble_frameq_t q;
    espos_ble_frameq_init(&q, slots, 3);
    TEST_ASSERT_EQUAL_UINT(0, espos_ble_frameq_count(&q));
    TEST_ASSERT_NULL(espos_ble_frameq_pop(&q));
    TEST_ASSERT_FALSE(espos_ble_frameq_push(&q, NULL));
    TEST_ASSERT_EQUAL_UINT(0, espos_ble_frameq_count(&q));
}

TEST_CASE("frames come out oldest first across the wrap", "[frameq]")
{
    char *slots[3];
    espos_ble_frameq_t q;
    espos_ble_frameq_init(&q, slots, 3);
    TEST_ASSERT_FALSE(espos_ble_frameq_push(&q, frame("a")));
    TEST_ASSERT_FALSE(espos_ble_frameq_push(&q, frame("b")));
    expect_pop(&q, "a");
    TEST_ASSERT_FALSE(espos_ble_frameq_push(&q, frame("c")));
    TEST_ASSERT_FALSE(espos_ble_frameq_push(&q, frame("d")));
    TEST_ASSERT_EQUAL_UINT(3, espos_ble_frameq_count(&q));
    expect_pop(&q, "b");
    expect_pop(&q, "c");
    expect_pop(&q, "d");
    TEST_ASSERT_NULL(espos_ble_frameq_pop(&q));
    TEST_ASSERT_EQUAL_UINT32(0, q.dropped);
}

TEST_CASE("a full frame queue drops and counts the oldest", "[frameq]")
{
    char *slots[2];
    espos_ble_frameq_t q;
    espos_ble_frameq_init(&q, slots, 2);
    espos_ble_frameq_push(&q, frame("a"));
    espos_ble_frameq_push(&q, frame("b"));
    TEST_ASSERT_TRUE(espos_ble_frameq_push(&q, frame("c")));
    TEST_ASSERT_TRUE(espos_ble_frameq_push(&q, frame("d")));
    TEST_ASSERT_EQUAL_UINT32(2, q.dropped);
    TEST_ASSERT_EQUAL_UINT(2, espos_ble_frameq_count(&q));
    expect_pop(&q, "c");
    expect_pop(&q, "d");
}

TEST_CASE("a frame that failed to send goes out first next time", "[frameq]")
{
    char *slots[3];
    espos_ble_frameq_t q;
    espos_ble_frameq_init(&q, slots, 3);
    espos_ble_frameq_push(&q, frame("a"));
    espos_ble_frameq_push(&q, frame("b"));
    char *f = espos_ble_frameq_pop(&q);
    TEST_ASSERT_EQUAL_STRING("a", f);
    espos_ble_frameq_push(&q, frame("c"));
    TEST_ASSERT_TRUE(espos_ble_frameq_unshift(&q, f));
    TEST_ASSERT_EQUAL_UINT32(0, q.dropped);
    expect_pop(&q, "a");
    expect_pop(&q, "b");
    expect_pop(&q, "c");
}

TEST_CASE("the only frame, put back after a failed send, is queued again", "[frameq]")
{
    char *slots[3];
    espos_ble_frameq_t q;
    espos_ble_frameq_init(&q, slots, 3);
    espos_ble_frameq_push(&q, frame("a"));
    char *f = espos_ble_frameq_pop(&q);
    TEST_ASSERT_EQUAL_UINT(0, espos_ble_frameq_count(&q));
    TEST_ASSERT_TRUE(espos_ble_frameq_unshift(&q, f));
    TEST_ASSERT_EQUAL_UINT(1, espos_ble_frameq_count(&q));
    TEST_ASSERT_EQUAL_UINT32(0, q.dropped);
    expect_pop(&q, "a");
    TEST_ASSERT_NULL(espos_ble_frameq_pop(&q));
}

TEST_CASE("a frame put back into a queue that filled up is the one dropped", "[frameq]")
{
    char *slots[2];
    espos_ble_frameq_t q;
    espos_ble_frameq_init(&q, slots, 2);
    espos_ble_frameq_push(&q, frame("a"));
    char *f = espos_ble_frameq_pop(&q);
    espos_ble_frameq_push(&q, frame("b"));
    espos_ble_frameq_push(&q, frame("c"));
    TEST_ASSERT_FALSE(espos_ble_frameq_unshift(&q, f));
    TEST_ASSERT_EQUAL_UINT32(1, q.dropped);
    expect_pop(&q, "b");
    expect_pop(&q, "c");
}

TEST_CASE("clearing the frame queue frees without counting drops", "[frameq]")
{
    char *slots[3];
    espos_ble_frameq_t q;
    espos_ble_frameq_init(&q, slots, 3);
    espos_ble_frameq_push(&q, frame("a"));
    espos_ble_frameq_push(&q, frame("b"));
    espos_ble_frameq_clear(&q);
    TEST_ASSERT_EQUAL_UINT(0, espos_ble_frameq_count(&q));
    TEST_ASSERT_EQUAL_UINT32(0, q.dropped);
    TEST_ASSERT_NULL(espos_ble_frameq_pop(&q));
    espos_ble_frameq_push(&q, frame("c"));
    expect_pop(&q, "c");
}

TEST_CASE("a frame queue without slots drops every frame", "[frameq]")
{
    espos_ble_frameq_t q;
    espos_ble_frameq_init(&q, NULL, 0);
    TEST_ASSERT_TRUE(espos_ble_frameq_push(&q, frame("a")));
    TEST_ASSERT_EQUAL_UINT32(1, q.dropped);
    TEST_ASSERT_NULL(espos_ble_frameq_pop(&q));
}
