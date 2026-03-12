#include <gtest/gtest.h>
#include "protocoll/state/crdt/pn_counter.h"

using namespace protocoll;

TEST(PnCounter, IncrementDecrement) {
    PnCounter c(1);
    c.increment(10);
    c.decrement(3);
    EXPECT_EQ(c.value(), 7);
}

TEST(PnCounter, NegativeValue) {
    PnCounter c(1);
    c.decrement(5);
    EXPECT_EQ(c.value(), -5);
}

TEST(PnCounter, MergeConverges) {
    PnCounter a(1), b(2);
    a.increment(10);
    a.decrement(2); // a: 10 - 2 = 8

    b.increment(5);
    b.decrement(1); // b: 5 - 1 = 4

    auto snap_b = b.snapshot();
    ASSERT_TRUE(a.merge(snap_b.data(), snap_b.size()));
    // a should see: P{1:10, 2:5} - N{1:2, 2:1} = 15 - 3 = 12
    EXPECT_EQ(a.value(), 12);

    auto snap_a = a.snapshot();
    b.merge(snap_a.data(), snap_a.size());
    EXPECT_EQ(b.value(), 12); // Converged
}

TEST(PnCounter, MergeIdempotent) {
    PnCounter a(1), b(2);
    a.increment(10);
    b.increment(5);

    auto snap_b = b.snapshot();
    ASSERT_TRUE(a.merge(snap_b.data(), snap_b.size()));
    EXPECT_EQ(a.value(), 15);

    EXPECT_FALSE(a.merge(snap_b.data(), snap_b.size())); // No change
    EXPECT_EQ(a.value(), 15);
}

TEST(PnCounter, DeltaRoundTrip) {
    PnCounter a(1);
    a.increment(7);
    EXPECT_TRUE(a.has_pending_delta());

    auto d = a.delta();
    EXPECT_FALSE(d.empty());
    EXPECT_FALSE(a.has_pending_delta());

    PnCounter b(2);
    ASSERT_TRUE(b.merge(d.data(), d.size()));
    EXPECT_EQ(b.value(), 7);
}

TEST(PnCounter, SnapshotRoundTrip) {
    PnCounter a(1);
    a.increment(100);
    a.decrement(30);

    auto snap = a.snapshot();
    PnCounter b(2);
    b.merge(snap.data(), snap.size());
    EXPECT_EQ(b.value(), 70);
}
