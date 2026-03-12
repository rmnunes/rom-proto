#include "protocoll/connection/connection.h"
#include <algorithm>

namespace protocoll {

Connection::Connection()
    : epoch_(std::chrono::steady_clock::now()) {}

bool Connection::initiate(uint16_t local_conn_id, const Endpoint& remote) {
    if (state_ != ConnectionState::IDLE) return false;

    local_conn_id_ = local_conn_id;
    remote_ = remote;
    epoch_ = std::chrono::steady_clock::now();
    state_ = ConnectionState::CONNECTING;
    return true;
}

bool Connection::accept(uint16_t local_conn_id, uint16_t remote_conn_id, const Endpoint& remote) {
    if (state_ != ConnectionState::IDLE && state_ != ConnectionState::ACCEPTING) return false;

    local_conn_id_ = local_conn_id;
    remote_conn_id_ = remote_conn_id;
    remote_ = remote;
    epoch_ = std::chrono::steady_clock::now();
    state_ = ConnectionState::CONNECTED;
    return true;
}

bool Connection::on_accept(uint16_t assigned_conn_id, uint32_t /*server_timestamp*/) {
    if (state_ != ConnectionState::CONNECTING) return false;

    remote_conn_id_ = assigned_conn_id;
    state_ = ConnectionState::CONNECTED;
    return true;
}

bool Connection::on_connect(uint8_t peer_version, uint16_t peer_max_frame_size) {
    peer_version_ = peer_version;
    negotiated_max_frame_ = std::min(config_.max_frame_size, peer_max_frame_size);
    state_ = ConnectionState::ACCEPTING;
    return true;
}

bool Connection::close(CloseReason /*reason*/) {
    if (state_ != ConnectionState::CONNECTED) return false;
    state_ = ConnectionState::CLOSING;
    return true;
}

bool Connection::on_close(CloseReason /*reason*/) {
    state_ = ConnectionState::CLOSED;
    return true;
}

void Connection::update_recv_packet_number(uint32_t pkt_num) {
    if (pkt_num > recv_pkt_num_) {
        recv_pkt_num_ = pkt_num;
    }
}

uint32_t Connection::elapsed_us() const {
    auto now = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - epoch_).count();
    return static_cast<uint32_t>(us & 0xFFFFFFFF);
}

void Connection::update_rtt(uint32_t sample_us) {
    if (srtt_us_ == 0) {
        srtt_us_ = sample_us;
    } else {
        // EWMA: srtt = 7/8 * srtt + 1/8 * sample (same as TCP)
        srtt_us_ = (srtt_us_ * 7 + sample_us) / 8;
    }
}

} // namespace protocoll
