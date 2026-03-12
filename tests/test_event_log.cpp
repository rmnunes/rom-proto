#include <gtest/gtest.h>
#include "protocoll/state/event_log.h"

using namespace protocoll;

TEST(EventLog, AppendAndRetrieve) {
    EventLog log;
    uint8_t data[] = {1, 2, 3};
    VersionVector vv;
    vv.set(1, 1);

    uint64_t seq = log.append_delta(0xAA, CrdtType::LWW_REGISTER, vv, data, 3, 1000);
    EXPECT_EQ(seq, 1u);
    EXPECT_EQ(log.size(), 1u);

    auto entries = log.entries_for(0xAA);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0]->path_hash, 0xAAu);
    EXPECT_EQ(entries[0]->data.size(), 3u);
    EXPECT_FALSE(entries[0]->is_snapshot);
}

TEST(EventLog, EntriesSince) {
    EventLog log;

    VersionVector v1; v1.set(1, 1);
    VersionVector v2; v2.set(1, 2);
    VersionVector v3; v3.set(1, 3);

    uint8_t d1[] = {1};
    uint8_t d2[] = {2};
    uint8_t d3[] = {3};

    log.append_delta(0xAA, CrdtType::LWW_REGISTER, v1, d1, 1, 1000);
    log.append_delta(0xAA, CrdtType::LWW_REGISTER, v2, d2, 1, 2000);
    log.append_delta(0xAA, CrdtType::LWW_REGISTER, v3, d3, 1, 3000);

    // Query: "give me everything since v1"
    auto entries = log.entries_since(0xAA, v1);
    EXPECT_EQ(entries.size(), 2u); // v2 and v3 are newer

    // Query: "give me everything since v2"
    entries = log.entries_since(0xAA, v2);
    EXPECT_EQ(entries.size(), 1u); // Only v3

    // Query: "give me everything since empty" (fresh client)
    entries = log.entries_since(0xAA, VersionVector{});
    EXPECT_EQ(entries.size(), 3u); // Everything
}

TEST(EventLog, Snapshot) {
    EventLog log;
    VersionVector vv; vv.set(1, 5);
    uint8_t data[] = {0xFF};

    log.append_snapshot(0xBB, CrdtType::G_COUNTER, vv, data, 1, 5000);

    auto* snap = log.latest_snapshot(0xBB);
    ASSERT_NE(snap, nullptr);
    EXPECT_TRUE(snap->is_snapshot);
    EXPECT_EQ(snap->data[0], 0xFF);

    // No snapshot for different path
    EXPECT_EQ(log.latest_snapshot(0xCC), nullptr);
}

TEST(EventLog, Compaction) {
    EventLog log;

    VersionVector v1; v1.set(1, 1);
    VersionVector v2; v2.set(1, 2);
    VersionVector v3; v3.set(1, 3);

    uint8_t d[] = {0};
    log.append_delta(0xAA, CrdtType::LWW_REGISTER, v1, d, 1, 1000);
    log.append_delta(0xAA, CrdtType::LWW_REGISTER, v2, d, 1, 2000);
    log.append_snapshot(0xAA, CrdtType::LWW_REGISTER, v2, d, 1, 2500);
    log.append_delta(0xAA, CrdtType::LWW_REGISTER, v3, d, 1, 3000);

    // Also add entry for different path (should survive compaction)
    log.append_delta(0xBB, CrdtType::G_COUNTER, v1, d, 1, 1000);

    EXPECT_EQ(log.size(), 5u);

    // Compact 0xAA: remove entries dominated by v2
    size_t removed = log.compact(0xAA, v2);
    EXPECT_EQ(removed, 2u); // v1 and v2 deltas removed (snapshot + v3 kept)

    // 0xBB entry still present
    auto bb_entries = log.entries_for(0xBB);
    EXPECT_EQ(bb_entries.size(), 1u);
}

TEST(EventLog, SequenceNumbers) {
    EventLog log;
    VersionVector vv;
    uint8_t d[] = {0};

    uint64_t s1 = log.append_delta(0xAA, CrdtType::LWW_REGISTER, vv, d, 1, 0);
    uint64_t s2 = log.append_delta(0xAA, CrdtType::LWW_REGISTER, vv, d, 1, 0);
    uint64_t s3 = log.append_delta(0xBB, CrdtType::G_COUNTER, vv, d, 1, 0);

    EXPECT_EQ(s1, 1u);
    EXPECT_EQ(s2, 2u);
    EXPECT_EQ(s3, 3u);
    EXPECT_EQ(log.latest_sequence(), 3u);
}

TEST(EventLog, MultiPathSince) {
    EventLog log;
    VersionVector vv; vv.set(1, 1);
    uint8_t d[] = {0};

    log.append_delta(0xAA, CrdtType::LWW_REGISTER, vv, d, 1, 0);
    log.append_delta(0xBB, CrdtType::G_COUNTER, vv, d, 1, 0);

    // Query for 0xAA only
    auto entries = log.entries_since(0xAA, VersionVector{});
    EXPECT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0]->path_hash, 0xAAu);
}

TEST(EventLog, NeedsCompaction) {
    EventLog log;
    log.set_max_entries(5);
    VersionVector vv;
    uint8_t d[] = {0};

    for (int i = 0; i < 5; i++) {
        log.append_delta(0xAA, CrdtType::LWW_REGISTER, vv, d, 1, 0);
    }
    EXPECT_FALSE(log.needs_compaction());

    log.append_delta(0xAA, CrdtType::LWW_REGISTER, vv, d, 1, 0);
    EXPECT_TRUE(log.needs_compaction());
}
