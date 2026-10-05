/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * The receiver's listener table. Several consumers share one bus through it
 * (candump server, NMEA 2000 node, the application), so a wrong handle or a
 * reused slot is one consumer silently cutting off another.
 */
#include <functional>

#include "listener_table.h"
#include "unity.h"

using Table = espos_n2k::detail::ListenerTable<std::function<void(int)>, 3>;

TEST_CASE("every listener sees every dispatch, in the order added", "[listeners]")
{
    Table t;
    int log[8] = {};
    int n = 0;
    t.add([&](int v) { log[n++] = 10 + v; });
    t.add([&](int v) { log[n++] = 20 + v; });
    t.dispatch(1);
    t.dispatch(2);
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_INT(11, log[0]);
    TEST_ASSERT_EQUAL_INT(21, log[1]);
    TEST_ASSERT_EQUAL_INT(12, log[2]);
    TEST_ASSERT_EQUAL_INT(22, log[3]);
}

TEST_CASE("a full table refuses, and a freed slot is usable again", "[listeners]")
{
    Table t;
    auto noop = [](int) {};
    const Table::Handle a = t.add(noop);
    TEST_ASSERT_NOT_EQUAL(Table::kNone, t.add(noop));
    TEST_ASSERT_NOT_EQUAL(Table::kNone, t.add(noop));
    TEST_ASSERT_EQUAL(Table::kNone, t.add(noop));
    TEST_ASSERT_TRUE(t.remove(a));
    TEST_ASSERT_NOT_EQUAL(Table::kNone, t.add(noop));
    TEST_ASSERT_EQUAL_size_t(3, t.size());
}

TEST_CASE("a stale handle cannot remove the listener now in its slot", "[listeners]")
{
    Table t;
    int calls = 0;
    const Table::Handle old = t.add([](int) {});
    TEST_ASSERT_TRUE(t.remove(old));
    /* Same slot, new owner. Removing by the old handle again must miss: with
     * slot indices as handles this is exactly how the candump server's
     * stop() would have unhooked the node. */
    const Table::Handle now = t.add([&](int) { calls++; });
    TEST_ASSERT_NOT_EQUAL(old, now);
    TEST_ASSERT_FALSE(t.remove(old));
    t.dispatch(0);
    TEST_ASSERT_EQUAL_INT(1, calls);
}

TEST_CASE("an empty callback and the null handle are refused", "[listeners]")
{
    Table t;
    TEST_ASSERT_EQUAL(Table::kNone, t.add(nullptr));
    TEST_ASSERT_FALSE(t.remove(Table::kNone));
    TEST_ASSERT_EQUAL_size_t(0, t.size());
}

TEST_CASE("dispatch_except skips only the named listener", "[listeners]")
{
    /* The node loops its own frames back to the other listeners: a candump
     * client must see them, the node itself must not read them as received. */
    Table t;
    int a = 0, b = 0;
    const Table::Handle ha = t.add([&](int) { a++; });
    t.add([&](int) { b++; });
    t.dispatch_except(ha, 0);
    TEST_ASSERT_EQUAL_INT(0, a);
    TEST_ASSERT_EQUAL_INT(1, b);
    t.dispatch_except(Table::kNone, 0);
    TEST_ASSERT_EQUAL_INT(1, a);
    TEST_ASSERT_EQUAL_INT(2, b);
}
