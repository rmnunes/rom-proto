#include <gtest/gtest.h>
#include "protocoll/state/version_vector.h"

using namespace protocoll;

TEST(VersionVector, IncrementAndGet) {
    VersionVector vv;
    EXPECT_EQ(vv.get(1), 0u);
    EXPECT_EQ(vv.increment(1), 1u);
    EXPECT_EQ(vv.increment(1), 2u);
    EXPECT_EQ(vv.get(1), 2u);
    EXPECT_EQ(vv.get(2), 0u); // Different node
}

TEST(VersionVector, MultipleNodes) {
    VersionVector vv;
    vv.increment(1);
    vv.increment(2);
    vv.increment(1);
    EXPECT_EQ(vv.get(1), 2u);
    EXPECT_EQ(vv.get(2), 1u);
}

TEST(VersionVector, Merge) {
    VersionVector a, b;
    a.increment(1); // a: {1:1}
    a.increment(1); // a: {1:2}
    b.increment(2); // b: {2:1}
    b.increment(1); // b: {1:1}

    a.merge(b); // a should be {1:2, 2:1}
    EXPECT_EQ(a.get(1), 2u);
    EXPECT_EQ(a.get(2), 1u);
}

TEST(VersionVector, Dominates) {
    VersionVector a, b;
    a.set(1, 3);
    a.set(2, 2);
    b.set(1, 2);
    b.set(2, 1);

    EXPECT_TRUE(a.dominates(b));
    EXPECT_FALSE(b.dominates(a));
}

TEST(VersionVector, Concurrent) {
    VersionVector a, b;
    a.set(1, 2);
    a.set(2, 1);
    b.set(1, 1);
    b.set(2, 2);

    EXPECT_TRUE(a.concurrent_with(b));
    EXPECT_TRUE(b.concurrent_with(a));
}

TEST(VersionVector, LessThan) {
    VersionVector a, b;
    a.set(1, 1);
    b.set(1, 2);
    b.set(2, 1);

    EXPECT_TRUE(a.less_than(b));
    EXPECT_FALSE(b.less_than(a));
}

TEST(VersionVector, Equality) {
    VersionVector a, b;
    a.set(1, 5);
    a.set(2, 3);
    b.set(2, 3);
    b.set(1, 5);

    EXPECT_TRUE(a == b);
}

TEST(VersionVector, WireRoundTrip) {
    VersionVector vv;
    vv.set(1, 42);
    vv.set(100, 999);

    std::vector<uint8_t> buf(vv.wire_size());
    ASSERT_TRUE(vv.encode(buf.data(), buf.size()));

    VersionVector decoded;
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));

    EXPECT_EQ(decoded.get(1), 42u);
    EXPECT_EQ(decoded.get(100), 999u);
    EXPECT_TRUE(vv == decoded);
}

TEST(VersionVector, EmptyWireRoundTrip) {
    VersionVector vv;

    std::vector<uint8_t> buf(vv.wire_size());
    ASSERT_TRUE(vv.encode(buf.data(), buf.size()));
    EXPECT_EQ(buf.size(), 1u); // Just the count byte

    VersionVector decoded;
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));
    EXPECT_TRUE(decoded.empty());
}
