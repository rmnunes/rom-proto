#ifdef __linux__

#include "protocoll/transport/iouring_transport.h"
#include "protocoll/wire/frame_types.h"

#include <cstring>

namespace protocoll {

// io_uring requires liburing — implementation behind PROTOCOLL_ENABLE_IOURING
#if defined(PROTOCOLL_ENABLE_IOURING)

#include <liburing.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

enum class UringOpType : uint8_t {
    RECV,
    SEND,
};

struct UringOp {
    UringOpType type;
    uint8_t buffer[MAX_PACKET_SIZE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    struct msghdr msg;
    struct iovec iov;
    IoUringSendCallback send_cb;

    UringOp() {
        std::memset(&from_addr, 0, sizeof(from_addr));
        std::memset(&msg, 0, sizeof(msg));
        iov.iov_base = buffer;
        iov.iov_len = MAX_PACKET_SIZE;
        msg.msg_name = &from_addr;
        msg.msg_namelen = sizeof(from_addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
    }
};

struct IoUringTransport::Impl {
    struct io_uring ring;
    int sock_fd = -1;
    Endpoint local_ep;
    std::vector<std::unique_ptr<UringOp>> recv_ops;
    bool ring_initialized = false;
};

IoUringTransport::IoUringTransport(uint32_t queue_depth)
    : queue_depth_(queue_depth)
    , impl_(std::make_unique<Impl>()) {}

IoUringTransport::~IoUringTransport() {
    stop();
    close();
}

bool IoUringTransport::bind(const Endpoint& local) {
    impl_->sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (impl_->sock_fd < 0) return false;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(local.port);
    if (local.address.empty() || local.address == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, local.address.c_str(), &addr.sin_addr);
    }

    if (::bind(impl_->sock_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(impl_->sock_fd);
        impl_->sock_fd = -1;
        return false;
    }

    impl_->local_ep = local;

    if (io_uring_queue_init(queue_depth_, &impl_->ring, 0) < 0) {
        ::close(impl_->sock_fd);
        impl_->sock_fd = -1;
        return false;
    }
    impl_->ring_initialized = true;

    return true;
}

void IoUringTransport::start() {
    if (running_.exchange(true)) return;

    // Pre-allocate recv operations
    impl_->recv_ops.clear();
    for (uint32_t i = 0; i < std::min(queue_depth_, 64u); i++) {
        impl_->recv_ops.push_back(std::make_unique<UringOp>());
    }

    // Submit initial recvmsg operations
    for (auto& op : impl_->recv_ops) {
        op->type = UringOpType::RECV;
        struct io_uring_sqe* sqe = io_uring_get_sqe(&impl_->ring);
        if (sqe) {
            io_uring_prep_recvmsg(sqe, impl_->sock_fd, &op->msg, 0);
            io_uring_sqe_set_data(sqe, op.get());
        }
    }
    io_uring_submit(&impl_->ring);

    event_thread_ = std::thread(&IoUringTransport::event_loop, this);
}

void IoUringTransport::stop() {
    if (!running_.exchange(false)) return;

    // Wake up the event loop
    if (impl_->ring_initialized) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&impl_->ring);
        if (sqe) {
            io_uring_prep_nop(sqe);
            io_uring_submit(&impl_->ring);
        }
    }

