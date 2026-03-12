#include <gtest/gtest.h>
#include "protocoll/state/crdt/lww_register.h"
#include <cstring>
#include <string>

using namespace protocoll;

TEST(LwwRegister, SetAndGet) {
    LwwRegister reg(1);
    std::string val = "hello";
    reg.set(reinterpret_cast<const uint8_t*>(val.data()), val.size(), 1000);

    EXPECT_EQ(reg.value().size(), 5u);
    EXPECT_EQ(std::string(reg.value().begin(), reg.value().end()), "hello");
    EXPECT_EQ(reg.timestamp(), 1000u);
    EXPECT_EQ(reg.node_id(), 1);
}

TEST(LwwRegister, MergeNewerWins) {
    LwwRegister reg(1);
    std::string v1 = "old";
    reg.set(reinterpret_cast<const uint8_t*>(v1.data()), v1.size(), 1000);

    // Incoming with higher timestamp
    LwwRegister other(2);
    std::string v2 = "new";
    other.set(reinterpret_cast<const uint8_t*>(v2.data()), v2.size(), 2000);

    auto snapshot = other.snapshot();
    ASSERT_TRUE(reg.merge(snapshot.data(), snapshot.size()));

    EXPECT_EQ(std::string(reg.value().begin(), reg.value().end()), "new");
    EXPECT_EQ(reg.timestamp(), 2000u);
}

TEST(LwwRegister, MergeOlderLoses) {
    LwwRegister reg(1);
    std::string v1 = "current";
    reg.set(reinterpret_cast<const uint8_t*>(v1.data()), v1.size(), 2000);

    // Incoming with lower timestamp
    LwwRegister other(2);
    std::string v2 = "stale";
    other.set(reinterpret_cast<const uint8_t*>(v2.data()), v2.size(), 1000);

    auto snapshot = other.snapshot();
    EXPECT_FALSE(reg.merge(snapshot.data(), snapshot.size()));

    EXPECT_EQ(std::string(reg.value().begin(), reg.value().end()), "current");
}

TEST(LwwRegister, TieBreakByNodeId) {
    LwwRegister reg(1);
    std::string v1 = "node1";
    reg.set(reinterpret_cast<const uint8_t*>(v1.data()), v1.size(), 1000);

    // Same timestamp, higher node_id wins
    LwwRegister other(2);
    std::string v2 = "node2";
    other.set(reinterpret_cast<const uint8_t*>(v2.data()), v2.size(), 1000);

    auto snapshot = other.snapshot();
    ASSERT_TRUE(reg.merge(snapshot.data(), snapshot.size()));
    EXPECT_EQ(std::string(reg.value().begin(), reg.value().end()), "node2");
}

TEST(LwwRegister, SnapshotRoundTrip) {
    LwwRegister reg(42);
    std::string val = "test data";
    reg.set(reinterpret_cast<const uint8_t*>(val.data()), val.size(), 5000);

    auto snap = reg.snapshot();

    LwwRegister restored(0);
    ASSERT_TRUE(restored.merge(snap.data(), snap.size()));
    EXPECT_EQ(restored.timestamp(), 5000u);
    EXPECT_EQ(restored.node_id(), 42);
    EXPECT_EQ(std::string(restored.value().begin(), restored.value().end()), "test data");
}

TEST(LwwRegister, DeltaTracking) {
    LwwRegister reg(1);
    EXPECT_FALSE(reg.has_pending_delta());
    EXPECT_TRUE(reg.delta().empty());

    std::string val = "x";
    reg.set(reinterpret_cast<const uint8_t*>(val.data()), val.size(), 100);
    EXPECT_TRUE(reg.has_pending_delta());

    auto d = reg.delta();
    EXPECT_FALSE(d.empty());
    EXPECT_FALSE(reg.has_pending_delta()); // Cleared after delta()
    EXPECT_TRUE(reg.delta().empty());      // No new changes
}
