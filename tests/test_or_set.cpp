#include <gtest/gtest.h>
#include "protocoll/state/crdt/or_set.h"
#include <string>
#include <algorithm>

using namespace protocoll;

TEST(OrSet, AddContains) {
    OrSet s(1);
    s.add("alice");
    s.add("bob");
    EXPECT_TRUE(s.contains("alice"));
    EXPECT_TRUE(s.contains("bob"));
    EXPECT_FALSE(s.contains("charlie"));
    EXPECT_EQ(s.size(), 2u);
}

TEST(OrSet, Remove) {
    OrSet s(1);
    s.add("alice");
    s.add("bob");
    ASSERT_TRUE(s.remove("alice"));
    EXPECT_FALSE(s.contains("alice"));
    EXPECT_TRUE(s.contains("bob"));
    EXPECT_EQ(s.size(), 1u);
}

TEST(OrSet, RemoveNonexistent) {
    OrSet s(1);
    EXPECT_FALSE(s.remove("alice"));
}

TEST(OrSet, AddAfterRemoveReappears) {
    OrSet s(1);
    s.add("alice");
    s.remove("alice");
    EXPECT_FALSE(s.contains("alice"));
    s.add("alice"); // Re-add with new tag
    EXPECT_TRUE(s.contains("alice"));
}

TEST(OrSet, MergeAddWins) {
    // Concurrent add on A, remove on B → element should be present
    OrSet a(1), b(2);

    // Both start with "alice"
    a.add("alice");
    auto snap = a.snapshot();
    b.merge(snap.data(), snap.size());

    // A adds "alice" again (new tag), B removes "alice" (removes old tag)
    a.add("alice");
    b.remove("alice");

    // Merge B's delta into A → A should still have "alice" (add-wins)
    auto delta_b = b.delta();
    a.merge(delta_b.data(), delta_b.size());
    EXPECT_TRUE(a.contains("alice"));

    // Merge A's snapshot into B → B should now have "alice" too
    auto snap_a = a.snapshot();
    b.merge(snap_a.data(), snap_a.size());
    EXPECT_TRUE(b.contains("alice"));
}

TEST(OrSet, MergeTwoNodes) {
    OrSet a(1), b(2);
    a.add("x");
    a.add("y");
    b.add("y");
    b.add("z");

    auto snap_a = a.snapshot();
    auto snap_b = b.snapshot();

    a.merge(snap_b.data(), snap_b.size());
    b.merge(snap_a.data(), snap_a.size());

    // Both should have {x, y, z}
    EXPECT_EQ(a.size(), 3u);
    EXPECT_EQ(b.size(), 3u);
    EXPECT_TRUE(a.contains("x"));
    EXPECT_TRUE(a.contains("y"));
    EXPECT_TRUE(a.contains("z"));
    EXPECT_TRUE(b.contains("x"));
    EXPECT_TRUE(b.contains("y"));
    EXPECT_TRUE(b.contains("z"));
}

TEST(OrSet, SnapshotRoundTrip) {
    OrSet a(1);
    a.add("hello");
    a.add("world");

    auto snap = a.snapshot();
    OrSet b(2);
    b.merge(snap.data(), snap.size());

    EXPECT_TRUE(b.contains("hello"));
    EXPECT_TRUE(b.contains("world"));
    EXPECT_EQ(b.size(), 2u);
}

TEST(OrSet, DeltaTracking) {
    OrSet s(1);
    EXPECT_FALSE(s.has_pending_delta());
    EXPECT_TRUE(s.delta().empty());

    s.add("a");
    EXPECT_TRUE(s.has_pending_delta());
    auto d = s.delta();
    EXPECT_FALSE(d.empty());
    EXPECT_FALSE(s.has_pending_delta());

    // Delta can be applied to another set
    OrSet other(2);
    other.merge(d.data(), d.size());
    EXPECT_TRUE(other.contains("a"));
}

TEST(OrSet, DeltaWithRemove) {
    OrSet a(1), b(1);
    a.add("x");
    a.add("y");

    // Sync a → b
    auto snap = a.snapshot();
    b.merge(snap.data(), snap.size());

    // Now remove "x" from a
    a.remove("x");
    auto d = a.delta();

    // Apply delta to b — should remove "x"
    b.merge(d.data(), d.size());
    EXPECT_FALSE(b.contains("x"));
    EXPECT_TRUE(b.contains("y"));
}

TEST(OrSet, Elements) {
    OrSet s(1);
    s.add("c");
    s.add("a");
    s.add("b");

    auto elems = s.elements();
    ASSERT_EQ(elems.size(), 3u);

    // Convert to strings for easy checking
    std::vector<std::string> strs;
    for (const auto& e : elems) strs.emplace_back(e.begin(), e.end());
    std::sort(strs.begin(), strs.end());

    EXPECT_EQ(strs[0], "a");
    EXPECT_EQ(strs[1], "b");
    EXPECT_EQ(strs[2], "c");
}
