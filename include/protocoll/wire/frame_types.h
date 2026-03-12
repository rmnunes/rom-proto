#pragma once

/*
 * Frame type codes and constants for the ProtoCol wire format.
 */

#include <cstdint>

namespace protocoll {

// Frame type codes
enum class FrameType : uint8_t {
    STATE_DECLARE   = 0x01,
    STATE_SNAPSHOT  = 0x02,
    STATE_DELTA     = 0x03,
    STATE_SUBSCRIBE = 0x04,
    STATE_UNSUBSCRIBE = 0x05,
    CREDIT          = 0x06,
    ACK             = 0x07,
    NACK            = 0x08,
    CAPABILITY_GRANT  = 0x09,
    CAPABILITY_REVOKE = 0x0A,
    CLOCK_SYNC      = 0x0B,
    PING            = 0x0C,
    PONG            = 0x0D,
    CONNECT         = 0x0E,
    ACCEPT          = 0x0F,
    CLOSE           = 0x10,
    ROUTE_ANNOUNCE  = 0x11,
};

// Packet types
enum class PacketType : uint8_t {
    DATA      = 0x01,
    CONTROL   = 0x02,
    HANDSHAKE = 0x03,
};

// Version/flags byte: [4b version][4b flags]
constexpr uint8_t PROTOCOL_VERSION = 1;

enum PacketFlags : uint8_t {
    FLAG_CHECKSUM   = 0x08,
    FLAG_COMPRESSED = 0x04,
    FLAG_ENCRYPTED  = 0x02,
    FLAG_RESERVED   = 0x01,
};

// Reliability levels per state region
enum class Reliability : uint8_t {
    UNRELIABLE = 0x00,   // Fire and forget
    BEST_EFFORT = 0x01,  // Retransmit once, then drop
    RELIABLE   = 0x02,   // Full ACK/retransmit
};

// CRDT type identifiers
enum class CrdtType : uint8_t {
    NONE        = 0x00,
    LWW_REGISTER = 0x01,
    G_COUNTER   = 0x02,
    PN_COUNTER  = 0x03,
    OR_SET      = 0x04,
    MV_REGISTER = 0x05,
    RGA         = 0x06, // Replicated Growable Array (sequences)
};

// Close reason codes
enum class CloseReason : uint8_t {
    NORMAL      = 0x00,
    TIMEOUT     = 0x01,
    PROTOCOL_ERROR = 0x02,
    AUTH_FAILURE = 0x03,
    GOING_AWAY  = 0x04,
};

// Wire format sizes
constexpr size_t PACKET_HEADER_SIZE = 16;
constexpr size_t FRAME_HEADER_SIZE  = 3;
constexpr size_t MAX_PACKET_SIZE    = 1452; // MTU 1500 - IP(20) - UDP(8) - headroom(20)
constexpr size_t MAX_PAYLOAD_SIZE   = MAX_PACKET_SIZE - PACKET_HEADER_SIZE;

// CONNECT frame magic bytes
constexpr uint32_t CONNECT_MAGIC = 0x50524F43; // "PROC" in ASCII

} // namespace protocoll
