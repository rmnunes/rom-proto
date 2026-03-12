#ifdef _WIN32

#include "protocoll/transport/iocp_transport.h"
#include "protocoll/wire/frame_types.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include <cstring>

namespace protocoll {

enum class IoOpType : uint8_t {
    RECV,
    SEND,
    QUIT,
};

struct IoOperation : OVERLAPPED {
    IoOpType type;
    WSABUF wsa_buf;
    uint8_t buffer[MAX_PACKET_SIZE];
    sockaddr_in from_addr;
    int from_len;
    IocpSendCallback send_cb;

    IoOperation() {
        std::memset(static_cast<OVERLAPPED*>(this), 0, sizeof(OVERLAPPED));
        wsa_buf.buf = reinterpret_cast<char*>(buffer);
        wsa_buf.len = MAX_PACKET_SIZE;
        from_len = sizeof(from_addr);
        std::memset(&from_addr, 0, sizeof(from_addr));
    }
};

struct IocpTransport::Impl {
    HANDLE iocp = INVALID_HANDLE_VALUE;
    SOCKET sock = INVALID_SOCKET;
    Endpoint local_ep;
    std::vector<std::unique_ptr<IoOperation>> recv_ops;

    void post_recv_operation(IoOperation* op) {
        std::memset(static_cast<OVERLAPPED*>(op), 0, sizeof(OVERLAPPED));
        op->wsa_buf.buf = reinterpret_cast<char*>(op->buffer);
        op->wsa_buf.len = MAX_PACKET_SIZE;
        op->from_len = sizeof(op->from_addr);
        DWORD flags = 0;
        DWORD bytes_recvd = 0;

        WSARecvFrom(sock, &op->wsa_buf, 1, &bytes_recvd, &flags,
                    reinterpret_cast<sockaddr*>(&op->from_addr), &op->from_len,
                    static_cast<OVERLAPPED*>(op), nullptr);
    }
};

IocpTransport::IocpTransport(size_t num_workers)
    : num_workers_(num_workers < 1 ? 1 : num_workers)
    , impl_(std::make_unique<Impl>()) {}

IocpTransport::~IocpTransport() {
    stop();
    close();
}

bool IocpTransport::bind(const Endpoint& local) {
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);

    impl_->sock = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP,
                              nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (impl_->sock == INVALID_SOCKET) return false;

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(local.port);

    if (local.address.empty() || local.address == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, local.address.c_str(), &addr.sin_addr);
    }

    if (::bind(impl_->sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(impl_->sock);
        impl_->sock = INVALID_SOCKET;
        return false;
    }

    impl_->local_ep = local;

    impl_->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0,
                                          static_cast<DWORD>(num_workers_));
    if (impl_->iocp == INVALID_HANDLE_VALUE) {
        closesocket(impl_->sock);
        impl_->sock = INVALID_SOCKET;
        return false;
    }

    if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(impl_->sock),
                                impl_->iocp, 0, 0) == NULL) {
        CloseHandle(impl_->iocp);
        impl_->iocp = INVALID_HANDLE_VALUE;
        closesocket(impl_->sock);
        impl_->sock = INVALID_SOCKET;
        return false;
    }

    return true;
}

void IocpTransport::start() {
    if (running_.exchange(true)) return;

    impl_->recv_ops.clear();
    for (size_t i = 0; i < num_workers_; i++) {
        impl_->recv_ops.push_back(std::make_unique<IoOperation>());
    }

    for (size_t i = 0; i < num_workers_; i++) {
        workers_.emplace_back(&IocpTransport::worker_loop, this);
    }

    for (auto& op : impl_->recv_ops) {
        op->type = IoOpType::RECV;
        impl_->post_recv_operation(op.get());
    }
}

void IocpTransport::stop() {
    if (!running_.exchange(false)) return;

    for (size_t i = 0; i < workers_.size(); i++) {
        PostQueuedCompletionStatus(impl_->iocp, 0, 0, nullptr);
    }

    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
    impl_->recv_ops.clear();
}

