#include <gtest/gtest.h>
#include "protocoll/transport/external_transport.h"
#include "protocoll/protocoll.h"
#include <cstring>

using namespace protocoll;

TEST(ExternalTransport, BindAndSendEnqueues) {
    ExternalTransport t;
    Endpoint local{"external", 1};
    ASSERT_TRUE(t.bind(local));

    uint8_t data[] = {10, 20, 30};
    Endpoint remote{"server", 8080};
    int sent = t.send_to(data, sizeof(data), remote);
    EXPECT_EQ(sent, 3);
    EXPECT_EQ(t.send_queue_size(), 1u);
}

TEST(ExternalTransport, PopSendReturnsPacket) {
    ExternalTransport t;
    t.bind({"external", 1});

    uint8_t data[] = {1, 2, 3, 4};
    Endpoint remote{"server", 443};
    t.send_to(data, sizeof(data), remote);

    ExternalTransport::Packet pkt;
    ASSERT_TRUE(t.pop_send(pkt));
    EXPECT_EQ(pkt.data.size(), 4u);
    EXPECT_EQ(pkt.data[0], 1);
    EXPECT_EQ(pkt.data[3], 4);
    EXPECT_EQ(pkt.endpoint.address, "server");
    EXPECT_EQ(pkt.endpoint.port, 443);
    EXPECT_EQ(t.send_queue_size(), 0u);
}

TEST(ExternalTransport, PopSendEmptyReturnsFalse) {
    ExternalTransport t;
    t.bind({"external", 1});

    ExternalTransport::Packet pkt;
    EXPECT_FALSE(t.pop_send(pkt));
}

TEST(ExternalTransport, PushRecvAndReceive) {
    ExternalTransport t;
    t.bind({"external", 1});

    uint8_t incoming[] = {0xAA, 0xBB, 0xCC};
    Endpoint sender{"peer", 9000};
    t.push_recv(incoming, sizeof(incoming), sender);

    uint8_t buf[64];
    Endpoint from;
    int n = t.recv_from(buf, sizeof(buf), from, 0);
    ASSERT_EQ(n, 3);
    EXPECT_EQ(buf[0], 0xAA);
    EXPECT_EQ(buf[2], 0xCC);
    EXPECT_EQ(from.address, "peer");
    EXPECT_EQ(from.port, 9000);
}

TEST(ExternalTransport, RecvFromEmptyReturnsNegative) {
    ExternalTransport t;
    t.bind({"external", 1});

    uint8_t buf[64];
    Endpoint from;
    EXPECT_EQ(t.recv_from(buf, sizeof(buf), from, 0), -1);
}

TEST(ExternalTransport, RecvFromUnboundReturnsNegative) {
    ExternalTransport t;
    // Not bound

    uint8_t buf[64];
    Endpoint from;
    EXPECT_EQ(t.recv_from(buf, sizeof(buf), from, 0), -1);
}

TEST(ExternalTransport, SendToUnboundReturnsNegative) {
    ExternalTransport t;
    uint8_t data[] = {1};
    EXPECT_EQ(t.send_to(data, sizeof(data), {"remote", 1}), -1);
}

TEST(ExternalTransport, FifoOrdering) {
    ExternalTransport t;
    t.bind({"external", 1});

    for (uint8_t i = 0; i < 5; i++) {
        t.push_recv(&i, 1, {"peer", static_cast<uint16_t>(i)});
    }
    EXPECT_EQ(t.recv_queue_size(), 5u);

    for (uint8_t i = 0; i < 5; i++) {
        uint8_t buf;
        Endpoint from;
        int n = t.recv_from(&buf, 1, from, 0);
        ASSERT_EQ(n, 1);
        EXPECT_EQ(buf, i);
        EXPECT_EQ(from.port, i);
    }
}

