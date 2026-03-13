#include "protocoll/transport/external_transport.h"
#include <algorithm>
#include <cstring>

namespace protocoll {

ExternalTransport::~ExternalTransport() {
    close();
}

bool ExternalTransport::bind(const Endpoint& local) {
    local_ = local;
    bound_ = true;
    return true;
}

void ExternalTransport::close() {
    bound_ = false;
    // Clear queues
    while (!recv_queue_.empty()) recv_queue_.pop();
    while (!send_queue_.empty()) send_queue_.pop();
}

int ExternalTransport::send_to(const uint8_t* data, size_t len, const Endpoint& remote) {
    if (!bound_ || !data || len == 0) return -1;

    Packet pkt;
    pkt.data.assign(data, data + len);
    pkt.endpoint = remote;  // destination
    send_queue_.push(std::move(pkt));
    return static_cast<int>(len);
}

int ExternalTransport::recv_from(uint8_t* buf, size_t buf_len, Endpoint& from, int timeout_ms) {
    if (!bound_) return -1;

    // In single-threaded WASM, we can only do non-blocking.
    // For native builds, timeout_ms > 0 or -1 also just checks the queue once
    // (no condition variable — callers must poll).
    (void)timeout_ms;

    if (recv_queue_.empty()) return -1;

    auto& pkt = recv_queue_.front();
    size_t copy_len = std::min(buf_len, pkt.data.size());
    std::memcpy(buf, pkt.data.data(), copy_len);
    from = pkt.endpoint;
    recv_queue_.pop();
    return static_cast<int>(copy_len);
}

void ExternalTransport::push_recv(const uint8_t* data, size_t len, const Endpoint& from) {
    Packet pkt;
    pkt.data.assign(data, data + len);
    pkt.endpoint = from;
    recv_queue_.push(std::move(pkt));
}

bool ExternalTransport::pop_send(Packet& out) {
    if (send_queue_.empty()) return false;
    out = std::move(send_queue_.front());
    send_queue_.pop();
    return true;
}

} // namespace protocoll
