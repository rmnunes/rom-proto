#include "protocoll/transport/udp_transport.h"

#ifdef PROTOCOLL_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <cerrno>
#endif

#include <cstring>

namespace protocoll {

// --- Platform init/cleanup ---

bool UdpTransport::platform_init() {
#ifdef PROTOCOLL_PLATFORM_WINDOWS
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        initialized = true;
    }
#endif
    return true;
}

void UdpTransport::platform_cleanup() {
#ifdef PROTOCOLL_PLATFORM_WINDOWS
    WSACleanup();
#endif
}

bool UdpTransport::set_recv_timeout(socket_t s, int timeout_ms) {
    if (timeout_ms < 0) {
        // Block forever: set timeout to 0 (no timeout)
        timeout_ms = 0;
    }
#ifdef PROTOCOLL_PLATFORM_WINDOWS
    DWORD tv = static_cast<DWORD>(timeout_ms);
    return setsockopt(static_cast<SOCKET>(s), SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&tv), sizeof(tv)) == 0;
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

// --- UdpTransport ---

UdpTransport::UdpTransport() {
    platform_init();
}

UdpTransport::~UdpTransport() {
    close();
}

bool UdpTransport::bind(const Endpoint& local) {
    if (sock_ != INVALID_SOCK) close();

#ifdef PROTOCOLL_PLATFORM_WINDOWS
    sock_ = static_cast<socket_t>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (sock_ == INVALID_SOCK) return false;

    // Allow address reuse
    int reuse = 1;
    setsockopt(static_cast<SOCKET>(sock_), SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCK) return false;

    int reuse = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(local.port);

    if (local.address.empty() || local.address == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, local.address.c_str(), &addr.sin_addr);
    }

    if (::bind(static_cast<int>(sock_), reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr)) != 0) {
        close();
        return false;
    }

    return true;
}

void UdpTransport::close() {
    if (sock_ != INVALID_SOCK) {
#ifdef PROTOCOLL_PLATFORM_WINDOWS
        closesocket(static_cast<SOCKET>(sock_));
#else
        ::close(sock_);
#endif
        sock_ = INVALID_SOCK;
    }
}

int UdpTransport::send_to(const uint8_t* data, size_t len, const Endpoint& remote) {
    if (sock_ == INVALID_SOCK) return -1;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(remote.port);
    inet_pton(AF_INET, remote.address.c_str(), &addr.sin_addr);

    int result;
#ifdef PROTOCOLL_PLATFORM_WINDOWS
    result = sendto(static_cast<SOCKET>(sock_), reinterpret_cast<const char*>(data),
                    static_cast<int>(len), 0,
                    reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
#else
    result = static_cast<int>(sendto(sock_, data, len, 0,
                                     reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)));
#endif
    return result;
}

int UdpTransport::recv_from(uint8_t* buf, size_t buf_len, Endpoint& from, int timeout_ms) {
    if (sock_ == INVALID_SOCK) return -1;

    // Handle non-blocking (timeout=0) via poll/select
    if (timeout_ms == 0) {
#ifdef PROTOCOLL_PLATFORM_WINDOWS
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(static_cast<SOCKET>(sock_), &readfds);
        struct timeval tv = {0, 0};
        int sel = select(0, &readfds, nullptr, nullptr, &tv);
        if (sel <= 0) return -1;
#else
        struct pollfd pfd = {sock_, POLLIN, 0};
        int ret = poll(&pfd, 1, 0);
        if (ret <= 0) return -1;
#endif
    } else if (timeout_ms > 0) {
        set_recv_timeout(sock_, timeout_ms);
    } else {
        // Block forever
        set_recv_timeout(sock_, -1);
    }

    struct sockaddr_in addr{};
#ifdef PROTOCOLL_PLATFORM_WINDOWS
    int addr_len = sizeof(addr);
    int result = recvfrom(static_cast<SOCKET>(sock_), reinterpret_cast<char*>(buf),
                          static_cast<int>(buf_len), 0,
                          reinterpret_cast<struct sockaddr*>(&addr), &addr_len);
#else
    socklen_t addr_len = sizeof(addr);
    int result = static_cast<int>(recvfrom(sock_, buf, buf_len, 0,
                                           reinterpret_cast<struct sockaddr*>(&addr), &addr_len));
#endif

    if (result > 0) {
        char ip_buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip_buf, sizeof(ip_buf));
        from.address = ip_buf;
        from.port = ntohs(addr.sin_port);
    }

    return result;
}

} // namespace protocoll