void IocpTransport::close() {
    stop();
    if (impl_->sock != INVALID_SOCKET) {
        closesocket(impl_->sock);
        impl_->sock = INVALID_SOCKET;
    }
    if (impl_->iocp != INVALID_HANDLE_VALUE) {
        CloseHandle(impl_->iocp);
        impl_->iocp = INVALID_HANDLE_VALUE;
    }
}

void IocpTransport::set_recv_handler(IocpRecvCallback cb) {
    recv_handler_ = std::move(cb);
}

void IocpTransport::async_send(const uint8_t* data, size_t len,
                                 const Endpoint& remote, IocpSendCallback cb) {
    auto* op = new IoOperation();
    op->type = IoOpType::SEND;
    op->send_cb = std::move(cb);

    size_t copy_len = len < MAX_PACKET_SIZE ? len : MAX_PACKET_SIZE;
    std::memcpy(op->buffer, data, copy_len);
    op->wsa_buf.len = static_cast<ULONG>(copy_len);

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(remote.port);
    inet_pton(AF_INET, remote.address.c_str(), &addr.sin_addr);

    DWORD bytes_sent = 0;
    int result = WSASendTo(impl_->sock, &op->wsa_buf, 1, &bytes_sent, 0,
                            reinterpret_cast<sockaddr*>(&addr), sizeof(addr),
                            static_cast<OVERLAPPED*>(op), nullptr);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        if (op->send_cb) op->send_cb(-1, WSAGetLastError());
        delete op;
    }
}

int IocpTransport::send_to(const uint8_t* data, size_t len, const Endpoint& remote) {
    if (impl_->sock == INVALID_SOCKET) return -1;

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(remote.port);
    inet_pton(AF_INET, remote.address.c_str(), &addr.sin_addr);

    int result = ::sendto(impl_->sock, reinterpret_cast<const char*>(data),
                           static_cast<int>(len), 0,
                           reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (result > 0) {
        packets_sent_.fetch_add(1);
        bytes_sent_.fetch_add(static_cast<uint64_t>(result));
    }
    return result;
}

void IocpTransport::post_recv() {
    // Recv operations are managed via impl_->post_recv_operation in worker_loop
}

void IocpTransport::worker_loop() {
    while (running_.load()) {
        DWORD bytes_transferred = 0;
        ULONG_PTR completion_key = 0;
        OVERLAPPED* overlapped = nullptr;

        BOOL success = GetQueuedCompletionStatus(
            impl_->iocp, &bytes_transferred, &completion_key,
            &overlapped, 100);

        if (!success && overlapped == nullptr) {
            continue; // timeout
        }

        if (overlapped == nullptr) {
            break; // quit sentinel
        }

        auto* op = static_cast<IoOperation*>(overlapped);

        if (op->type == IoOpType::RECV) {
            if (success && bytes_transferred > 0) {
                packets_received_.fetch_add(1);
                bytes_received_.fetch_add(bytes_transferred);

                if (recv_handler_) {
                    char addr_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &op->from_addr.sin_addr, addr_str, sizeof(addr_str));
                    Endpoint from;
                    from.address = addr_str;
                    from.port = ntohs(op->from_addr.sin_port);
                    recv_handler_(op->buffer, bytes_transferred, from, 0);
                }
            }

            if (running_.load()) {
                impl_->post_recv_operation(op);
            }
        } else if (op->type == IoOpType::SEND) {
            if (success && bytes_transferred > 0) {
                packets_sent_.fetch_add(1);
                bytes_sent_.fetch_add(bytes_transferred);
            }
            if (op->send_cb) {
                op->send_cb(success ? static_cast<int>(bytes_transferred) : -1,
                           success ? 0 : -1);
            }
            delete op;
        }
    }
}

} // namespace protocoll

#endif // _WIN32
