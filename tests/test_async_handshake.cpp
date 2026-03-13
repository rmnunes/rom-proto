#include <gtest/gtest.h>
#include "protocoll/transport/external_transport.h"
#include "protocoll/peer.h"
#include "protocoll/protocoll.h"
#include <cstring>

using namespace protocoll;

TEST(AsyncHandshake, ConnectStartSendsPacket) {
    ExternalTransport t;
    t.bind({"client", 1});

    auto keys = KeyPair::generate();
    Peer peer(1, t, keys);

    ASSERT_TRUE(peer.connect_start({"server", 2}));

    // CONNECT packet should be in the send queue
    EXPECT_GE(t.send_queue_size(), 1u);
}

TEST(AsyncHandshake, ConnectPollBeforeStartReturnsFalse) {
    ExternalTransport t;
    t.bind({"client", 1});

    auto keys = KeyPair::generate();
    Peer peer(1, t, keys);

    EXPECT_FALSE(peer.connect_poll());
}

TEST(AsyncHandshake, AcceptStartSucceeds) {
    ExternalTransport t;
    t.bind({"server", 2});

    auto keys = KeyPair::generate();
    Peer peer(2, t, keys);

    ASSERT_TRUE(peer.accept_start());
}

TEST(AsyncHandshake, AcceptPollBeforeStartReturnsFalse) {
    ExternalTransport t;
    t.bind({"server", 2});

    auto keys = KeyPair::generate();
    Peer peer(2, t, keys);

    EXPECT_FALSE(peer.accept_poll());
}

TEST(AsyncHandshake, AcceptPollNoDataReturnsFalse) {
    ExternalTransport t;
    t.bind({"server", 2});

    auto keys = KeyPair::generate();
    Peer peer(2, t, keys);

    peer.accept_start();
    EXPECT_FALSE(peer.accept_poll());
}

TEST(AsyncHandshake, FullAsyncHandshake) {
    ExternalTransport client_transport, server_transport;
    client_transport.bind({"client", 1});
    server_transport.bind({"server", 2});

    auto client_keys = KeyPair::generate();
    auto server_keys = KeyPair::generate();
    Peer client(1, client_transport, client_keys);
    Peer server(2, server_transport, server_keys);

    client.register_peer_key(2, server_keys.public_key);
    server.register_peer_key(1, client_keys.public_key);

    // Client starts connecting
    ASSERT_TRUE(client.connect_start({"server", 2}));
    EXPECT_FALSE(client.is_connected());

    // Server starts accepting
    ASSERT_TRUE(server.accept_start());
    EXPECT_FALSE(server.is_connected());

    // Shuttle CONNECT packet: client -> server
    ExternalTransport::Packet pkt;
    ASSERT_TRUE(client_transport.pop_send(pkt));
    server_transport.push_recv(pkt.data.data(), pkt.data.size(), {"client", 1});

    // Server polls — processes CONNECT, sends ACCEPT
    ASSERT_TRUE(server.accept_poll());
    EXPECT_TRUE(server.is_connected());

    // Shuttle ACCEPT packet: server -> client
    ASSERT_TRUE(server_transport.pop_send(pkt));
    client_transport.push_recv(pkt.data.data(), pkt.data.size(), {"server", 2});

    // Client polls — processes ACCEPT
    ASSERT_TRUE(client.connect_poll());
    EXPECT_TRUE(client.is_connected());
}

TEST(AsyncHandshake, ConnectPollReturnsFalseUntilAcceptArrives) {
    ExternalTransport t;
    t.bind({"client", 1});

    auto keys = KeyPair::generate();
    Peer peer(1, t, keys);

    peer.connect_start({"server", 2});

    for (int i = 0; i < 5; i++) {
        EXPECT_FALSE(peer.connect_poll());
    }
}

// --- C API tests ---

TEST(AsyncHandshakeCApi, FullRoundTrip) {
    PcolTransport* ct = pcol_transport_external_create();
    PcolTransport* st = pcol_transport_external_create();
    pcol_transport_bind(ct, {"client", 1});
    pcol_transport_bind(st, {"server", 2});

    PcolKeyPair ck, sk;
    pcol_generate_keypair(&ck);
    pcol_generate_keypair(&sk);

    PcolPeer* client = pcol_peer_create(1, ct, &ck);
    PcolPeer* server = pcol_peer_create(2, st, &sk);

    PcolPublicKey cpk, spk;
    std::memcpy(cpk.bytes, ck.public_key, 32);
    std::memcpy(spk.bytes, sk.public_key, 32);
    pcol_peer_register_key(client, 2, &spk);
    pcol_peer_register_key(server, 1, &cpk);

    // Start async handshake
    EXPECT_EQ(pcol_peer_connect_start(client, {"server", 2}), PCOL_OK);
    EXPECT_EQ(pcol_peer_accept_start(server), PCOL_OK);

    // Shuttle CONNECT: client -> server
    uint8_t buf[2048];
    size_t len;
    char addr[64];
    uint16_t port;
    ASSERT_EQ(pcol_transport_external_pop_send(ct, buf, sizeof(buf), &len, addr, sizeof(addr), &port), PCOL_OK);
    ASSERT_EQ(pcol_transport_external_push_recv(st, buf, len, "client", 1), PCOL_OK);

    // Server processes CONNECT, sends ACCEPT
    ASSERT_EQ(pcol_peer_accept_poll(server), PCOL_OK);
    EXPECT_EQ(pcol_peer_is_connected(server), 1);

    // Shuttle ACCEPT: server -> client
    ASSERT_EQ(pcol_transport_external_pop_send(st, buf, sizeof(buf), &len, addr, sizeof(addr), &port), PCOL_OK);
    ASSERT_EQ(pcol_transport_external_push_recv(ct, buf, len, "server", 2), PCOL_OK);

    // Client processes ACCEPT
    ASSERT_EQ(pcol_peer_connect_poll(client), PCOL_OK);
    EXPECT_EQ(pcol_peer_is_connected(client), 1);

    pcol_peer_destroy(client);
    pcol_peer_destroy(server);
    pcol_transport_destroy(ct);
    pcol_transport_destroy(st);
}
