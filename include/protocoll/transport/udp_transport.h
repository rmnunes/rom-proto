#pragma once

/*
 * UdpTransport: cross-platform UDP socket transport.
 * Blocking sockets first. Async (io_uring/IOCP) swappable later.
 */

#include "protocoll/transport/transport.h"
#include <cstdint>

namespace protocoll {

class UdpTransport : public Transport {
public:
    UdpTransport();
    ~UdpTransport() override;

    bool bind(const Endpoint& local) override;
    void close() override;
    int send_to(const uint8_t* data, size_t len, const Endpoint& remote) override;
    int recv_from(uint8_t* buf, size_t buf_len, Endpoint& from, int timeout_ms = -1) override;

    bool is_open() const { return sock_ != INVALID_SOCK; }

private:
#ifdef PROTOCOLL_PLATFORM_WINDOWS
    using socket_t = uintptr_t; // SOCKET is UINT_PTR on Windows
    static constexpr socket_t INVALID_SOCK = ~static_cast<socket_t>(0);
#else
    using socket_t = int;
    static constexpr socket_t INVALID_SOCK = -1;
#endif

    socket_t sock_ = INVALID_SOCK;

    static bool platform_init();
    static void platform_cleanup();
    static bool set_recv_timeout(socket_t s, int timeout_ms);
};

} // namespace protocoll
