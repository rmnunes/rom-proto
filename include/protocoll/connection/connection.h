#pragma once

/*
 * Connection: state machine for a single peer connection.
 *
 * States:
 *   IDLE -> CONNECTING -> CONNECTED -> CLOSING -> CLOSED
 *                                   -> CLOSED (timeout)
 *   (server side: IDLE -> ACCEPTING -> CONNECTED -> ...)
 *
 * Each connection tracks:
 *   - Local and remote endpoints
 *   - Connection IDs (local + remote)
 *   - Packet number sequences (send + recv)
 *   - RTT estimate
 *   - Connection epoch (for timestamp_us)
 */

#include <cstdint>
#include <chrono>

#include "protocoll/wire/frame_types.h"
#include "protocoll/transport/transport.h"

namespace protocoll {

enum class ConnectionState : uint8_t {
    IDLE,
    CONNECTING,   // Client sent CONNECT, waiting for ACCEPT
    ACCEPTING,    // Server received CONNECT, sending ACCEPT
    CONNECTED,    // Handshake complete, bidirectional streaming
    CLOSING,      // Sent CLOSE, waiting for peer CLOSE
    CLOSED,
};

struct ConnectionConfig {
    uint16_t max_frame_size = static_cast<uint16_t>(MAX_PAYLOAD_SIZE);
    uint32_t connect_timeout_ms = 5000;
    uint32_t idle_timeout_ms = 30000;
    uint32_t ping_interval_ms = 10000;
};

class Connection {
public:
    Connection();

    // --- State machine ---
    ConnectionState state() const { return state_; }

    // Client: initiate connection. Returns CONNECT packet to send.
    bool initiate(uint16_t local_conn_id, const Endpoint& remote);

    // Server: accept incoming CONNECT. Returns ACCEPT packet to send.
    bool accept(uint16_t local_conn_id, uint16_t remote_conn_id, const Endpoint& remote);

    // Process received ACCEPT (client side). Returns true if handshake complete.
    bool on_accept(uint16_t assigned_conn_id, uint32_t server_timestamp);

    // Process received CONNECT (server side, info extraction).
    // Returns true if valid CONNECT.
    bool on_connect(uint8_t peer_version, uint16_t peer_max_frame_size);

    // Initiate close. Returns CLOSE frame data.
    bool close(CloseReason reason);

    // Process received CLOSE.
    bool on_close(CloseReason reason);

    // --- Packet numbering ---
    uint32_t next_send_packet_number() { return send_pkt_num_++; }
    uint32_t last_recv_packet_number() const { return recv_pkt_num_; }
    void update_recv_packet_number(uint32_t pkt_num);

    // --- Timestamps ---
    uint32_t elapsed_us() const;

    // --- Accessors ---
    uint16_t local_conn_id() const { return local_conn_id_; }
    uint16_t remote_conn_id() const { return remote_conn_id_; }
    const Endpoint& remote_endpoint() const { return remote_; }
    uint16_t negotiated_max_frame_size() const { return negotiated_max_frame_; }

    // RTT tracking
    void update_rtt(uint32_t sample_us);
    uint32_t smoothed_rtt_us() const { return srtt_us_; }

    const ConnectionConfig& config() const { return config_; }
    void set_config(const ConnectionConfig& cfg) { config_ = cfg; }

private:
    ConnectionState state_ = ConnectionState::IDLE;
    uint16_t local_conn_id_ = 0;
    uint16_t remote_conn_id_ = 0;
    Endpoint remote_;
    ConnectionConfig config_;

    uint32_t send_pkt_num_ = 0;
    uint32_t recv_pkt_num_ = 0;

    uint16_t negotiated_max_frame_ = 0;
    uint8_t peer_version_ = 0;

    // Connection epoch for relative timestamps
    std::chrono::steady_clock::time_point epoch_;

    // Smoothed RTT (exponential moving average)
    uint32_t srtt_us_ = 0;
};

} // namespace protocoll
