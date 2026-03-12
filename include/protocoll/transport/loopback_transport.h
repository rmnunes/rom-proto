#pragma once

/*
 * LoopbackTransport: in-process transport for testing.
 * Two LoopbackTransport instances share a packet queue via LoopbackBus.
 */

#include "protocoll/transport/transport.h"
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <memory>
#include <unordered_map>

namespace protocoll {

class LoopbackBus;

class LoopbackTransport : public Transport {
public:
    explicit LoopbackTransport(std::shared_ptr<LoopbackBus> bus);
    ~LoopbackTransport() override;

    bool bind(const Endpoint& local) override;
    void close() override;
    int send_to(const uint8_t* data, size_t len, const Endpoint& remote) override;
    int recv_from(uint8_t* buf, size_t buf_len, Endpoint& from, int timeout_ms = -1) override;

private:
    std::shared_ptr<LoopbackBus> bus_;
    Endpoint local_;
    bool bound_ = false;
};

// Shared bus that routes packets between LoopbackTransport instances
class LoopbackBus {
public:
    struct Packet {
        std::vector<uint8_t> data;
        Endpoint from;
        Endpoint to;
    };

    void deliver(Packet pkt);
    bool receive(const Endpoint& for_endpoint, Packet& out, int timeout_ms);
    void remove_endpoint(const Endpoint& ep);

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    // Per-endpoint receive queues
    std::unordered_map<std::string, std::queue<Packet>> queues_;

    static std::string key(const Endpoint& ep) {
        return ep.address + ":" + std::to_string(ep.port);
    }
};

} // namespace protocoll