    if (event_thread_.joinable()) event_thread_.join();
    impl_->recv_ops.clear();
}

void IoUringTransport::close() {
    stop();
    if (impl_->sock_fd >= 0) {
        ::close(impl_->sock_fd);
        impl_->sock_fd = -1;
    }
    if (impl_->ring_initialized) {
        io_uring_queue_exit(&impl_->ring);
        impl_->ring_initialized = false;
    }
}

void IoUringTransport::set_recv_handler(IoUringRecvCallback cb) {
    recv_handler_ = std::move(cb);
}

void IoUringTransport::async_send(const uint8_t* data, size_t len,
                                    const Endpoint& remote, IoUringSendCallback cb) {
    auto* op = new UringOp();
    op->type = UringOpType::SEND;
    op->send_cb = std::move(cb);

    size_t copy_len = len < MAX_PACKET_SIZE ? len : MAX_PACKET_SIZE;
    std::memcpy(op->buffer, data, copy_len);
    op->iov.iov_len = copy_len;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(remote.port);
    inet_pton(AF_INET, remote.address.c_str(), &addr.sin_addr);
    std::memcpy(&op->from_addr, &addr, sizeof(addr));

    op->msg.msg_name = &op->from_addr;
    op->msg.msg_namelen = sizeof(op->from_addr);

    struct io_uring_sqe* sqe = io_uring_get_sqe(&impl_->ring);
    if (sqe) {
        io_uring_prep_sendmsg(sqe, impl_->sock_fd, &op->msg, 0);
        io_uring_sqe_set_data(sqe, op);
        io_uring_submit(&impl_->ring);
    } else {
        if (op->send_cb) op->send_cb(-1, -1);
        delete op;
    }
}

int IoUringTransport::send_to(const uint8_t* data, size_t len, const Endpoint& remote) {
    if (impl_->sock_fd < 0) return -1;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(remote.port);
    inet_pton(AF_INET, remote.address.c_str(), &addr.sin_addr);

    int result = ::sendto(impl_->sock_fd, data, len, 0,
                           reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (result > 0) {
        packets_sent_.fetch_add(1);
        bytes_sent_.fetch_add(static_cast<uint64_t>(result));
    }
    return result;
}

void IoUringTransport::event_loop() {
    while (running_.load()) {
        struct io_uring_cqe* cqe;
        int ret = io_uring_wait_cqe_timeout(&impl_->ring, &cqe, nullptr);
        if (ret < 0) {
            if (!running_.load()) break;
            continue;
        }

        auto* op = static_cast<UringOp*>(io_uring_cqe_get_data(cqe));
        int result = cqe->res;
        io_uring_cqe_seen(&impl_->ring, cqe);

        if (!op) continue;

        if (op->type == UringOpType::RECV) {
            if (result > 0) {
                packets_received_.fetch_add(1);
                bytes_received_.fetch_add(static_cast<uint64_t>(result));

                if (recv_handler_) {
                    char addr_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &op->from_addr.sin_addr, addr_str, sizeof(addr_str));
                    Endpoint from;
                    from.address = addr_str;
                    from.port = ntohs(op->from_addr.sin_port);
                    recv_handler_(op->buffer, static_cast<size_t>(result), from, 0);
                }
            }

            // Resubmit recv
            if (running_.load()) {
                op->from_len = sizeof(op->from_addr);
                op->msg.msg_namelen = sizeof(op->from_addr);
                op->iov.iov_len = MAX_PACKET_SIZE;
                struct io_uring_sqe* sqe = io_uring_get_sqe(&impl_->ring);
                if (sqe) {
                    io_uring_prep_recvmsg(sqe, impl_->sock_fd, &op->msg, 0);
                    io_uring_sqe_set_data(sqe, op);
                    io_uring_submit(&impl_->ring);
                }
            }
        } else if (op->type == UringOpType::SEND) {
            if (result > 0) {
                packets_sent_.fetch_add(1);
                bytes_sent_.fetch_add(static_cast<uint64_t>(result));
            }
            if (op->send_cb) {
                op->send_cb(result > 0 ? result : -1, result > 0 ? 0 : -1);
            }
            delete op;
        }
    }
}

#else // !PROTOCOLL_ENABLE_IOURING — stub

struct IoUringTransport::Impl {};

IoUringTransport::IoUringTransport(uint32_t queue_depth)
    : queue_depth_(queue_depth)
    , impl_(std::make_unique<Impl>()) {}

IoUringTransport::~IoUringTransport() {
    stop();
}

bool IoUringTransport::bind(const Endpoint&) { return false; }
void IoUringTransport::start() {}
void IoUringTransport::stop() { running_.store(false); }
void IoUringTransport::close() {}
void IoUringTransport::set_recv_handler(IoUringRecvCallback) {}
void IoUringTransport::async_send(const uint8_t*, size_t, const Endpoint&, IoUringSendCallback cb) {
    if (cb) cb(-1, -1);
}
int IoUringTransport::send_to(const uint8_t*, size_t, const Endpoint&) { return -1; }
void IoUringTransport::event_loop() {}

#endif // PROTOCOLL_ENABLE_IOURING

} // namespace protocoll

#endif // __linux__
