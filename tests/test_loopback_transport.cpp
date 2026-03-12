#include <gtest/gtest.h>
#include "protocoll/transport/loopback_transport.h"
#include "protocoll/wire/codec.h"
#include "protocoll/connection/handshake.h"
#include <thread>
#include <memory>

using namespace protocoll;

TEST(LoopbackTransport, SendReceive) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport t1(bus), t2(bus);

    Endpoint ep1{"loopback", 1};
    Endpoint ep2{"loopback", 2};
    t1.bind(ep1);
    t2.bind(ep2);

    uint8_t data[] = {1, 2, 3, 4, 5};
    ASSERT_EQ(t1.send_to(data, sizeof(data), ep2), 5);

    uint8_t buf[64];
    Endpoint from;
    int n = t2.recv_from(buf, sizeof(buf), from, 1000);
    ASSERT_EQ(n, 5);
    EXPECT_EQ(buf[0], 1);
    EXPECT_EQ(buf[4], 5);
    EXPECT_EQ(from.address, "loopback");
    EXPECT_EQ(from.port, 1);
}

TEST(LoopbackTransport, NonBlockingEmpty) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport t1(bus);

    Endpoint ep1{"loopback", 1};
    t1.bind(ep1);

    uint8_t buf[64];
    Endpoint from;
    int n = t1.recv_from(buf, sizeof(buf), from, 0); // non-blocking
    EXPECT_EQ(n, -1); // nothing to receive
}

TEST(LoopbackTransport, TimeoutExpires) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport t1(bus);

    Endpoint ep1{"loopback", 1};
    t1.bind(ep1);

    uint8_t buf[64];
    Endpoint from;
    auto start = std::chrono::steady_clock::now();
    int n = t1.recv_from(buf, sizeof(buf), from, 50); // 50ms timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    ).count();
    EXPECT_EQ(n, -1);
    EXPECT_GE(elapsed, 40); // Should have waited ~50ms
}

TEST(LoopbackTransport, MultipleMessages) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport t1(bus), t2(bus);

    Endpoint ep1{"loopback", 1};
    Endpoint ep2{"loopback", 2};
    t1.bind(ep1);
    t2.bind(ep2);

    for (uint8_t i = 0; i < 10; i++) {
        t1.send_to(&i, 1, ep2);
    }

    for (uint8_t i = 0; i < 10; i++) {
        uint8_t buf;
        Endpoint from;
        int n = t2.recv_from(&buf, 1, from, 100);
        ASSERT_EQ(n, 1);
        EXPECT_EQ(buf, i); // FIFO ordering
    }
}

TEST(LoopbackTransport, HandshakeOverLoopback) {
    // End-to-end: full CONNECT/ACCEPT handshake over LoopbackTransport
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport client_transport(bus), server_transport(bus);

    Endpoint client_ep{"loopback", 1};
    Endpoint server_ep{"loopback", 2};
    client_transport.bind(client_ep);
    server_transport.bind(server_ep);

    // Client initiates
    Connection client_conn;
    client_conn.initiate(1, server_ep);
    auto connect_pkt = handshake::build_connect_packet(client_conn);
    client_transport.send_to(connect_pkt.data(), connect_pkt.size(), server_ep);

    // Server receives CONNECT
    uint8_t buf[MAX_PACKET_SIZE];
    Endpoint from;
    int n = server_transport.recv_from(buf, sizeof(buf), from, 1000);
    ASSERT_GT(n, 0);

    Connection server_conn;
    auto event = handshake::process_packet(server_conn, buf, static_cast<size_t>(n));
    ASSERT_EQ(event.result, handshake::HandshakeResult::CONNECT_RECEIVED);

    // Server accepts
    server_conn.accept(42, 0, from);
    auto accept_pkt = handshake::build_accept_packet(server_conn);
    server_transport.send_to(accept_pkt.data(), accept_pkt.size(), from);

    // Client receives ACCEPT
    n = client_transport.recv_from(buf, sizeof(buf), from, 1000);
    ASSERT_GT(n, 0);

    auto event2 = handshake::process_packet(client_conn, buf, static_cast<size_t>(n));
    ASSERT_EQ(event2.result, handshake::HandshakeResult::ACCEPT_RECEIVED);

    // Both sides connected
    EXPECT_EQ(client_conn.state(), ConnectionState::CONNECTED);
    EXPECT_EQ(server_conn.state(), ConnectionState::CONNECTED);
    EXPECT_EQ(client_conn.remote_conn_id(), 42);
}
