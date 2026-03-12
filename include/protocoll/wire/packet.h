#pragma once

/*
 * PacketHeader: 16-byte fixed header for every ProtoCol packet.
 *
 * Offset  Size  Field
 * 0       1     version_flags    [4b version][4b flags]
 * 1       1     packet_type      DATA/CONTROL/HANDSHAKE
 * 2       2     connection_id    uint16_t big-endian
 * 4       4     packet_number    uint32_t big-endian, monotonic per connection
 * 8       4     timestamp_us     uint32_t big-endian, microseconds since epoch
 * 12      2     payload_length   uint16_t big-endian
 * 14      2     checksum         uint16_t, xxHash32 truncated
 */

#include <cstdint>
#include <cstddef>
#include <span>

#include "protocoll/wire/frame_types.h"

namespace protocoll {

struct PacketHeader {
    uint8_t  version;        // Protocol version (extracted from version_flags)
    uint8_t  flags;          // Flags (extracted from version_flags)
    PacketType packet_type;
    uint16_t connection_id;
    uint32_t packet_number;
    uint32_t timestamp_us;
    uint16_t payload_length;
    uint16_t checksum;

    // Encode header into 16-byte wire format buffer.
    // Returns true on success.
    bool encode(uint8_t* buf, size_t buf_len) const;

    // Decode 16-byte wire format buffer into this header.
    // Returns true on success, false if buffer too small or invalid.
    bool decode(const uint8_t* buf, size_t buf_len);

    // Compute checksum over header (bytes 0-13) + payload.
    // The checksum field itself (bytes 14-15) is zeroed during computation.
    static uint16_t compute_checksum(const uint8_t* packet_buf, size_t total_len);

    // Make version_flags byte from version and flags
    static uint8_t make_version_flags(uint8_t version, uint8_t flags) {
        return static_cast<uint8_t>((version << 4) | (flags & 0x0F));
    }
};

} // namespace protocoll
