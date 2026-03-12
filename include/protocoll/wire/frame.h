#pragma once

/*
 * Frame: 3-byte header + variable payload.
 *
 * Offset  Size  Field
 * 0       1     frame_type
 * 1       2     frame_length   uint16_t big-endian (payload bytes, excluding header)
 *
 * Followed by frame_length bytes of type-specific payload.
 */

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>

#include "protocoll/wire/frame_types.h"

namespace protocoll {

struct FrameHeader {
    FrameType type;
    uint16_t  length; // payload length (not including 3-byte frame header)

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
};

// Parsed frame: header + view into payload bytes
struct Frame {
    FrameHeader header;
    const uint8_t* payload; // non-owning pointer into packet buffer

    // Total wire size (header + payload)
    size_t wire_size() const { return FRAME_HEADER_SIZE + header.length; }
};

// --- Specific frame payload structures ---

struct ConnectFrame {
    uint32_t magic;           // CONNECT_MAGIC
    uint8_t  version;         // Protocol version
    uint16_t max_frame_size;  // Max frame payload bytes peer can handle

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    static constexpr size_t WIRE_SIZE = 7;
};

struct AcceptFrame {
    uint16_t assigned_conn_id;
    uint32_t server_timestamp_us;

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    static constexpr size_t WIRE_SIZE = 6;
};

struct CloseFrame {
    CloseReason reason;

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    static constexpr size_t WIRE_SIZE = 1;
};

struct PingFrame {
    uint32_t ping_id;
    uint32_t timestamp_us;

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    static constexpr size_t WIRE_SIZE = 8;
};

// PongFrame has identical layout to PingFrame
using PongFrame = PingFrame;

struct AckFrame {
    uint32_t largest_acked;
    uint16_t ack_delay_us;     // Microseconds since receiving the acked packet
    uint8_t  sack_range_count; // Number of SACK ranges following
    // Followed by sack_range_count * (uint32_t start, uint32_t end) pairs

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    static constexpr size_t BASE_WIRE_SIZE = 7; // Without SACK ranges

    struct SackRange {
        uint32_t start;
        uint32_t end;
    };
};

struct StateDeclareFrame {
    uint32_t    path_hash;
    CrdtType    crdt_type;
    Reliability reliability;
    // Followed by path_string (variable length, rest of frame payload)

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    static constexpr size_t BASE_WIRE_SIZE = 6;
};

// STATE_DELTA wire layout:
//   path_hash(4) + crdt_type(1) + reliability(1) + author_node_id(2) = 8 bytes base
//   [delta_payload ...]
//   [signature(64)]
//
// Every delta is signed by its author. Verification is independent of transport.
struct StateDeltaFrame {
    uint32_t    path_hash;
    CrdtType    crdt_type;
    Reliability reliability;
    uint16_t    author_node_id;

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    static constexpr size_t BASE_WIRE_SIZE = 8;
    static constexpr size_t SIGNATURE_SIZE = 64;
    // Minimum frame: base + 0 delta bytes + signature
    static constexpr size_t MIN_WIRE_SIZE = BASE_WIRE_SIZE + SIGNATURE_SIZE;
};

// STATE_SNAPSHOT wire layout:
//   path_hash(4) + crdt_type(1) + author_node_id(2) = 7 bytes base
//   [snapshot_payload ...]
//   [signature(64)]
struct StateSnapshotFrame {
    uint32_t    path_hash;
    CrdtType    crdt_type;
    uint16_t    author_node_id;

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    static constexpr size_t BASE_WIRE_SIZE = 7;
    static constexpr size_t SIGNATURE_SIZE = 64;
    static constexpr size_t MIN_WIRE_SIZE = BASE_WIRE_SIZE + SIGNATURE_SIZE;
};

// --- Capability frames ---

struct CapabilityGrantFrame {
    // Wire: full CapabilityToken bytes (variable length)
    // The token includes its own signature.
    // Decode: use CapabilityToken::decode on the frame payload directly.
    static constexpr size_t MIN_WIRE_SIZE = 83; // 19 header bytes + 64 sig
};

struct CapabilityRevokeFrame {
    uint32_t token_id;
    uint16_t issuer_node_id;

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    static constexpr size_t WIRE_SIZE = 6;
};

// ROUTE_ANNOUNCE wire layout:
//   path_hash(4) + announcing_node_id(2) = 6 bytes
struct RouteAnnounceFrame {
    uint32_t path_hash;
    uint16_t announcing_node_id;

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    static constexpr size_t WIRE_SIZE = 6;
};

} // namespace protocoll
