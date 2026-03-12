#include "protocoll/reliability/fec.h"
#include "protocoll/util/platform.h"
#include <cstring>
#include <algorithm>

namespace protocoll {

// --- FecHeader ---

bool FecHeader::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < WIRE_SIZE) return false;
    write_u16(buf, group_id);
    buf[2] = group_size;
    buf[3] = index;
    return true;
}

bool FecHeader::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < WIRE_SIZE) return false;
    group_id = read_u16(buf);
    group_size = buf[2];
    index = buf[3];
    return true;
}

// --- FecEncoder ---

FecEncoder::FecEncoder(uint8_t group_size)
    : group_size_(group_size < 2 ? 2 : group_size) {}

void FecEncoder::xor_into(const uint8_t* data, size_t len) {
    if (len > parity_.size()) {
        parity_.resize(len, 0);
    }
    for (size_t i = 0; i < len; i++) {
        parity_[i] ^= data[i];
    }
    if (len > max_packet_len_) max_packet_len_ = len;
}

std::vector<uint8_t> FecEncoder::build_parity_packet() {
    // Build: [FecHeader][parity_data padded to max_packet_len]
    std::vector<uint8_t> pkt(FecHeader::WIRE_SIZE + max_packet_len_);

    FecHeader hdr;
    hdr.group_id = group_id_;
    hdr.group_size = group_size_;
    hdr.index = group_size_; // parity index = K
    hdr.encode(pkt.data(), pkt.size());

    std::memcpy(pkt.data() + FecHeader::WIRE_SIZE, parity_.data(), max_packet_len_);

    return pkt;
}

std::optional<std::vector<uint8_t>> FecEncoder::add_packet(const uint8_t* data, size_t len) {
    xor_into(data, len);
    count_++;

    if (count_ >= group_size_) {
        auto parity = build_parity_packet();
        // Reset for next group
        group_id_++;
        count_ = 0;
        parity_.clear();
        max_packet_len_ = 0;
        return parity;
    }

    return std::nullopt;
}

std::optional<std::vector<uint8_t>> FecEncoder::flush() {
    if (count_ == 0) return std::nullopt;

    auto parity = build_parity_packet();
    // Adjust header to reflect actual group size
    FecHeader hdr;
    hdr.group_id = group_id_;
    hdr.group_size = count_;
    hdr.index = count_; // parity
    hdr.encode(parity.data(), parity.size());

    group_id_++;
    count_ = 0;
    parity_.clear();
    max_packet_len_ = 0;
    return parity;
}

void FecEncoder::reset() {
    group_id_ = 0;
    count_ = 0;
    parity_.clear();
    max_packet_len_ = 0;
}

// --- FecDecoder ---

FecDecoder::FecDecoder(uint8_t expected_group_size)
    : expected_group_size_(expected_group_size) {}

std::optional<std::vector<uint8_t>> FecDecoder::add_packet(const FecHeader& header,
                                                             const uint8_t* data, size_t len) {
    // New group?
    if (!group_started_ || header.group_id != current_group_id_) {
        // Try recover from previous group first (shouldn't happen normally)
        reset();
        current_group_id_ = header.group_id;
        group_started_ = true;
    }

    if (header.index >= header.group_size) {
        // This is the parity packet
        has_parity_ = true;
        parity_data_.assign(data, data + len);
    } else {
        // Data packet
        received_.push_back({header.index, std::vector<uint8_t>(data, data + len)});
    }

    return try_recover();
}

std::optional<std::vector<uint8_t>> FecDecoder::try_recover() {
    uint8_t gs = expected_group_size_;
    // Use actual group_size from header if we have parity
    if (has_parity_ && !received_.empty()) {
        // group_size might differ if encoder flushed early
    }

    size_t data_count = received_.size();
    size_t total_needed = gs; // Need all K data packets

    if (data_count >= total_needed) {
        // All data packets received — no recovery needed
        return std::nullopt;
    }

    if (data_count == total_needed - 1 && has_parity_) {
        // Exactly one missing — can recover via XOR
        // Find which index is missing
        std::vector<bool> present(gs, false);
        for (const auto& rp : received_) {
            if (rp.index < gs) present[rp.index] = true;
        }

        int missing_idx = -1;
        for (uint8_t i = 0; i < gs; i++) {
            if (!present[i]) {
                missing_idx = i;
                break;
            }
        }
        if (missing_idx < 0) return std::nullopt;

        // Recover: XOR all received data packets + parity
        size_t max_len = parity_data_.size();
        for (const auto& rp : received_) {
            if (rp.data.size() > max_len) max_len = rp.data.size();
        }

        std::vector<uint8_t> recovered(max_len, 0);

        // Start with parity
        for (size_t i = 0; i < parity_data_.size(); i++) {
            recovered[i] = parity_data_[i];
        }

        // XOR each received data packet
        for (const auto& rp : received_) {
            for (size_t i = 0; i < rp.data.size(); i++) {
                recovered[i] ^= rp.data[i];
            }
        }

        return recovered;
    }

    // Can't recover yet (need more packets or parity)
    return std::nullopt;
}

void FecDecoder::reset() {
    group_started_ = false;
    received_.clear();
    has_parity_ = false;
    parity_data_.clear();
}

} // namespace protocoll
