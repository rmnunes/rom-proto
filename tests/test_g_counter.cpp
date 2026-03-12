#include <gtest/gtest.h>
#include "protocoll/state/crdt/g_counter.h"

using namespace protocoll;

TEST(GCounter, IncrementAndValue) {
    GCounter gc(1);
    EXPECT_EQ(gc.value(), 0u);
    gc.increment(5);
    EXPECT_EQ(gc.value(), 5u);
    gc.increment(3);
    EXPECT_EQ(gc.value(), 8u);
    EXPECT_EQ(gc.local_count(), 8u);
}

TEST(GCounter, MergeFromOtherNode) {
    GCounter gc1(1), gc2(2);
    gc1.increment(10);
    gc2.increment(20);

    auto snap = gc2.snapshot();
    ASSERT_TRUE(gc1.merge(snap.data(), snap.size()));

    EXPECT_EQ(gc1.value(), 30u); // 10 (node1) + 20 (node2)
    EXPECT_EQ(gc1.local_count(), 10u);
}

TEST(GCounter, MergeIdempotent) {
    GCounter gc1(1), gc2(2);
    gc1.increment(10);
    gc2.increment(20);

    auto snap = gc2.snapshot();
    ASSERT_TRUE(gc1.merge(snap.data(), snap.size()));
    EXPECT_FALSE(gc1.merge(snap.data(), snap.size())); // No change

    EXPECT_EQ(gc1.value(), 30u);
}

TEST(GCounter, MergeTakesMax) {
    GCounter gc1(1), gc2(1);
    gc1.increment(10);
    gc2.increment(5);

    // gc2 says node1=5, but gc1 already has node1=10 → keep 10
    auto snap = gc2.snapshot();
    EXPECT_FALSE(gc1.merge(snap.data(), snap.size()));
    EXPECT_EQ(gc1.value(), 10u);
}

TEST(GCounter, ThreeNodeConvergence) {
    GCounter a(1), b(2), c(3);
    a.increment(5);
    b.increment(10);
    c.increment(15);

    // a merges b's snapshot
    auto snap_b = b.snapshot();
    a.merge(snap_b.data(), snap_b.size());

    // a merges c's snapshot
    auto snap_c = c.snapshot();
    a.merge(snap_c.data(), snap_c.size());

    EXPECT_EQ(a.value(), 30u); // 5 + 10 + 15

    // b merges a's snapshot (which includes all three)
    auto snap_a = a.snapshot();
    b.merge(snap_a.data(), snap_a.size());
    EXPECT_EQ(b.value(), 30u); // Converged
}

TEST(GCounter, DeltaIsMinimal) {
    GCounter gc(1);
    gc.increment(5);
    auto d = gc.delta();
    ASSERT_FALSE(d.empty());

    // Delta should have 1 entry (just our node)
    EXPECT_EQ(d[0], 1u); // entry_count = 1

    // Apply delta to fresh counter
    GCounter other(2);
    ASSERT_TRUE(other.merge(d.data(), d.size()));
    EXPECT_EQ(other.value(), 5u);
}

TEST(GCounter, DeltaTracking) {
    GCounter gc(1);
    EXPECT_FALSE(gc.has_pending_delta());
    EXPECT_TRUE(gc.delta().empty());

    gc.increment(1);
    EXPECT_TRUE(gc.has_pending_delta());

    auto d = gc.delta();
    EXPECT_FALSE(d.empty());
    EXPECT_FALSE(gc.has_pending_delta());
}

TEST(GCounter, SnapshotRoundTrip) {
    GCounter gc(1);
    gc.increment(100);

    auto snap = gc.snapshot();
    GCounter restored(2);
    ASSERT_TRUE(restored.merge(snap.data(), snap.size()));
    EXPECT_EQ(restored.value(), 100u);
}

TEST(GCounter, LargeValues) {
    GCounter gc(1);
    gc.increment(1ULL << 40); // > 1 trillion
    EXPECT_EQ(gc.value(), 1ULL << 40);

    auto snap = gc.snapshot();
    GCounter other(2);
    ASSERT_TRUE(other.merge(snap.data(), snap.size()));
    EXPECT_EQ(other.value(), 1ULL << 40);
}
