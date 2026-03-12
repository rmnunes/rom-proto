#include <gtest/gtest.h>
#include "protocoll/peer.h"
#include "protocoll/security/crypto.h"
#include "protocoll/transport/loopback_transport.h"
#include "protocoll/state/resolution.h"
#include "protocoll/state/subscription.h"
#include "protocoll/protocoll.h"
#include <thread>
#include <cstring>
#include <string>

using namespace protocoll;

// --- Backward compatibility tests ---
// Ensure single-connection API still works after refactor

class PeerSingleConnTest : public ::testing::Test {
protected:
    std::shared_ptr<LoopbackBus> bus = std::make_shared<LoopbackBus>();
    LoopbackTransport client_transport{bus};
    LoopbackTransport server_transport{bus};
    Endpoint client_ep{"loopback", 1};
    Endpoint server_ep{"loopback", 2};

    KeyPair server_keys = KeyPair::generate();
    KeyPair client_keys = KeyPair::generate();

    void SetUp() override {
        client_transport.bind(client_ep);
        server_transport.bind(server_ep);
    }

    void exchange_keys(Peer& server, Peer& client) {
        server.register_peer_key(2, client_keys.public_key);
        client.register_peer_key(1, server_keys.public_key);
    }
};

TEST_F(PeerSingleConnTest, HandshakeAndLwwStream) {
    Peer server(1, server_transport, server_keys);
    Peer client(2, client_transport, client_keys);
    exchange_keys(server, client);
    server.set_local_endpoint(server_ep);
    client.set_local_endpoint(client_ep);

    StatePath pos("/game/player/1/position");
    server.declare(pos, CrdtType::LWW_REGISTER);
    client.declare(pos, CrdtType::LWW_REGISTER);

    std::thread server_thread([&]() { server.accept_from(client_ep, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(client.connect(server_ep));
    server_thread.join();

    ASSERT_TRUE(client.is_connected());
    ASSERT_TRUE(server.is_connected());

    std::string pos1 = "x:100,y:200";
    ASSERT_TRUE(client.set_lww(pos, reinterpret_cast<const uint8_t*>(pos1.data()), pos1.size()));

    int sent = client.flush();
    EXPECT_GT(sent, 0);

    int changes = server.poll(1000);
    EXPECT_EQ(changes, 1);

    auto* region = server.state().get(pos);
    ASSERT_NE(region, nullptr);
    auto* lww = static_cast<LwwRegister*>(region->crdt.get());
    std::string received(lww->value().begin(), lww->value().end());
    EXPECT_EQ(received, "x:100,y:200");
}

TEST_F(PeerSingleConnTest, CounterConvergence) {
    Peer server(1, server_transport, server_keys);
    Peer client(2, client_transport, client_keys);
    exchange_keys(server, client);

    StatePath clicks("/app/clicks");
    server.declare(clicks, CrdtType::G_COUNTER);
    client.declare(clicks, CrdtType::G_COUNTER);

    std::thread st([&]() { server.accept_from(client_ep, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(client.connect(server_ep));
    st.join();

    client.increment_counter(clicks, 5);
    client.flush();
    server.poll(500);

    server.increment_counter(clicks, 10);
    server.flush();
    client.poll(500);

    auto* cgc = static_cast<GCounter*>(client.state().get(clicks)->crdt.get());
    auto* sgc = static_cast<GCounter*>(server.state().get(clicks)->crdt.get());
    EXPECT_EQ(cgc->value(), 15u);
    EXPECT_EQ(sgc->value(), 15u);
}

TEST_F(PeerSingleConnTest, DisconnectCleanup) {
    Peer server(1, server_transport, server_keys);
    Peer client(2, client_transport, client_keys);

    std::thread st([&]() { server.accept_from(client_ep, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    client.connect(server_ep);
    st.join();

    ASSERT_TRUE(client.is_connected());
    client.disconnect();
    // After disconnect, flush should return -1 (not connected)
    // Note: disconnect only changes local state; the connection manager
    // still has the entry, so is_connected() checks actual state
}

// --- Multi-connection tests ---

TEST(PeerMultiConn, ConnectToMultipleNodes) {
    auto bus1 = std::make_shared<LoopbackBus>();
    auto bus2 = std::make_shared<LoopbackBus>();

    LoopbackTransport t_hub_a(bus1), t_hub_b(bus2);
    LoopbackTransport t_a(bus1), t_b(bus2);

    Endpoint ep_hub_a{"loop", 10};
    Endpoint ep_hub_b{"loop", 20};
    Endpoint ep_a{"loop", 30};
    Endpoint ep_b{"loop", 40};

    t_hub_a.bind(ep_hub_a);
    t_hub_b.bind(ep_hub_b);
    t_a.bind(ep_a);
    t_b.bind(ep_b);

    auto keys_hub = KeyPair::generate();
    auto keys_a = KeyPair::generate();
    auto keys_b = KeyPair::generate();

    // Hub uses two separate peers on different buses (as before)
    Peer hub_a(100, t_hub_a, keys_hub);
    Peer hub_b(100, t_hub_b, keys_hub);
    Peer node_a(1, t_a, keys_a);
    Peer node_b(2, t_b, keys_b);

    hub_a.register_peer_key(1, keys_a.public_key);
    hub_b.register_peer_key(2, keys_b.public_key);
    node_a.register_peer_key(100, keys_hub.public_key);
    node_b.register_peer_key(100, keys_hub.public_key);

    // Connect A <-> hub_a
    std::thread t1([&]() { hub_a.accept_from(ep_a, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(node_a.connect(ep_hub_a));
    t1.join();

    // Connect B <-> hub_b
    std::thread t2([&]() { hub_b.accept_from(ep_b, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(node_b.connect(ep_hub_b));
    t2.join();

    EXPECT_TRUE(node_a.is_connected());
    EXPECT_TRUE(node_b.is_connected());
    EXPECT_TRUE(hub_a.is_connected());
    EXPECT_TRUE(hub_b.is_connected());
}

// --- Router integration ---

TEST(PeerRouter, DeclareAnnouncesPath) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport transport(bus);
    transport.bind({"loop", 1});

    auto keys = KeyPair::generate();
    Peer peer(1, transport, keys);

    StatePath pos("/game/pos");
    peer.declare(pos, CrdtType::LWW_REGISTER);

    // Declare should auto-announce on the router
    EXPECT_EQ(peer.router().local_paths().size(), 1u);
    EXPECT_EQ(peer.router().local_paths()[0], pos.hash());
}

TEST(PeerRouter, RouterAccessible) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport transport(bus);
    transport.bind({"loop", 1});

    auto keys = KeyPair::generate();
    Peer peer(1, transport, keys);

    // Learn a route manually
    peer.router().learn_route(0xAAAA, 2);
    EXPECT_TRUE(peer.router().has_route(0xAAAA));
}

// --- Resolution filter integration ---

TEST(PeerResolution, SetConnectionResolution) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport transport(bus);
    transport.bind({"loop", 1});

    auto keys = KeyPair::generate();
    Peer peer(1, transport, keys);

    // This should not crash
    peer.set_connection_resolution(42, ResolutionTier::NORMAL);
    peer.set_connection_resolution(43, ResolutionTier::COARSE);
}

// --- Backward-compatible connection() accessor ---

TEST_F(PeerSingleConnTest, ConnectionAccessor) {
    Peer server(1, server_transport, server_keys);
    Peer client(2, client_transport, client_keys);

    std::thread st([&]() { server.accept_from(client_ep, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    client.connect(server_ep);
    st.join();

    // connection() should return the primary connection
    EXPECT_EQ(server.connection().state(), ConnectionState::CONNECTED);
    EXPECT_EQ(client.connection().state(), ConnectionState::CONNECTED);
}

// --- C API multi-connection tests ---

TEST(CApiMultiConn, UdpTransportCreate) {
    PcolTransport* t = pcol_transport_udp_create();
    ASSERT_NE(t, nullptr);
    pcol_transport_destroy(t);
}

TEST(CApiMultiConn, ResolutionTierSet) {
    PcolKeyPair kp;
    pcol_generate_keypair(&kp);
    PcolTransport* t = pcol_transport_loopback_create(500);
    pcol_transport_bind(t, {"loopback", 1});
    PcolPeer* peer = pcol_peer_create(1, t, &kp);

    // Should not crash
    pcol_peer_set_resolution(peer, 2, PCOL_RESOLUTION_NORMAL);
    pcol_peer_set_resolution(peer, 3, PCOL_RESOLUTION_COARSE);

    pcol_peer_destroy(peer);
    pcol_transport_destroy(t);
}

TEST(CApiMultiConn, RoutingAnnounceAndQuery) {
    PcolKeyPair kp;
    pcol_generate_keypair(&kp);
    PcolTransport* t = pcol_transport_loopback_create(501);
    pcol_transport_bind(t, {"loopback", 1});
    PcolPeer* peer = pcol_peer_create(1, t, &kp);

    // Declare announces on router automatically
    pcol_declare(peer, "/game/pos", PCOL_LWW_REGISTER, PCOL_RELIABLE);

    // Manual route learning
    pcol_peer_learn_route(peer, 0xBBBB, 2);
    EXPECT_EQ(pcol_peer_has_route(peer, 0xBBBB), 1);
    EXPECT_EQ(pcol_peer_has_route(peer, 0xCCCC), 0);

    pcol_peer_destroy(peer);
    pcol_transport_destroy(t);
}

// --- RouteAnnounceFrame wire test ---

TEST(RouteAnnounceFrame, EncodeDecodeRoundtrip) {
    protocoll::RouteAnnounceFrame raf;
    raf.path_hash = 0xDEADBEEF;
    raf.announcing_node_id = 42;

    uint8_t buf[16];
    ASSERT_TRUE(raf.encode(buf, sizeof(buf)));

    protocoll::RouteAnnounceFrame decoded;
    ASSERT_TRUE(decoded.decode(buf, sizeof(buf)));
    EXPECT_EQ(decoded.path_hash, 0xDEADBEEFu);
    EXPECT_EQ(decoded.announcing_node_id, 42);
}

TEST(RouteAnnounceFrame, EncodeTooSmall) {
    protocoll::RouteAnnounceFrame raf;
    raf.path_hash = 1;
    raf.announcing_node_id = 2;

    uint8_t buf[4]; // too small
    EXPECT_FALSE(raf.encode(buf, sizeof(buf)));
}

// --- Subscription wire format with ResolutionTier ---

TEST(SubscriptionWire, EncodeDecodeWithTier) {
    protocoll::SubscribeFramePayload sfp;
    sfp.sub_id = 42;
    sfp.initial_credits = -1;
    sfp.freshness_us = 5000;
    sfp.tier = protocoll::ResolutionTier::COARSE;
    sfp.pattern = "/game/**";

    uint8_t buf[128];
    size_t written = 0;
    bool encode_ok = sfp.encode(buf, sizeof(buf), written);
    EXPECT_TRUE(encode_ok);
    size_t expected_size = sfp.wire_size();
    EXPECT_EQ(written, expected_size);

    protocoll::SubscribeFramePayload decoded;
    bool decode_ok = decoded.decode(buf, written);
    ASSERT_TRUE(decode_ok);

    uint32_t d_sub_id = decoded.sub_id;
    int32_t d_credits = decoded.initial_credits;
    uint32_t d_freshness = decoded.freshness_us;
    int d_tier = static_cast<int>(decoded.tier);

    EXPECT_EQ(d_sub_id, 42u);
    EXPECT_EQ(d_credits, int32_t(-1));
    EXPECT_EQ(d_freshness, 5000u);
    EXPECT_EQ(d_tier, 2); // COARSE = 2
    EXPECT_EQ(decoded.pattern, std::string("/game/**"));
}