TEST(ExternalTransport, CloseEmptiesQueues) {
    ExternalTransport t;
    t.bind({"external", 1});

    uint8_t data[] = {1};
    t.send_to(data, 1, {"remote", 1});
    t.push_recv(data, 1, {"peer", 1});
    EXPECT_EQ(t.send_queue_size(), 1u);
    EXPECT_EQ(t.recv_queue_size(), 1u);

    t.close();
    EXPECT_EQ(t.send_queue_size(), 0u);
    EXPECT_EQ(t.recv_queue_size(), 0u);
}

TEST(ExternalTransport, RoundTripTwoPeers) {
    // Simulate two peers communicating through external transports
    // (like two browser tabs with JS bridging in between)
    ExternalTransport t1, t2;
    t1.bind({"peer1", 1});
    t2.bind({"peer2", 2});

    // Peer 1 sends to peer 2
    uint8_t msg[] = {0xDE, 0xAD};
    t1.send_to(msg, sizeof(msg), {"peer2", 2});

    // "JavaScript" drains t1's send queue and pushes into t2's recv queue
    ExternalTransport::Packet pkt;
    ASSERT_TRUE(t1.pop_send(pkt));
    t2.push_recv(pkt.data.data(), pkt.data.size(), {"peer1", 1});

    // Peer 2 receives
    uint8_t buf[64];
    Endpoint from;
    int n = t2.recv_from(buf, sizeof(buf), from, 0);
    ASSERT_EQ(n, 2);
    EXPECT_EQ(buf[0], 0xDE);
    EXPECT_EQ(buf[1], 0xAD);
    EXPECT_EQ(from.address, "peer1");
}

// --- C API tests ---

TEST(ExternalTransportCApi, CreateAndDestroy) {
    PcolTransport* t = pcol_transport_external_create();
    ASSERT_NE(t, nullptr);

    PcolEndpoint ep = {"external", 1};
    EXPECT_EQ(pcol_transport_bind(t, ep), PCOL_OK);

    pcol_transport_destroy(t);
}

TEST(ExternalTransportCApi, PushRecvAndPopSend) {
    PcolTransport* t = pcol_transport_external_create();
    PcolEndpoint ep = {"external", 1};
    pcol_transport_bind(t, ep);

    // Push a received packet
    uint8_t incoming[] = {0x01, 0x02, 0x03};
    EXPECT_EQ(pcol_transport_external_push_recv(t, incoming, 3, "sender", 9000), PCOL_OK);

    // The ExternalTransport should now have it in recv queue
    // We'd need a peer to actually recv_from, but let's test pop_send path

    // Create a peer to send through this transport
    PcolKeyPair keys;
    pcol_generate_keypair(&keys);
    PcolPeer* peer = pcol_peer_create(1, t, &keys);
    ASSERT_NE(peer, nullptr);

    pcol_peer_destroy(peer);
    pcol_transport_destroy(t);
}

TEST(ExternalTransportCApi, PopSendEmpty) {
    PcolTransport* t = pcol_transport_external_create();
    pcol_transport_bind(t, {"external", 1});

    uint8_t buf[256];
    size_t out_len = 0;
    char addr_buf[64];
    uint16_t port = 0;

    EXPECT_EQ(pcol_transport_external_pop_send(t, buf, sizeof(buf), &out_len,
                                                 addr_buf, sizeof(addr_buf), &port),
              PCOL_ERR_NOT_FOUND);

    EXPECT_EQ(pcol_transport_external_send_queue_size(t), 0u);
    pcol_transport_destroy(t);
}

TEST(ExternalTransportCApi, InvalidTransportType) {
    // Using external API on a loopback transport should fail
    PcolTransport* t = pcol_transport_loopback_create(999);

    uint8_t data[] = {1};
    EXPECT_EQ(pcol_transport_external_push_recv(t, data, 1, "x", 1), PCOL_ERR_INVALID);
    EXPECT_EQ(pcol_transport_external_send_queue_size(t), 0u);

    pcol_transport_destroy(t);
}
