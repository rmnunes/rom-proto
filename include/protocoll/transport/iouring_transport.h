#pragma once

// IoUringTransport: io_uring-based async transport for Linux 5.1+.
//
// Uses io_uring submission/completion queues for zero-copy async I/O.
// Single event loop thread replaces the thread-pool model.
//
// Requires liburing and Linux kernel 5.1+ with io_uring support.
// Compile with -DPROTOCOLL_ENABLE_IOURING=ON.

#ifdef __linux__

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

using IoUringRecvCallback = std::function<void(const uint8_t* data, size_t len,
                                                 const Endpoint& from, int error)>;
using IoUringSendCallback = std::function<void(int bytes_sent, int error)>;

class IoUringTransport {
public:
    explicit IoUringTransport(uint32_t queue_depth = 256);
    ~IoUringTransport();

    IoUringTransport(const IoUringTransport&) = delete;
    IoUringTransport& operator=(const IoUringTransport&) = delete;

    bool bind(const Endpoint& local);
    void start();
    void stop();
    void close();

    bool is_running() const { return running_.load(); }

    void async_send(const uint8_t* data, size_t len, const Endpoint& remote,
                    IoUringSendCallback cb = nullptr);
    void set_recv_handler(IoUringRecvCallback cb);
    int send_to(const uint8_t* data, size_t len, const Endpoint& remote);

    uint64_t packets_sent() const { return packets_sent_.load(); }
    uint64_t packets_received() const { return packets_received_.load(); }
    uint64_t bytes_sent() const { return bytes_sent_.load(); }
    uint64_t bytes_received() const { return bytes_received_.load(); }

private:
    uint32_t queue_depth_;
    std::atomic<bool> running_{false};
    std::thread event_thread_;
    IoUringRecvCallback recv_handler_;

    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::atomic<uint64_t> packets_sent_{0};
    std::atomic<uint64_t> packets_received_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> bytes_received_{0};

    void event_loop();
};

} // namespace protocoll

#endif // __linux__
