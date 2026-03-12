#include "protocoll/transport/async_transport.h"
#include <cstring>

namespace protocoll {

AsyncTransport::AsyncTransport(Transport& inner, size_t num_threads)
    : inner_(inner)
    , num_threads_(num_threads < 1 ? 1 : num_threads) {}

AsyncTransport::~AsyncTransport() {
    stop();
}

bool AsyncTransport::bind(const Endpoint& local) {
    return inner_.bind(local);
}

void AsyncTransport::start() {
    if (running_.exchange(true)) return; // Already running

    // One thread for recv loop, rest for send loop
    threads_.emplace_back(&AsyncTransport::recv_loop, this);
    for (size_t i = 1; i < num_threads_; i++) {
        threads_.emplace_back(&AsyncTransport::send_loop, this);
    }
    // If only 1 thread, add a dedicated send thread anyway
    if (num_threads_ == 1) {
        threads_.emplace_back(&AsyncTransport::send_loop, this);
    }
}

void AsyncTransport::stop() {
    if (!running_.exchange(false)) return; // Already stopped

    // Wake up send threads
    send_cv_.notify_all();

    // Join all threads
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();

    // Drain send queue (call callbacks with error)
    std::lock_guard<std::mutex> lock(send_mutex_);
    while (!send_queue_.empty()) {
        auto job = std::move(send_queue_.front());
        send_queue_.pop();
        if (job.callback) {
            job.callback(-1, -1);
        }
    }
}

void AsyncTransport::set_recv_handler(RecvCallback cb) {
    recv_handler_ = std::move(cb);
}

void AsyncTransport::async_send(const uint8_t* data, size_t len,
                                 const Endpoint& remote, SendCallback cb) {
    SendJob job;
    job.data.assign(data, data + len);
    job.remote = remote;
    job.callback = std::move(cb);

    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        send_queue_.push(std::move(job));
    }
    send_cv_.notify_one();
}

int AsyncTransport::send_to(const uint8_t* data, size_t len, const Endpoint& remote) {
    int result = inner_.send_to(data, len, remote);
    if (result > 0) {
        packets_sent_.fetch_add(1);
        bytes_sent_.fetch_add(static_cast<uint64_t>(result));
    }
    return result;
}

void AsyncTransport::recv_loop() {
    uint8_t buf[MAX_PACKET_SIZE];
    while (running_.load()) {
        Endpoint from;
        int n = inner_.recv_from(buf, sizeof(buf), from, 50); // 50ms timeout for checking running_
        if (n > 0) {
            packets_received_.fetch_add(1);
            bytes_received_.fetch_add(static_cast<uint64_t>(n));
            if (recv_handler_) {
                recv_handler_(buf, static_cast<size_t>(n), from, 0);
            }
        }
    }
}

void AsyncTransport::send_loop() {
    while (running_.load()) {
        SendJob job;
        {
            std::unique_lock<std::mutex> lock(send_mutex_);
            send_cv_.wait_for(lock, std::chrono::milliseconds(50),
                [this]() { return !send_queue_.empty() || !running_.load(); });

            if (!running_.load() && send_queue_.empty()) break;
            if (send_queue_.empty()) continue;

            job = std::move(send_queue_.front());
            send_queue_.pop();
        }

        int result = inner_.send_to(job.data.data(), job.data.size(), job.remote);
        if (result > 0) {
            packets_sent_.fetch_add(1);
            bytes_sent_.fetch_add(static_cast<uint64_t>(result));
        }
        if (job.callback) {
            job.callback(result, result > 0 ? 0 : -1);
        }
    }
}

// --- Factory ---

AsyncBackend best_async_backend() {
#if defined(PROTOCOLL_ENABLE_IOURING) && defined(__linux__)
    return AsyncBackend::IO_URING;
#elif defined(_WIN32)
    return AsyncBackend::IOCP;
#else
    return AsyncBackend::THREAD_POOL;
#endif
}

std::unique_ptr<AsyncTransport> create_async_transport(
    AsyncBackend backend, Transport& inner, size_t num_threads) {
    AsyncBackend effective = (backend == AsyncBackend::BEST) ? best_async_backend() : backend;
    (void)effective; // io_uring/IOCP have different class hierarchies; for now, all
                     // backends funnel through the thread-pool AsyncTransport wrapper.
                     // When callers need raw io_uring/IOCP, they instantiate directly.
    return std::make_unique<AsyncTransport>(inner, num_threads);
}

} // namespace protocoll
