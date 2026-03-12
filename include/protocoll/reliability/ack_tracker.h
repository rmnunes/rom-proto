#pragma once

/*
 * AckTracker: tracks which packets have been acknowledged.
 *
 * Send side: records sent packets, processes incoming ACKs, determines
 *            what needs retransmission.
 * Recv side: records received packets, generates ACK frames with SACK ranges.
 */

#include <cstdint>
#include <vector>
#include <deque>
#include <chrono>

#include "protocoll/wire/frame.h"
#include "protocoll/reliability/sequence.h"

namespace protocoll {

// --- Send-side tracking ---

struct SentPacketInfo {
    uint32_t packet_number;
    uint32_t sent_timestamp_us;
    uint16_t size;         // Total packet size (for congestion control)
    bool     acked;
    bool     retransmitted;
    uint8_t  retransmit_count;
};

class SendTracker {
public:
    // Record a newly sent packet
    void on_packet_sent(uint32_t pkt_num, uint32_t timestamp_us, uint16_t size);

    // Process an incoming ACK frame. Returns RTT sample in microseconds
    // for the largest_acked packet (0 if not available).
    // Also marks SACK'd packets.
    uint32_t on_ack_received(const AckFrame& ack, const AckFrame::SackRange* ranges,
                             uint32_t current_timestamp_us);

    // Get packets that need retransmission (not acked, sent > rto_us ago)
    std::vector<uint32_t> get_retransmit_candidates(uint32_t current_timestamp_us,
                                                     uint32_t rto_us) const;

    // Remove old acked entries to bound memory
    void gc(uint32_t oldest_unacked);

    size_t pending_count() const;

private:
    std::deque<SentPacketInfo> sent_;

    SentPacketInfo* find(uint32_t pkt_num);
};

// --- Receive-side tracking ---

class RecvTracker {
public:
    // Record a received packet number
    void on_packet_received(uint32_t pkt_num);

    // Build an ACK frame for the current state.
    // Returns the AckFrame base + SACK ranges.
    struct AckData {
        AckFrame frame;
        std::vector<AckFrame::SackRange> sack_ranges;
    };

    AckData build_ack(uint32_t ack_delay_us) const;

    // Check if a packet number has already been received (duplicate detection)
    bool is_duplicate(uint32_t pkt_num) const;

    uint32_t largest_received() const { return largest_received_; }

private:
    uint32_t largest_received_ = 0;
    bool has_received_ = false;

    // Bitmap of received packets relative to largest_received_
    // bit[i] = 1 means (largest_received_ - i) was received
    // We track up to 256 packets back
    static constexpr size_t BITMAP_SIZE = 256;
    uint8_t bitmap_[BITMAP_SIZE / 8] = {};

    void set_bit(uint32_t pkt_num);
    bool get_bit(uint32_t pkt_num) const;
    void shift_bitmap(uint32_t shift);
};

} // namespace protocoll
