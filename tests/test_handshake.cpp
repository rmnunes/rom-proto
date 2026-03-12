#include <gtest/gtest.h>
#include "protocoll/connection/handshake.h"
#include "protocoll/connection/connection.h"
#include "protocoll/wire/frame_types.h"

using namespace protocoll;

TEST(Handshake, BuildConnectPacket) {
    Connection conn;
    Endpoint remote{"127.0.0.1", 9000};
    conn.initiate(1, remote);

    auto pkt = handshake::build_connect_packet(conn);
    ASSERT_GT(pkt.size(), PACKET_HEADER_SIZE);

    // Parse it back
    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));
    EXPECT_TRUE(dec.verify_checksum());
    EXPECT_EQ(dec.header().packet_type, PacketType::HANDSHAKE);

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_EQ(frame.header.type, FrameType::CONNECT);

    ConnectFrame cf{};
    ASSERT_TRUE(cf.decode(frame.payload, frame.header.length));
    EXPECT_EQ(cf.magic, CONNECT_MAGIC);
    EXPECT_EQ(cf.version, PROTOCOL_VERSION);
}

TEST(Handshake, BuildAcceptPacket) {
    Connection conn;
    Endpoint remote{"127.0.0.1", 8000};
    conn.on_connect(1, 1400);
    conn.accept(10, 5, remote);

    auto pkt = handshake::build_accept_packet(conn);
    ASSERT_GT(pkt.size(), PACKET_HEADER_SIZE);

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));
    EXPECT_TRUE(dec.verify_checksum());

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_EQ(frame.header.type, FrameType::ACCEPT);

    AcceptFrame af{};
    ASSERT_TRUE(af.decode(frame.payload, frame.header.length));
    EXPECT_EQ(af.assigned_conn_id, 10);
}

TEST(Handshake, FullClientServerExchange) {
    // Simulate complete handshake over buffers

    // 1. Client sends CONNECT
    Connection client;
    Endpoint server_ep{"127.0.0.1", 9000};
    client.initiate(1, server_ep);
    auto connect_pkt = handshake::build_connect_packet(client);
    EXPECT_EQ(client.state(), ConnectionState::CONNECTING);

    // 2. Server receives CONNECT
    Connection server;
    auto event = handshake::process_packet(server, connect_pkt.data(), connect_pkt.size());
    EXPECT_EQ(event.result, handshake::HandshakeResult::CONNECT_RECEIVED);
    EXPECT_EQ(event.connect.magic, CONNECT_MAGIC);
    EXPECT_EQ(server.state(), ConnectionState::ACCEPTING);

    // 3. Server sends ACCEPT
    Endpoint client_ep{"127.0.0.1", 8000};
    server.accept(42, 0, client_ep); // remote_conn_id=0 since client used conn_id=0 in packet
    auto accept_pkt = handshake::build_accept_packet(server);
    EXPECT_EQ(server.state(), ConnectionState::CONNECTED);

    // 4. Client receives ACCEPT
    auto event2 = handshake::process_packet(client, accept_pkt.data(), accept_pkt.size());
    EXPECT_EQ(event2.result, handshake::HandshakeResult::ACCEPT_RECEIVED);
    EXPECT_EQ(client.state(), ConnectionState::CONNECTED);
    EXPECT_EQ(client.remote_conn_id(), 42);
}

TEST(Handshake, CloseExchange) {
    // Set up connected pair
    Connection client;
    Endpoint remote{"127.0.0.1", 9000};
    client.initiate(1, remote);
    client.on_accept(42, 1000);
    ASSERT_EQ(client.state(), ConnectionState::CONNECTED);

    // Client sends CLOSE
    client.close(CloseReason::NORMAL);
    auto close_pkt = handshake::build_close_packet(client, CloseReason::NORMAL);

    // Server receives CLOSE
    Connection server;
    server.on_connect(1, 1400);
    Endpoint client_ep{"127.0.0.1", 8000};
    server.accept(42, 1, client_ep);

    auto event = handshake::process_packet(server, close_pkt.data(), close_pkt.size());
    EXPECT_EQ(event.result, handshake::HandshakeResult::CLOSE_RECEIVED);
    EXPECT_EQ(event.close.reason, CloseReason::NORMAL);
    EXPECT_EQ(server.state(), ConnectionState::CLOSED);
}

TEST(Handshake, PingPong) {
    Connection conn;
    Endpoint remote{"127.0.0.1", 9000};
    conn.initiate(1, remote);
    conn.on_accept(42, 1000);

    auto ping_pkt = handshake::build_ping_packet(conn, 7);

    Connection peer;
    auto event = handshake::process_packet(peer, ping_pkt.data(), ping_pkt.size());
    EXPECT_EQ(event.result, handshake::HandshakeResult::PING_RECEIVED);
    EXPECT_EQ(event.ping.ping_id, 7u);

    auto pong_pkt = handshake::build_pong_packet(peer, event.ping.ping_id, event.ping.timestamp_us);

    Connection conn2;
    auto event2 = handshake::process_packet(conn2, pong_pkt.data(), pong_pkt.size());
    EXPECT_EQ(event2.result, handshake::HandshakeResult::PONG_RECEIVED);
    EXPECT_EQ(event2.ping.ping_id, 7u);
}

TEST(Handshake, InvalidPacket) {
    Connection conn;
    uint8_t garbage[] = {0xFF, 0xFF, 0xFF, 0xFF};
    auto event = handshake::process_packet(conn, garbage, sizeof(garbage));
    EXPECT_EQ(event.result, handshake::HandshakeResult::INVALID);
}
