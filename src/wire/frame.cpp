#include "protocoll/wire/frame.h"
#include "protocoll/util/platform.h"
#include <cstring>

namespace protocoll {

// --- FrameHeader ---

bool FrameHeader::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < FRAME_HEADER_SIZE) return false;
    buf[0] = static_cast<uint8_t>(type);
    write_u16(buf + 1, length);
    return true;
}

bool FrameHeader::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < FRAME_HEADER_SIZE) return false;
    type = static_cast<FrameType>(buf[0]);
    length = read_u16(buf + 1);
    return true;
}

// --- ConnectFrame ---

bool ConnectFrame::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < WIRE_SIZE) return false;
    write_u32(buf, magic);
    buf[4] = version;
    write_u16(buf + 5, max_frame_size);
    return true;
}

bool ConnectFrame::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < WIRE_SIZE) return false;
    magic = read_u32(buf);
    version = buf[4];
    max_frame_size = read_u16(buf + 5);
    return true;
}

// --- AcceptFrame ---

bool AcceptFrame::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < WIRE_SIZE) return false;
    write_u16(buf, assigned_conn_id);
    write_u32(buf + 2, server_timestamp_us);
    return true;
}

bool AcceptFrame::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < WIRE_SIZE) return false;
    assigned_conn_id = read_u16(buf);
    server_timestamp_us = read_u32(buf + 2);
    return true;
}

// --- CloseFrame ---

bool CloseFrame::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < WIRE_SIZE) return false;
    buf[0] = static_cast<uint8_t>(reason);
    return true;
}

bool CloseFrame::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < WIRE_SIZE) return false;
    reason = static_cast<CloseReason>(buf[0]);
    return true;
}

// --- PingFrame ---

bool PingFrame::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < WIRE_SIZE) return false;
    write_u32(buf, ping_id);
    write_u32(buf + 4, timestamp_us);
    return true;
}

bool PingFrame::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < WIRE_SIZE) return false;
    ping_id = read_u32(buf);
    timestamp_us = read_u32(buf + 4);
    return true;
}

// --- AckFrame ---

bool AckFrame::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < BASE_WIRE_SIZE) return false;
    write_u32(buf, largest_acked);
    write_u16(buf + 4, ack_delay_us);
    buf[6] = sack_range_count;
    return true;
}

bool AckFrame::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < BASE_WIRE_SIZE) return false;
    largest_acked = read_u32(buf);
    ack_delay_us = read_u16(buf + 4);
    sack_range_count = buf[6];
    return true;
}

// --- StateDeclareFrame ---

bool StateDeclareFrame::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < BASE_WIRE_SIZE) return false;
    write_u32(buf, path_hash);
    buf[4] = static_cast<uint8_t>(crdt_type);
    buf[5] = static_cast<uint8_t>(reliability);
    return true;
}

bool StateDeclareFrame::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < BASE_WIRE_SIZE) return false;
    path_hash = read_u32(buf);
    crdt_type = static_cast<CrdtType>(buf[4]);
    reliability = static_cast<Reliability>(buf[5]);
    return true;
}

// --- StateDeltaFrame ---

bool StateDeltaFrame::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < BASE_WIRE_SIZE) return false;
    write_u32(buf, path_hash);
    buf[4] = static_cast<uint8_t>(crdt_type);
    buf[5] = static_cast<uint8_t>(reliability);
    write_u16(buf + 6, author_node_id);
    return true;
}

bool StateDeltaFrame::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < BASE_WIRE_SIZE) return false;
    path_hash = read_u32(buf);
    crdt_type = static_cast<CrdtType>(buf[4]);
    reliability = static_cast<Reliability>(buf[5]);
    author_node_id = read_u16(buf + 6);
    return true;
}

// --- StateSnapshotFrame ---

bool StateSnapshotFrame::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < BASE_WIRE_SIZE) return false;
    write_u32(buf, path_hash);
    buf[4] = static_cast<uint8_t>(crdt_type);
    write_u16(buf + 5, author_node_id);
    return true;
}

bool StateSnapshotFrame::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < BASE_WIRE_SIZE) return false;
    path_hash = read_u32(buf);
    crdt_type = static_cast<CrdtType>(buf[4]);
    author_node_id = read_u16(buf + 5);
    return true;
}

// --- CapabilityRevokeFrame ---

bool CapabilityRevokeFrame::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < WIRE_SIZE) return false;
    write_u32(buf, token_id);
    write_u16(buf + 4, issuer_node_id);
    return true;
}

bool CapabilityRevokeFrame::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < WIRE_SIZE) return false;
    token_id = read_u32(buf);
    issuer_node_id = read_u16(buf + 4);
    return true;
}

// --- RouteAnnounceFrame ---

bool RouteAnnounceFrame::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < WIRE_SIZE) return false;
    write_u32(buf, path_hash);
    write_u16(buf + 4, announcing_node_id);
    return true;
}

bool RouteAnnounceFrame::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < WIRE_SIZE) return false;
    path_hash = read_u32(buf);
    announcing_node_id = read_u16(buf + 4);
    return true;
}

} // namespace protocoll
