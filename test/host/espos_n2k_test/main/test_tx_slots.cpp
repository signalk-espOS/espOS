/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * TX slot bookkeeping.
 *
 * esp_twai keeps the pointer it is given and reads the frame after
 * twai_node_transmit() returns, so espOS hands it a slot rather than a stack
 * frame (espOS #153). Getting the *rule* for reusing a slot wrong does not
 * crash a host test -- it sends whatever has since overwritten that memory as
 * a PGN, on a boat, months later. Hence these, on the same reasoning that
 * pulled candump_resync_offset() out of a lambda: the arithmetic is testable,
 * the spinlock around it is not.
 */
#include "tx_slot_ring.h"
#include "unity.h"

using espos_n2k::detail::TxSlotRing;

namespace
{

/* The ring does not own its bookkeeping, so a fixture has to supply it. */
template <size_t N>
struct Ring {
    bool in_use[N] = {};
    TxSlotRing ring;
    Ring() { ring.init(in_use, N); }
    TxSlotRing *operator->() { return &ring; }
};

/* claim + submitted(accepted), the ordinary path. */
size_t send(TxSlotRing &r)
{
    const size_t idx = r.claim();
    if (idx != TxSlotRing::kNone) r.submitted(idx, true);
    return idx;
}

}  // namespace

TEST_CASE("a fresh ring hands out every slot exactly once", "[txring]")
{
    Ring<4> r;
    bool seen[4] = {};
    for (int i = 0; i < 4; i++) {
        const size_t idx = send(r.ring);
        TEST_ASSERT_TRUE_MESSAGE(idx != TxSlotRing::kNone, "the ring refused a free slot");
        TEST_ASSERT_FALSE_MESSAGE(seen[idx], "the same slot was handed out twice");
        seen[idx] = true;
    }
    TEST_ASSERT_EQUAL_size_t(4, r->in_use_count());
}

TEST_CASE("a full ring refuses rather than reusing a slot the driver holds", "[txring]")
{
    Ring<2> r;
    TEST_ASSERT_TRUE_MESSAGE(send(r.ring) != TxSlotRing::kNone, "the ring refused a free slot");
    TEST_ASSERT_TRUE_MESSAGE(send(r.ring) != TxSlotRing::kNone, "the ring refused a free slot");
    /* The refusal is the whole point: overwriting an in-flight slot is the bug
     * this ring exists to prevent, and set() counts a drop instead. */
    TEST_ASSERT_TRUE_MESSAGE(send(r.ring) == TxSlotRing::kNone, "a full ring handed out a slot the driver holds");
    TEST_ASSERT_EQUAL_size_t(2, r->in_use_count());
}

TEST_CASE("a released slot is handed out again", "[txring]")
{
    Ring<2> r;
    const size_t first = send(r.ring);
    send(r.ring);
    TEST_ASSERT_TRUE_MESSAGE(send(r.ring) == TxSlotRing::kNone, "a full ring handed out a slot the driver holds");
    r->release(first);
    TEST_ASSERT_EQUAL_size_t(first, send(r.ring));
}

TEST_CASE("slots are used round-robin, not the first one over and over", "[txring]")
{
    Ring<3> r;
    /* Release immediately each time: a search that always restarts at 0 would
     * return the same slot every time, which is correct but keeps one slot hot
     * and leaves the driver's own queue ordering harder to follow. */
    size_t seq[6];
    for (int i = 0; i < 6; i++) {
        seq[i] = send(r.ring);
        r->release(seq[i]);
    }
    for (int i = 1; i < 6; i++) {
        TEST_ASSERT_TRUE_MESSAGE(seq[i - 1] != seq[i], "the same slot twice in a row");
    }
}

TEST_CASE("a frame the driver refused frees its slot at once", "[txring]")
{
    Ring<1> r;
    const size_t idx = r->claim();
    TEST_ASSERT_TRUE_MESSAGE(idx != TxSlotRing::kNone, "the ring refused a free slot");
    /* twai_node_transmit() returned an error, so it never stored the pointer.
     * Keeping the slot would leak one per refusal -- and a bus that refuses is
     * exactly the bus that refuses often. */
    r->submitted(idx, false);
    TEST_ASSERT_EQUAL_size_t(0, r->in_use_count());
    TEST_ASSERT_TRUE_MESSAGE(r->claim() != TxSlotRing::kNone, "the refused frame kept its slot");
}

TEST_CASE("reclaim takes back slots the driver abandoned silently", "[txring][reclaim]")
{
    Ring<4> r;
    /* What a bus-off leaves behind: the driver refilled its hardware slots
     * from the pending queue on recovery and never called on_tx_done for what
     * was in them, so these two are claimed for ever without this. */
    send(r.ring);
    send(r.ring);

    const TxSlotRing::ReclaimTicket t = r->reclaim_begin();
    TEST_ASSERT_TRUE(t.worth_asking);
    TEST_ASSERT_EQUAL_size_t(2, r->reclaim_commit(t));
    TEST_ASSERT_EQUAL_size_t(0, r->in_use_count());
}

TEST_CASE("reclaim does not bother asking when nothing is held", "[txring][reclaim]")
{
    Ring<4> r;
    const TxSlotRing::ReclaimTicket t = r->reclaim_begin();
    TEST_ASSERT_FALSE(t.worth_asking);
    TEST_ASSERT_EQUAL_size_t(0, r->reclaim_commit(t));
}

TEST_CASE("reclaim will not ask while a claim is still being filled", "[txring][reclaim]")
{
    Ring<4> r;
    /* transmit() has claimed a slot but has not reached the driver yet. The
     * driver would truthfully answer "idle" about a frame it is about to be
     * given, and freeing that slot would put two writers on it. */
    const size_t idx = r->claim();
    const TxSlotRing::ReclaimTicket t = r->reclaim_begin();
    TEST_ASSERT_FALSE_MESSAGE(t.worth_asking, "reclaim ran against a half-submitted frame");

    r->submitted(idx, true);
    TEST_ASSERT_TRUE(r->reclaim_begin().worth_asking);
}

TEST_CASE("a claim that races the idle check keeps its slot", "[txring][reclaim]")
{
    Ring<4> r;
    send(r.ring);
    const TxSlotRing::ReclaimTicket t = r->reclaim_begin();
    TEST_ASSERT_TRUE(t.worth_asking);

    /* Between the ticket and the commit the caller was off asking the driver
     * whether it was idle, and a transmit() got in. Its frame IS with the
     * driver now, so the idle answer the caller is holding is stale and every
     * slot must stay put -- including the one that really was abandoned, which
     * the next pass will take. */
    const size_t raced = send(r.ring);
    TEST_ASSERT_EQUAL_size_t(0, r->reclaim_commit(t));
    TEST_ASSERT_TRUE(r->in_use(raced));
    TEST_ASSERT_EQUAL_size_t(2, r->in_use_count());

    const TxSlotRing::ReclaimTicket again = r->reclaim_begin();
    TEST_ASSERT_EQUAL_size_t(2, r->reclaim_commit(again));
}

TEST_CASE("a claim that races and is still filling keeps its slot", "[txring][reclaim]")
{
    Ring<4> r;
    send(r.ring);
    const TxSlotRing::ReclaimTicket t = r->reclaim_begin();
    /* Same race, caught one step earlier: the racing transmit() has claimed
     * but not submitted. Both guards have to hold, not either one. */
    r->claim();
    TEST_ASSERT_EQUAL_size_t(0, r->reclaim_commit(t));
    TEST_ASSERT_EQUAL_size_t(2, r->in_use_count());
}

TEST_CASE("a ticket cannot be replayed to free a later claim", "[txring][reclaim]")
{
    Ring<4> r;
    const size_t abandoned = send(r.ring);
    const TxSlotRing::ReclaimTicket t = r->reclaim_begin();
    TEST_ASSERT_EQUAL_size_t(1, r->reclaim_commit(t));

    const size_t fresh = send(r.ring);
    /* The same ticket again, after new traffic. Nothing must come of it. */
    TEST_ASSERT_EQUAL_size_t(0, r->reclaim_commit(t));
    TEST_ASSERT_TRUE(r->in_use(fresh));
    (void)abandoned;
}

TEST_CASE("release and reclaim ignore indices that are not slots", "[txring]")
{
    Ring<2> r;
    const size_t idx = send(r.ring);
    r->release(99);
    r->submitted(99, false);
    TEST_ASSERT_TRUE_MESSAGE(r->in_use(idx), "an out-of-range index freed a live slot");
    TEST_ASSERT_FALSE(r->in_use(99));
}

TEST_CASE("an uninitialised ring refuses every claim instead of writing nowhere", "[txring]")
{
    TxSlotRing r;
    r.init(nullptr, 8);
    TEST_ASSERT_EQUAL_size_t(0, r.size());
    TEST_ASSERT_TRUE(r.claim() == TxSlotRing::kNone);
    TEST_ASSERT_FALSE(r.reclaim_begin().worth_asking);
}

TEST_CASE("init clears claims a previous run left behind", "[txring]")
{
    Ring<2> r;
    send(r.ring);
    send(r.ring);
    TEST_ASSERT_EQUAL_size_t(2, r->in_use_count());
    /* acquire() after a release() that had to leak the old ring reuses the
     * object; it must not inherit the old run's claims. */
    r->init(r.in_use, 2);
    TEST_ASSERT_EQUAL_size_t(0, r->in_use_count());
    TEST_ASSERT_TRUE_MESSAGE(send(r.ring) != TxSlotRing::kNone, "the ring refused a free slot");
}
