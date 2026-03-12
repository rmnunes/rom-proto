#pragma once

// AsyncTransport: thread-pool based async wrapper over any Transport.
//
// Provides non-blocking send/recv with completion callbacks.
// Uses a fixed-size thread pool for I/O operations.
// Designed to be swappable with future io_uring/IOCP backends.
//
// Usage:
//   UdpTransport udp;
//   AsyncTransport async(udp, 2); // 2 I/O threads
//   async.bind(ep);
//   async.start();
//   async.async_recv([](auto* data, size_t len, auto& from, int err) {
//       // called on thread-pool thread
//   });
//   async.async_send(buf, len, remote, [](int bytes_sent, int err) { });
//   // ...
//   async.stop();

#include <cstdint>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

#include "protocoll/transport/transport.h"
#include "protocoll/wire/frame_types.h"

namespace protocoll {

// Completion callback for async receive
using RecvCallback = std::function<void(const uint8_t* data, size_t len,
                                         const Endpoint& from, int error)>;

// Completion callback for async send
using SendCallback = std::function<void(int bytes_sent, int error)>;

class AsyncTransport {
public:
    // Wrap an existing transport with `num_threads` I/O threads.
    AsyncTransport(Transport& inner, size_t num_threads = 2);
    ~AsyncTransport();

    // Non-copyable, non-movable
    AsyncTransport(const AsyncTransport&) = delete;
    AsyncTransport& operator=(const AsyncTransport&) = delete;

    // Bind underlying transport
    bool bind(const Endpoint& local);

    // Start the I/O thread pool. Recv loop begins automatically.
    void start();

    // Stop all I/O threads (blocks until joined).
    void stop();

    bool is_running() const { return running_.load(); }

    // Queue an async send. Callback fires on a thread-pool thread.
    void async_send(const uint8_t* data, size_t len, const Endpoint& remote,
                    SendCallback cb = nullptr);

    // Set the receive handler. Called on a thread-pool thread for each packet.
    // Must be set before start().
    void set_recv_handler(RecvCallback cb);

    // Synchronous send (convenience, blocks caller).
    int send_to(const uint8_t* data, size_t len, const Endpoint& remote);

    // Statistics
    uint64_t packets_sent() const { return packets_sent_.load(); }
    uint64_t packets_received() const { return packets_received_.load(); }
    uint64_t bytes_sent() const { return bytes_sent_.load(); }
    uint64_t bytes_received() const { return bytes_received_.load(); }

private:
    Transport& inner_;
    size_t num_threads_;
    std::atomic<bool> running_{false};

    // Thread pool
    std::vector<std::thread> threads_;

    // Send queue
    struct SendJob {
        std::vector<uint8_t> data;
        Endpoint remote;
        SendCallback callback;
    };
    std::queue<SendJob> send_queue_;
    std::mutex send_mutex_;
    std::condition_variable send_cv_;

    // Recv handler
    RecvCallback recv_handler_;

    // Stats
    std::atomic<uint64_t> packets_sent_{0};
    std::atomic<uint64_t> packets_received_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> bytes_received_{0};

    void recv_loop();
    void send_loop();
};

// Factory: create the best async transport for the current platform.
// On Linux with PROTOCOLL_ENABLE_IOURING: returns IoUringTransport.
// On Windows: returns IocpTransport.
// Otherwise: returns thread-pool AsyncTransport.
//
// The returned object is a variant wrapper — callers use the same
// RecvCallback/SendCallback interface regardless of backend.
enum class AsyncBackend {
    THREAD_POOL,   // Portable fallback
    IO_URING,      // Linux 5.1+ (requires PROTOCOLL_ENABLE_IOURING)
    IOCP,          // Windows
    BEST,          // Auto-detect best for current platform
};

// Returns the backend that create_async_transport(BEST, ...) would select.
AsyncBackend best_async_backend();

// Create an AsyncTransport using the specified backend.
// For THREAD_POOL, wraps the given transport with a thread pool.
// For IO_URING/IOCP, the inner transport's bound socket is used directly.
std::unique_ptr<AsyncTransport> create_async_transport(
    AsyncBackend backend, Transport& inner, size_t num_threads = 2);

} // namespace protocoll
