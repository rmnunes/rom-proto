#include "protocoll/transport/loopback_transport.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace protocoll {

// --- LoopbackTransport ---

LoopbackTransport::LoopbackTransport(std::shared_ptr<LoopbackBus> bus)
    : bus_(std::move(bus)) {}

LoopbackTransport::~LoopbackTransport() {
    close();
}

bool LoopbackTransport::bind(const Endpoint& local) {
    local_ = local;
    bound_ = true;
    return true;
}

void LoopbackTransport::close() {
    if (bound_) {
        bus_->remove_endpoint(local_);
        bound_ = false;
    }
}

int LoopbackTransport::send_to(const uint8_t* data, size_t len, const Endpoint& remote) {
    if (!bound_) return -1;
    LoopbackBus::Packet pkt;
    pkt.data.assign(data, data + len);
    pkt.from = local_;
    pkt.to = remote;
    bus_->deliver(std::move(pkt));
    return static_cast<int>(len);
}

int LoopbackTransport::recv_from(uint8_t* buf, size_t buf_len, Endpoint& from, int timeout_ms) {
    if (!bound_) return -1;
    LoopbackBus::Packet pkt;
    if (!bus_->receive(local_, pkt, timeout_ms)) return -1;
    size_t copy_len = std::min(buf_len, pkt.data.size());
    std::memcpy(buf, pkt.data.data(), copy_len);
    from = pkt.from;
    return static_cast<int>(copy_len);
}

// --- LoopbackBus ---

void LoopbackBus::deliver(Packet pkt) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string k = key(pkt.to);
    queues_[k].push(std::move(pkt));
    cv_.notify_all();
}

bool LoopbackBus::receive(const Endpoint& for_endpoint, Packet& out, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    std::string k = key(for_endpoint);

    auto pred = [&]() { return !queues_[k].empty(); };

    if (timeout_ms == 0) {
        if (!pred()) return false;
    } else if (timeout_ms < 0) {
        cv_.wait(lock, pred);
    } else {
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), pred)) {
            return false;
        }
    }

    out = std::move(queues_[k].front());
    queues_[k].pop();
    return true;
}

void LoopbackBus::remove_endpoint(const Endpoint& ep) {
    std::lock_guard<std::mutex> lock(mutex_);
    queues_.erase(key(ep));
}

} // namespace protocoll
