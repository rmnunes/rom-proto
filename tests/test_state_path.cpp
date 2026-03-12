#include <gtest/gtest.h>
#include "protocoll/state/state_path.h"

using namespace protocoll;

TEST(StatePath, Basic) {
    StatePath p("/app/users/alice/profile");
    EXPECT_EQ(p.str(), "/app/users/alice/profile");
    EXPECT_NE(p.hash(), 0u);
    ASSERT_EQ(p.segments().size(), 4u);
    EXPECT_EQ(p.segments()[0], "app");
    EXPECT_EQ(p.segments()[1], "users");
    EXPECT_EQ(p.segments()[2], "alice");
    EXPECT_EQ(p.segments()[3], "profile");
}

TEST(StatePath, DifferentPathsDifferentHashes) {
    StatePath a("/app/users/alice");
    StatePath b("/app/users/bob");
    EXPECT_NE(a.hash(), b.hash());
}

TEST(StatePath, SamePathSameHash) {
    StatePath a("/app/users/alice");
    StatePath b("/app/users/alice");
    EXPECT_EQ(a.hash(), b.hash());
    EXPECT_EQ(a, b);
}

TEST(StatePath, WildcardMatch) {
    StatePath path("/app/users/alice/status");
    StatePath pattern("/app/users/*/status");
    EXPECT_TRUE(path.matches(pattern));
}

TEST(StatePath, WildcardNoMatch) {
    StatePath path("/app/users/alice/profile");
    StatePath pattern("/app/users/*/status");
    EXPECT_FALSE(path.matches(pattern));
}

TEST(StatePath, WildcardLengthMismatch) {
    StatePath path("/app/users/alice");
    StatePath pattern("/app/users/*/status");
    EXPECT_FALSE(path.matches(pattern));
}

TEST(StatePath, ExactMatch) {
    StatePath path("/app/config");
    StatePath pattern("/app/config");
    EXPECT_TRUE(path.matches(pattern));
}

TEST(StatePath, PrefixOf) {
    StatePath prefix("/app/users");
    StatePath full("/app/users/alice/profile");
    EXPECT_TRUE(prefix.is_prefix_of(full));
    EXPECT_FALSE(full.is_prefix_of(prefix));
}

TEST(StatePath, EmptyPath) {
    StatePath p;
    EXPECT_TRUE(p.empty());
    EXPECT_EQ(p.segments().size(), 0u);
}
