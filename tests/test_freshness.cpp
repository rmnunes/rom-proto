#include <gtest/gtest.h>
#include "protocoll/reliability/freshness.h"

using namespace protocoll;

TEST(FreshnessStamp, NoDeadline) {
    FreshnessStamp fs{1000, 0};
    EXPECT_TRUE(fs.is_fresh(999999)); // Always fresh
    EXPECT_EQ(fs.remaining(999999), UINT32_MAX);
}

TEST(FreshnessStamp, WithinDeadline) {
    FreshnessStamp fs{1000, 5000}; // Created at 1000us, deadline 5000us
    EXPECT_TRUE(fs.is_fresh(3000));  // 2000us age < 5000us
    EXPECT_EQ(fs.remaining(3000), 3000u);
}

TEST(FreshnessStamp, Expired) {
    FreshnessStamp fs{1000, 5000};
    EXPECT_FALSE(fs.is_fresh(7000)); // 6000us age > 5000us
    EXPECT_EQ(fs.remaining(7000), 0u);
}

TEST(FreshnessStamp, ExactDeadline) {
    FreshnessStamp fs{1000, 5000};
    EXPECT_TRUE(fs.is_fresh(6000)); // 5000us age == 5000us deadline
}

TEST(DeltaOutbox, PushAndPop) {
    DeltaOutbox outbox;
    OutboxEntry e{};
    e.path_hash = 1;
    e.freshness = {1000, 0}; // No deadline
    e.data = {1, 2, 3};
    outbox.push(std::move(e));

    EXPECT_EQ(outbox.size(), 1u);

    OutboxEntry out;
    ASSERT_TRUE(outbox.pop(out, 999999));
    EXPECT_EQ(out.path_hash, 1u);
    EXPECT_EQ(outbox.size(), 0u);
}

TEST(DeltaOutbox, DropsStaleOnPop) {
    DeltaOutbox outbox;

    // Stale entry
    OutboxEntry stale{};
    stale.path_hash = 1;
    stale.freshness = {1000, 1000}; // Expires at 2000us
    outbox.push(std::move(stale));

    // Fresh entry
    OutboxEntry fresh{};
    fresh.path_hash = 2;
    fresh.freshness = {1000, 100000}; // Expires at 101000us
    outbox.push(std::move(fresh));

    OutboxEntry out;
    ASSERT_TRUE(outbox.pop(out, 50000)); // Now is 50000us
    EXPECT_EQ(out.path_hash, 2u); // Stale one was dropped
    EXPECT_EQ(outbox.size(), 0u);
}

TEST(DeltaOutbox, GarbageCollect) {
    DeltaOutbox outbox;

    for (uint32_t i = 0; i < 5; i++) {
        OutboxEntry e{};
        e.path_hash = i;
        e.freshness = {i * 1000, 2000}; // Each has 2ms lifetime
        outbox.push(std::move(e));
    }

    size_t dropped = outbox.gc(10000); // All created before 8000 are stale
    EXPECT_GT(dropped, 0u);
}

TEST(DeltaOutbox, Coalesce) {
    DeltaOutbox outbox;

    // Multiple updates to same path — only latest should survive
    for (int i = 0; i < 5; i++) {
        OutboxEntry e{};
        e.path_hash = 42;
        e.freshness = {static_cast<uint32_t>(i * 100), 0};
        e.data = {static_cast<uint8_t>(i)};
        outbox.push(std::move(e));
    }

    // Different path
    OutboxEntry other{};
    other.path_hash = 99;
    other.freshness = {0, 0};
    other.data = {0xFF};
    outbox.push(std::move(other));

    EXPECT_EQ(outbox.size(), 6u);

    size_t removed = outbox.coalesce();
    EXPECT_EQ(removed, 4u); // 4 intermediate versions of path 42 removed
    EXPECT_EQ(outbox.size(), 2u); // Latest of path 42 + path 99

    // Verify the latest version survived
    OutboxEntry out;
    outbox.pop(out, 0);
    EXPECT_EQ(out.path_hash, 42u);
    EXPECT_EQ(out.data[0], 4); // Last written value
}

TEST(DeltaOutbox, EmptyPop) {
    DeltaOutbox outbox;
    OutboxEntry out;
    EXPECT_FALSE(outbox.pop(out, 0));
}

TEST(DeltaOutbox, AllStale) {
    DeltaOutbox outbox;
    OutboxEntry e{};
    e.freshness = {100, 100}; // Expires at 200us
    outbox.push(std::move(e));

    OutboxEntry out;
    EXPECT_FALSE(outbox.pop(out, 1000)); // Way past deadline
    EXPECT_TRUE(outbox.empty());
}
