#pragma once

// Forward Error Correction (FEC): XOR-based parity for snapshot recovery.
//
// When sending large snapshots, the sender creates parity packets so
// the receiver can reconstruct missing packets without retransmission.
//
// Scheme: simple XOR parity over groups of K data packets.
// For every K data packets, one parity packet is generated.
// If any single packet in the group is lost, it can be reconstructed
// from the other K-1 data packets + the parity packet.
//
// This is intentionally simple (not Reed-Solomon) — a single parity
// packet per group recovers exactly one loss. For real-time state
// streaming, this covers the common case (isolated packet loss) while
// keeping CPU cost near zero.
//
// Future: upgrade to Reed-Solomon or fountain codes for burst loss.

#include <cstdint>
#include <cstddef>
#include <vector>
#include <optional>

namespace protocoll {

// FEC group: K data packets + 1 parity packet
struct FecGroup {
    uint32_t group_id;
    uint8_t  group_size;  // K (number of data packets in group)
    uint8_t  index;       // 0..K-1 for data, K for parity
};

// FEC header prepended to each packet in a group (4 bytes)
//   group_id(2) + group_size(1) + index(1)
struct FecHeader {
    uint16_t group_id;
    uint8_t  group_size;
    uint8_t  index;

    static constexpr size_t WIRE_SIZE = 4;

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
};

// FEC encoder: takes K data packets, produces 1 parity packet
class FecEncoder {
public:
    explicit FecEncoder(uint8_t group_size = 4);

    // Add a data packet. Returns parity packet when group is complete.
    // The returned parity packet includes the FEC header.
    std::optional<std::vector<uint8_t>> add_packet(const uint8_t* data, size_t len);

    // Flush: generate parity for incomplete group (if any packets added).
    std::optional<std::vector<uint8_t>> flush();

    uint8_t group_size() const { return group_size_; }
    uint16_t current_group_id() const { return group_id_; }

    void reset();

private:
    uint8_t group_size_;
    uint16_t group_id_ = 0;
    uint8_t count_ = 0;

    // Running XOR parity (grows to max packet size in group)
    std::vector<uint8_t> parity_;
    size_t max_packet_len_ = 0;

    void xor_into(const uint8_t* data, size_t len);
    std::vector<uint8_t> build_parity_packet();
};

// FEC decoder: collects packets in a group, recovers one missing packet
class FecDecoder {
public:
    explicit FecDecoder(uint8_t expected_group_size = 4);

    // Add a received packet (data or parity). header must be decoded already.
    // Returns recovered packet if a missing data packet can now be reconstructed.
    std::optional<std::vector<uint8_t>> add_packet(const FecHeader& header,
                                                     const uint8_t* data, size_t len);

    // Reset state for a new group
    void reset();

    uint8_t expected_group_size() const { return expected_group_size_; }

private:
    uint8_t expected_group_size_;

    struct ReceivedPacket {
        uint8_t index;
        std::vector<uint8_t> data;
    };

    uint16_t current_group_id_ = 0;
    bool group_started_ = false;
    std::vector<ReceivedPacket> received_;
    bool has_parity_ = false;
    std::vector<uint8_t> parity_data_;

    std::optional<std::vector<uint8_t>> try_recover();
};

} // namespace protocoll
