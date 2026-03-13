#pragma once

/*
 * ExternalTransport: queue-based transport for bridging with external I/O.
 *
 * Designed for WASM/browser use: JavaScript pushes received packets into
 * the recv queue via push_recv(), and drains outbound packets via pop_send().
 * The C++ Peer sees a standard Transport interface.
 *
 * No mutexes — assumes single-threaded execution (WASM).
 * Also works on native builds for testing.
 */

#include "protocoll/transport/transport.h"
#include <queue>
#include <vector>

namespace protocoll {

class ExternalTransport : public Transport {
public:
    struct Packet {
        std::vector<uint8_t> data;
        Endpoint endpoint;  // `from` for recv, `to` for send
    };

    ExternalTransport() = default;
    ~ExternalTransport() override;

    // --- Transport interface ---
    bool bind(const Endpoint& local) override;
    void close() override;
    int send_to(const uint8_t* data, size_t len, const Endpoint& remote) override;
    int recv_from(uint8_t* buf, size_t buf_len, Endpoint& from, int timeout_ms = -1) override;

    // --- Bridge API (called from JavaScript via C API) ---

    // Push a received packet into the recv queue.
    void push_recv(const uint8_t* data, size_t len, const Endpoint& from);

    // Pop a sent packet from the send queue. Returns false if empty.
    bool pop_send(Packet& out);

    // Number of packets waiting in the send queue.
    size_t send_queue_size() const { return send_queue_.size(); }

    // Number of packets waiting in the recv queue.
    size_t recv_queue_size() const { return recv_queue_.size(); }

private:
    Endpoint local_;
    bool bound_ = false;
    std::queue<Packet> recv_queue_;  // JS → C++
    std::queue<Packet> send_queue_;  // C++ → JS
};

} // namespace protocoll
