#include "protocoll/reliability/ack_tracker.h"
#include <algorithm>
#include <cstring>

namespace protocoll {

// --- SendTracker ---

void SendTracker::on_packet_sent(uint32_t pkt_num, uint32_t timestamp_us, uint16_t size) {
    sent_.push_back({pkt_num, timestamp_us, size, false, false, 0});
}

SentPacketInfo* SendTracker::find(uint32_t pkt_num) {
    for (auto& info : sent_) {
        if (info.packet_number == pkt_num) return &info;
    }
    return nullptr;
}

uint32_t SendTracker::on_ack_received(const AckFrame& ack, const AckFrame::SackRange* ranges,
                                       uint32_t current_timestamp_us) {
    uint32_t rtt_sample = 0;

    // Mark largest_acked
    if (auto* info = find(ack.largest_acked)) {
        if (!info->acked) {
            info->acked = true;
            // RTT = current_time - sent_time - ack_delay
            uint32_t raw_rtt = current_timestamp_us - info->sent_timestamp_us;
            if (raw_rtt > ack.ack_delay_us) {
                rtt_sample = raw_rtt - ack.ack_delay_us;
            } else {
                rtt_sample = raw_rtt;
            }
        }
    }

    // Mark all packets <= largest_acked as acked (cumulative)
    for (auto& info : sent_) {
        if (seq_le(info.packet_number, ack.largest_acked)) {
            info.acked = true;
        }
    }

    // Process SACK ranges
    if (ranges != nullptr) {
        for (uint8_t i = 0; i < ack.sack_range_count; i++) {
            for (auto& info : sent_) {
                if (seq_ge(info.packet_number, ranges[i].start) &&
                    seq_le(info.packet_number, ranges[i].end)) {
                    info.acked = true;
                }
            }
        }
    }

    return rtt_sample;
}

std::vector<uint32_t> SendTracker::get_retransmit_candidates(uint32_t current_timestamp_us,
                                                              uint32_t rto_us) const {
    std::vector<uint32_t> candidates;
    for (const auto& info : sent_) {
        if (!info.acked && !info.retransmitted) {
            uint32_t elapsed = current_timestamp_us - info.sent_timestamp_us;
            if (elapsed >= rto_us) {
                candidates.push_back(info.packet_number);
            }
        }
    }
    return candidates;
}

void SendTracker::gc(uint32_t oldest_unacked) {
    while (!sent_.empty() && sent_.front().acked &&
           seq_lt(sent_.front().packet_number, oldest_unacked)) {
        sent_.pop_front();
    }
}

size_t SendTracker::pending_count() const {
    size_t count = 0;
    for (const auto& info : sent_) {
        if (!info.acked) count++;
    }
    return count;
}

// --- RecvTracker ---

void RecvTracker::on_packet_received(uint32_t pkt_num) {
    if (!has_received_) {
        largest_received_ = pkt_num;
        has_received_ = true;
        std::memset(bitmap_, 0, sizeof(bitmap_));
        set_bit(pkt_num);
        return;
    }

    if (seq_gt(pkt_num, largest_received_)) {
        uint32_t shift = pkt_num - largest_received_;
        shift_bitmap(shift);
        largest_received_ = pkt_num;
        set_bit(pkt_num);
    } else {
        set_bit(pkt_num);
    }
}

bool RecvTracker::is_duplicate(uint32_t pkt_num) const {
    if (!has_received_) return false;
    return get_bit(pkt_num);
}

void RecvTracker::set_bit(uint32_t pkt_num) {
    if (!has_received_) return;
    if (seq_gt(pkt_num, largest_received_)) return; // Should not happen after shift
    uint32_t offset = largest_received_ - pkt_num;
    if (offset >= BITMAP_SIZE) return;
    bitmap_[offset / 8] |= (1u << (offset % 8));
}

bool RecvTracker::get_bit(uint32_t pkt_num) const {
    if (!has_received_) return false;
    if (seq_gt(pkt_num, largest_received_)) return false;
    uint32_t offset = largest_received_ - pkt_num;
    if (offset >= BITMAP_SIZE) return false;
    return (bitmap_[offset / 8] & (1u << (offset % 8))) != 0;
}

void RecvTracker::shift_bitmap(uint32_t shift) {
    if (shift >= BITMAP_SIZE) {
        std::memset(bitmap_, 0, sizeof(bitmap_));
        return;
    }

    // Shift the bitmap by `shift` positions
    // Each position is one bit; shift bits to higher indices
    uint32_t byte_shift = shift / 8;
    uint32_t bit_shift = shift % 8;

    if (bit_shift == 0) {
        // Simple byte-level shift
        std::memmove(bitmap_ + byte_shift, bitmap_, BITMAP_SIZE / 8 - byte_shift);
        std::memset(bitmap_, 0, byte_shift);
    } else {
        // Shift with bit carry
        for (int i = static_cast<int>(BITMAP_SIZE / 8) - 1; i >= 0; i--) {
            uint8_t val = 0;
            int src_byte = i - static_cast<int>(byte_shift);
            if (src_byte >= 0) {
                val = static_cast<uint8_t>(bitmap_[src_byte] << bit_shift);
                if (src_byte > 0) {
                    val |= bitmap_[src_byte - 1] >> (8 - bit_shift);
                }
            }
            bitmap_[i] = val;
        }
    }
}

RecvTracker::AckData RecvTracker::build_ack(uint32_t ack_delay_us) const {
    AckData data{};
    data.frame.largest_acked = largest_received_;
    data.frame.ack_delay_us = static_cast<uint16_t>(
        ack_delay_us > 0xFFFF ? 0xFFFF : ack_delay_us
    );

    if (!has_received_) {
        data.frame.sack_range_count = 0;
        return data;
    }

    // Build SACK ranges by scanning the bitmap for gaps
    // A gap = missing packets between received ranges
    bool in_range = false;
    uint32_t range_start = 0;

    // Start from bit 1 (bit 0 is always largest_received_ itself)
    for (uint32_t i = 1; i < BITMAP_SIZE && data.sack_ranges.size() < 4; i++) {
        uint32_t pkt = largest_received_ - i;
        bool received = get_bit(pkt);

        if (received && !in_range) {
            range_start = pkt;
            in_range = true;
        } else if (!received && in_range) {
            // End of range
            data.sack_ranges.push_back({pkt + 1, range_start});
            in_range = false;
        }
    }
    if (in_range && !data.sack_ranges.empty()) {
        // Close final open range
        uint32_t last_pkt = largest_received_ - (BITMAP_SIZE - 1);
        data.sack_ranges.back().start = last_pkt;
    }

    data.frame.sack_range_count = static_cast<uint8_t>(data.sack_ranges.size());
    return data;
}

} // namespace protocoll
