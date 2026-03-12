#pragma once

/*
 * Handshake: orchestrates CONNECT/ACCEPT/CLOSE exchange over a transport.
 * Builds and parses handshake packets using the codec.
 */

#include "protocoll/connection/connection.h"
#include "protocoll/wire/codec.h"
#include "protocoll/wire/frame.h"
#include <vector>

namespace protocoll {

namespace handshake {

// Build a CONNECT packet for client-initiated handshake
std::vector<uint8_t> build_connect_packet(Connection& conn);

// Build an ACCEPT packet for server response
std::vector<uint8_t> build_accept_packet(Connection& conn);

// Build a CLOSE packet
std::vector<uint8_t> build_close_packet(Connection& conn, CloseReason reason);

// Build a PING packet
std::vector<uint8_t> build_ping_packet(Connection& conn, uint32_t ping_id);

// Build a PONG packet (response to ping)
std::vector<uint8_t> build_pong_packet(Connection& conn, uint32_t ping_id, uint32_t echo_timestamp);

// Parse a received packet and process handshake frames.
// Returns the frame type of the first handshake frame found, or nullopt.
// Updates the connection state machine accordingly.
enum class HandshakeResult {
    CONNECT_RECEIVED,
    ACCEPT_RECEIVED,
    CLOSE_RECEIVED,
    PING_RECEIVED,
    PONG_RECEIVED,
    NOT_HANDSHAKE,
    INVALID,
};

struct HandshakeEvent {
    HandshakeResult result;
    // Populated fields depend on result:
    ConnectFrame connect;
    AcceptFrame accept;
    CloseFrame close;
    PingFrame ping;
};

HandshakeEvent process_packet(Connection& conn, const uint8_t* data, size_t len);

} // namespace handshake
} // namespace protocoll
