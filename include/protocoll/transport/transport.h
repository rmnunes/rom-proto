#pragma once

/*
 * Transport: abstract interface for sending/receiving packets.
 * Implementations: LoopbackTransport (testing), UdpTransport (network).
 */

#include <cstdint>
#include <cstddef>
#include <string>

namespace protocoll {

struct Endpoint {
    std::string address;
    uint16_t port;

    bool operator==(const Endpoint& o) const {
        return address == o.address && port == o.port;
    }
};

class Transport {
public:
    virtual ~Transport() = default;

    // Bind to a local endpoint. Returns true on success.
    virtual bool bind(const Endpoint& local) = 0;

    // Close the transport.
    virtual void close() = 0;

    // Send packet to remote endpoint. Returns bytes sent, or -1 on error.
    virtual int send_to(const uint8_t* data, size_t len, const Endpoint& remote) = 0;

    // Receive packet. Returns bytes received, or -1 on error/timeout.
    // Fills in `from` with sender endpoint.
    // timeout_ms: 0 = non-blocking, -1 = block forever
    virtual int recv_from(uint8_t* buf, size_t buf_len, Endpoint& from, int timeout_ms = -1) = 0;
};

} // namespace protocoll
