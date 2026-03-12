#include "protocoll/wire/codec.h"
#include "protocoll/util/platform.h"
#include "protocoll/util/hash.h"
#include <cstring>
#include <algorithm>

namespace protocoll {

// --- PacketEncoder ---

PacketEncoder::PacketEncoder()
    : cursor_(PACKET_HEADER_SIZE)
    , packet_type_(PacketType::DATA)
    , connection_id_(0)
    , packet_number_(0)
    , timestamp_us_(0)
    , flags_(FLAG_CHECKSUM)
{
    std::memset(buf_, 0, sizeof(buf_));
}

void PacketEncoder::set_packet_type(PacketType type) { packet_type_ = type; }
void PacketEncoder::set_connection_id(uint16_t conn_id) { connection_id_ = conn_id; }
void PacketEncoder::set_packet_number(uint32_t pkt_num) { packet_number_ = pkt_num; }
void PacketEncoder::set_timestamp(uint32_t timestamp_us) { timestamp_us_ = timestamp_us; }

bool PacketEncoder::add_frame(FrameType type, const uint8_t* payload, uint16_t payload_len) {
    size_t needed = FRAME_HEADER_SIZE + payload_len;
    if (cursor_ + needed > MAX_PACKET_SIZE) return false;

    // Write frame header
    buf_[cursor_] = static_cast<uint8_t>(type);
    write_u16(buf_ + cursor_ + 1, payload_len);
    cursor_ += FRAME_HEADER_SIZE;

    // Write frame payload
    if (payload_len > 0 && payload != nullptr) {
        std::memcpy(buf_ + cursor_, payload, payload_len);
    }
    cursor_ += payload_len;

    return true;
}

std::vector<uint8_t> PacketEncoder::finalize() {
    uint16_t payload_len = static_cast<uint16_t>(cursor_ - PACKET_HEADER_SIZE);

    PacketHeader hdr{};
    hdr.version = PROTOCOL_VERSION;
    hdr.flags = flags_;
    hdr.packet_type = packet_type_;
    hdr.connection_id = connection_id_;
    hdr.packet_number = packet_number_;
    hdr.timestamp_us = timestamp_us_;
    hdr.payload_length = payload_len;
    hdr.checksum = 0;
    hdr.encode(buf_, PACKET_HEADER_SIZE);

    // Compute and write checksum
    uint16_t cksum = PacketHeader::compute_checksum(buf_, cursor_);
    write_u16(buf_ + 14, cksum);

    std::vector<uint8_t> result(buf_, buf_ + cursor_);

    // Reset
    cursor_ = PACKET_HEADER_SIZE;
    std::memset(buf_, 0, sizeof(buf_));

    return result;
}

// --- PacketDecoder ---

bool PacketDecoder::parse(const uint8_t* data, size_t len) {
    if (len < PACKET_HEADER_SIZE) return false;
    if (!header_.decode(data, len)) return false;

    // Validate payload_length matches actual data
    size_t expected = PACKET_HEADER_SIZE + header_.payload_length;
    if (len < expected) return false;

    data_ = data;
    data_len_ = expected; // Only consider valid portion
    frame_cursor_ = 0;

    return true;
}

bool PacketDecoder::next_frame(Frame& out) {
    size_t payload_start = PACKET_HEADER_SIZE;
    size_t payload_end = payload_start + header_.payload_length;
    size_t abs_pos = payload_start + frame_cursor_;

    if (abs_pos + FRAME_HEADER_SIZE > payload_end) return false;

    FrameHeader fh;
    if (!fh.decode(data_ + abs_pos, payload_end - abs_pos)) return false;

    if (abs_pos + FRAME_HEADER_SIZE + fh.length > payload_end) return false;

    out.header = fh;
    out.payload = data_ + abs_pos + FRAME_HEADER_SIZE;
    frame_cursor_ += FRAME_HEADER_SIZE + fh.length;

    return true;
}

void PacketDecoder::reset_frame_cursor() {
    frame_cursor_ = 0;
}

bool PacketDecoder::verify_checksum() const {
    if (data_ == nullptr) return false;
    uint16_t computed = PacketHeader::compute_checksum(data_, data_len_);
    return computed == header_.checksum;
}

} // namespace protocoll
