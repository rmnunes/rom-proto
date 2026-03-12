#pragma once

// IocpTransport: IOCP-based async transport for Windows.
//
// Uses I/O Completion Ports with overlapped WSARecvFrom/WSASendTo
// for native async I/O without a thread-pool polling model.
//
// Same public interface as AsyncTransport (RecvCallback/SendCallback).
// Drop-in replacement for higher throughput and lower latency.
//
// Requires Windows Vista+ (IOCP is available on all modern Windows).

#ifdef _WIN32

#include <cstdint>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>
#include <atomic>
#include <memory>

#include "protocoll/transport/transport.h"
#include "protocoll/wire/frame_types.h"

namespace protocoll {

// Reuse the same callback types as AsyncTransport
using IocpRecvCallback = std::function<void(const uint8_t* data, size_t len,
                                              const Endpoint& from, int error)>;
using IocpSendCallback = std::function<void(int bytes_sent, int error)>;

class IocpTransport {
public:
    // Create IOCP transport. num_workers = completion port worker threads.
    explicit IocpTransport(size_t num_workers = 2);
    ~IocpTransport();

    // Non-copyable
    IocpTransport(const IocpTransport&) = delete;
    IocpTransport& operator=(const IocpTransport&) = delete;

    // Bind to local endpoint (creates and binds the UDP socket)
    bool bind(const Endpoint& local);

    // Start the IOCP worker threads and post initial recv operations
    void start();

    // Stop all workers (blocks until joined)
    void stop();

    bool is_running() const { return running_.load(); }

    // Queue an async send
    void async_send(const uint8_t* data, size_t len, const Endpoint& remote,
                    IocpSendCallback cb = nullptr);

    // Set the receive handler
    void set_recv_handler(IocpRecvCallback cb);

    // Synchronous send (convenience)
    int send_to(const uint8_t* data, size_t len, const Endpoint& remote);

    // Statistics
    uint64_t packets_sent() const { return packets_sent_.load(); }
    uint64_t packets_received() const { return packets_received_.load(); }
    uint64_t bytes_sent() const { return bytes_sent_.load(); }
    uint64_t bytes_received() const { return bytes_received_.load(); }

    void close();

private:
    size_t num_workers_;
    std::atomic<bool> running_{false};

    // Opaque IOCP implementation details (avoids Windows.h in header)
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::vector<std::thread> workers_;
    IocpRecvCallback recv_handler_;

    std::atomic<uint64_t> packets_sent_{0};
    std::atomic<uint64_t> packets_received_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> bytes_received_{0};

    void worker_loop();
    void post_recv();
};

} // namespace protocoll

#endif // _WIN32
