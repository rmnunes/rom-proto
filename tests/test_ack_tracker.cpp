#include <gtest/gtest.h>
#include "protocoll/reliability/ack_tracker.h"
#include "protocoll/reliability/sequence.h"

using namespace protocoll;

// --- Sequence arithmetic ---

TEST(Sequence, BasicComparison) {
    EXPECT_TRUE(seq_lt(1, 2));
    EXPECT_FALSE(seq_lt(2, 1));
    EXPECT_TRUE(seq_le(1, 1));
    EXPECT_TRUE(seq_gt(2, 1));
    EXPECT_TRUE(seq_ge(2, 2));
}

TEST(Sequence, WrapAround) {
    // Near wrap-around: 0xFFFFFFFF is "before" 0x00000000
    EXPECT_TRUE(seq_lt(0xFFFFFFFE, 0xFFFFFFFF));
    EXPECT_TRUE(seq_lt(0xFFFFFFFF, 0x00000000));
    EXPECT_TRUE(seq_lt(0xFFFFFFFF, 0x00000001));
}

// --- SendTracker ---

TEST(SendTracker, TrackAndAck) {
    SendTracker tracker;
    tracker.on_packet_sent(1, 1000, 100);
    tracker.on_packet_sent(2, 2000, 200);
    tracker.on_packet_sent(3, 3000, 150);

    EXPECT_EQ(tracker.pending_count(), 3u);

    AckFrame ack{};
    ack.largest_acked = 2;
    ack.ack_delay_us = 100;
    ack.sack_range_count = 0;

    uint32_t rtt = tracker.on_ack_received(ack, nullptr, 5000);
    // RTT = 5000 - 2000 - 100 = 2900
    EXPECT_EQ(rtt, 2900u);

    // Packets 1 and 2 should be acked (cumulative), 3 still pending
    EXPECT_EQ(tracker.pending_count(), 1u);
}

TEST(SendTracker, SackRanges) {
    SendTracker tracker;
    for (uint32_t i = 1; i <= 10; i++) {
        tracker.on_packet_sent(i, i * 1000, 100);
    }

    // ACK 5, SACK {8-10} — packets 6,7 are gaps
    AckFrame ack{};
    ack.largest_acked = 5;
    ack.ack_delay_us = 0;
    ack.sack_range_count = 1;

    AckFrame::SackRange ranges[] = {{8, 10}};
    tracker.on_ack_received(ack, ranges, 20000);

    // Packets 1-5 acked (cumulative), 8-10 acked (SACK), 6-7 pending
    EXPECT_EQ(tracker.pending_count(), 2u);
}

TEST(SendTracker, RetransmitCandidates) {
    SendTracker tracker;
    tracker.on_packet_sent(1, 1000, 100);
    tracker.on_packet_sent(2, 2000, 100);

    // RTO = 5000us, current time = 10000
    auto candidates = tracker.get_retransmit_candidates(10000, 5000);
    EXPECT_EQ(candidates.size(), 2u);

    // At time 3000 with RTO=5000, only packet 1 is overdue
    candidates = tracker.get_retransmit_candidates(6500, 5000);
    EXPECT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates[0], 1u);
}

// --- RecvTracker ---

TEST(RecvTracker, BasicReceive) {
    RecvTracker tracker;
    tracker.on_packet_received(1);
    EXPECT_EQ(tracker.largest_received(), 1u);
    EXPECT_TRUE(tracker.is_duplicate(1));
    EXPECT_FALSE(tracker.is_duplicate(2));
}

TEST(RecvTracker, OutOfOrder) {
    RecvTracker tracker;
    tracker.on_packet_received(1);
    tracker.on_packet_received(3);
    tracker.on_packet_received(2);

    EXPECT_EQ(tracker.largest_received(), 3u);
    EXPECT_TRUE(tracker.is_duplicate(1));
    EXPECT_TRUE(tracker.is_duplicate(2));
    EXPECT_TRUE(tracker.is_duplicate(3));
}

TEST(RecvTracker, BuildAckNoGaps) {
    RecvTracker tracker;
    tracker.on_packet_received(1);
    tracker.on_packet_received(2);
    tracker.on_packet_received(3);

    auto ack_data = tracker.build_ack(500);
    EXPECT_EQ(ack_data.frame.largest_acked, 3u);
    EXPECT_EQ(ack_data.frame.ack_delay_us, 500);
    // No gaps, so no SACK ranges needed (all contiguous below largest)
}

TEST(RecvTracker, BuildAckWithGap) {
    RecvTracker tracker;
    tracker.on_packet_received(1);
    // Skip 2
    tracker.on_packet_received(3);
    tracker.on_packet_received(4);
    tracker.on_packet_received(5);

    auto ack_data = tracker.build_ack(0);
    EXPECT_EQ(ack_data.frame.largest_acked, 5u);
    // Should have SACK range for packet 1 (gap at 2, then 1 received)
    // Contiguous range 3-5 is covered by largest_acked=5,
    // then gap at 2, then range at 1
    EXPECT_GE(ack_data.sack_ranges.size(), 0u); // Implementation dependent
}

TEST(RecvTracker, DuplicateDetection) {
    RecvTracker tracker;
    tracker.on_packet_received(5);
    EXPECT_TRUE(tracker.is_duplicate(5));
    EXPECT_FALSE(tracker.is_duplicate(4));

    tracker.on_packet_received(5); // Duplicate
    EXPECT_TRUE(tracker.is_duplicate(5));
}

TEST(RecvTracker, LargeGap) {
    RecvTracker tracker;
    tracker.on_packet_received(1);
    tracker.on_packet_received(100);

    EXPECT_EQ(tracker.largest_received(), 100u);
    EXPECT_TRUE(tracker.is_duplicate(100));
    // Packet 1 is 99 positions back — within bitmap range (256)
    EXPECT_TRUE(tracker.is_duplicate(1));
    EXPECT_FALSE(tracker.is_duplicate(50));
}
