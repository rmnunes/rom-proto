#pragma once

/*
 * Codec: encode/decode complete packets (header + frames).
 *
 * PacketEncoder: build a packet by appending frames, then finalize.
 * PacketDecoder: parse a received packet into header + frame iterator.
 */

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>

#include "protocoll/wire/packet.h"
#include "protocoll/wire/frame.h"

namespace protocoll {

class PacketEncoder {
public:
    PacketEncoder();

    // Set packet header fields (call before finalize)
    void set_packet_type(PacketType type);
    void set_connection_id(uint16_t conn_id);
    void set_packet_number(uint32_t pkt_num);
    void set_timestamp(uint32_t timestamp_us);

    // Append a raw frame (type + payload bytes). Returns false if packet full.
    bool add_frame(FrameType type, const uint8_t* payload, uint16_t payload_len);

    // Convenience: append a typed frame struct that has encode().
    // T must have encode(buf, len) and WIRE_SIZE.
    template<typename T>
    bool add_typed_frame(FrameType type, const T& frame) {
        uint8_t buf[256];
        if (!frame.encode(buf, sizeof(buf))) return false;
        return add_frame(type, buf, static_cast<uint16_t>(T::WIRE_SIZE));
    }

    // Finalize: compute checksum, write header. Returns the complete packet.
    // After finalize(), the encoder is reset.
    std::vector<uint8_t> finalize();

    // Current payload size (sum of frame headers + frame payloads)
    size_t payload_size() const { return cursor_ - PACKET_HEADER_SIZE; }

    // Remaining space for frames
    size_t remaining() const { return MAX_PACKET_SIZE - cursor_; }

private:
    uint8_t buf_[MAX_PACKET_SIZE];
    size_t cursor_;
    PacketType packet_type_;
    uint16_t connection_id_;
    uint32_t packet_number_;
    uint32_t timestamp_us_;
    uint8_t flags_;
};

class PacketDecoder {
public:
    // Parse a packet buffer. Returns false if header is invalid.
    bool parse(const uint8_t* data, size_t len);

    const PacketHeader& header() const { return header_; }

    // Iterate frames. Returns false when no more frames.
    // Call reset_frame_cursor() to iterate again.
    bool next_frame(Frame& out);
    void reset_frame_cursor();

    // Verify checksum
    bool verify_checksum() const;

private:
    PacketHeader header_;
    const uint8_t* data_ = nullptr;
    size_t data_len_ = 0;
    size_t frame_cursor_ = 0; // offset within payload
};

} // namespace protocoll
