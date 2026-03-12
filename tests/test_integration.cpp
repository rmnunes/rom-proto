#include <gtest/gtest.h>
#include "protocoll/peer.h"
#include "protocoll/security/crypto.h"
#include "protocoll/transport/loopback_transport.h"
#include <string>
#include <cstring>
#include <thread>

using namespace protocoll;

class IntegrationTest : public ::testing::Test {
protected:
    std::shared_ptr<LoopbackBus> bus = std::make_shared<LoopbackBus>();
    LoopbackTransport client_transport{bus};
    LoopbackTransport server_transport{bus};
    Endpoint client_ep{"loopback", 1};
    Endpoint server_ep{"loopback", 2};

    // Every peer needs keys — security is mandatory
    KeyPair server_keys = KeyPair::generate();
    KeyPair client_keys = KeyPair::generate();

    void SetUp() override {
        client_transport.bind(client_ep);
        server_transport.bind(server_ep);
    }

    // Helper: exchange keys between two peers
    void exchange_keys(Peer& server, uint16_t server_id,
                       Peer& client, uint16_t client_id) {
        server.register_peer_key(client_id, client_keys.public_key);
        client.register_peer_key(server_id, server_keys.public_key);
    }
};

TEST_F(IntegrationTest, HandshakeAndLwwStream) {
    Peer server(1, server_transport, server_keys);
    Peer client(2, client_transport, client_keys);
    exchange_keys(server, 1, client, 2);
    server.set_local_endpoint(server_ep);
    client.set_local_endpoint(client_ep);

    // Declare same state on both sides
    StatePath player_pos("/game/player/1/position");
    server.declare(player_pos, CrdtType::LWW_REGISTER);
    client.declare(player_pos, CrdtType::LWW_REGISTER);

    // Connect (client sends CONNECT, server accepts)
    std::thread server_thread([&]() {
        server.accept_from(client_ep, 2000);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(client.connect(server_ep));
    server_thread.join();

    ASSERT_TRUE(client.is_connected());
    ASSERT_TRUE(server.is_connected());

    // Client writes state
    std::string pos1 = "x:100,y:200";
    ASSERT_TRUE(client.set_lww(player_pos,
        reinterpret_cast<const uint8_t*>(pos1.data()), pos1.size()));

    // Flush sends deltas
    int sent = client.flush();
    EXPECT_GT(sent, 0);

    // Server polls and receives
    int changes = server.poll(1000);
    EXPECT_EQ(changes, 1);

    // Verify server has the value
    auto* region = server.state().get(player_pos);
    ASSERT_NE(region, nullptr);
    auto* lww = static_cast<LwwRegister*>(region->crdt.get());
    std::string received(lww->value().begin(), lww->value().end());
    EXPECT_EQ(received, "x:100,y:200");
}

TEST_F(IntegrationTest, BidirectionalStateExchange) {
    Peer server(1, server_transport, server_keys);
    Peer client(2, client_transport, client_keys);
    exchange_keys(server, 1, client, 2);

    // Both sides declare a counter and a register
    StatePath clicks("/app/metrics/clicks");
    StatePath status("/app/server/status");
    server.declare(clicks, CrdtType::G_COUNTER);
    server.declare(status, CrdtType::LWW_REGISTER);
    client.declare(clicks, CrdtType::G_COUNTER);
    client.declare(status, CrdtType::LWW_REGISTER);

    // Connect
    std::thread server_thread([&]() { server.accept_from(client_ep, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(client.connect(server_ep));
    server_thread.join();

    // Client increments counter
    client.increment_counter(clicks, 5);
    client.flush();
    server.poll(500);

    // Server increments counter too
    server.increment_counter(clicks, 10);
    server.flush();
    client.poll(500);

    // Both should converge: client sees 5+10=15, server sees 5+10=15
    auto* client_region = client.state().get(clicks);
    auto* server_region = server.state().get(clicks);
    auto* client_gc = static_cast<GCounter*>(client_region->crdt.get());
    auto* server_gc = static_cast<GCounter*>(server_region->crdt.get());

    EXPECT_EQ(client_gc->value(), 15u);
    EXPECT_EQ(server_gc->value(), 15u);

    // Server sets status register
    std::string s = "healthy";
    server.set_lww(status, reinterpret_cast<const uint8_t*>(s.data()), s.size());
    server.flush();
    client.poll(500);

    auto* client_status = client.state().get(status);
    auto* lww = static_cast<LwwRegister*>(client_status->crdt.get());
    EXPECT_EQ(std::string(lww->value().begin(), lww->value().end()), "healthy");
}

TEST_F(IntegrationTest, MultipleRapidUpdates) {
    Peer server(1, server_transport, server_keys);
    Peer client(2, client_transport, client_keys);
    exchange_keys(server, 1, client, 2);

    StatePath pos("/game/player/1/pos");
    server.declare(pos, CrdtType::LWW_REGISTER);
    client.declare(pos, CrdtType::LWW_REGISTER);

    std::thread server_thread([&]() { server.accept_from(client_ep, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(client.connect(server_ep));
    server_thread.join();

    // Simulate rapid state updates (like 60fps position updates)
    for (int i = 0; i < 60; i++) {
        std::string frame = "frame:" + std::to_string(i);
        client.set_lww(pos, reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
        client.flush();
    }

    // Server receives all updates
    int total_changes = 0;
    for (int i = 0; i < 60; i++) {
        total_changes += server.poll(100);
    }

    // Should have received updates (may coalesce due to LWW)
    EXPECT_GT(total_changes, 0);

    // Final state should be the last frame
    auto* region = server.state().get(pos);
    auto* lww = static_cast<LwwRegister*>(region->crdt.get());
    std::string final_val(lww->value().begin(), lww->value().end());
    EXPECT_EQ(final_val, "frame:59");
}

TEST_F(IntegrationTest, GCounterThreeNodeConvergence) {
    // Simulate 3 nodes: A <-> hub <-> B
    auto bus2 = std::make_shared<LoopbackBus>();
    LoopbackTransport ta(bus), tb(bus2), hub_a(bus), hub_b(bus2);

    Endpoint epa{"loop", 10};
    Endpoint epb{"loop", 20};
    Endpoint ephub_a{"loop", 30};
    Endpoint ephub_b{"loop", 40};

    ta.bind(epa);
    tb.bind(epb);
    hub_a.bind(ephub_a);
    hub_b.bind(ephub_b);

    auto keys_a = KeyPair::generate();
    auto keys_b = KeyPair::generate();
    auto keys_hub = KeyPair::generate();

    Peer a(1, ta, keys_a);
    Peer b(2, tb, keys_b);
    Peer hub_side_a(3, hub_a, keys_hub);
    Peer hub_side_b(3, hub_b, keys_hub); // Same node_id + keys for hub

    // Exchange keys
    a.register_peer_key(3, keys_hub.public_key);
    b.register_peer_key(3, keys_hub.public_key);
    hub_side_a.register_peer_key(1, keys_a.public_key);
    hub_side_b.register_peer_key(2, keys_b.public_key);

    StatePath counter("/app/visits");
    a.declare(counter, CrdtType::G_COUNTER);
    b.declare(counter, CrdtType::G_COUNTER);
    hub_side_a.declare(counter, CrdtType::G_COUNTER);
    hub_side_b.declare(counter, CrdtType::G_COUNTER);

    // Connect A <-> hub_a
    std::thread t1([&]() { hub_side_a.accept_from(epa, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(a.connect(ephub_a));
    t1.join();

    // Connect B <-> hub_b
    std::thread t2([&]() { hub_side_b.accept_from(epb, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(b.connect(ephub_b));
    t2.join();

    // A increments by 7
    a.increment_counter(counter, 7);
    a.flush();
    hub_side_a.poll(500);

    // B increments by 3
    b.increment_counter(counter, 3);
    b.flush();
    hub_side_b.poll(500);

    // Hub merges: hub_side_a has node1=7, hub_side_b has node2=3
    // Relay hub_a's state to hub_b and vice versa by applying snapshots
    auto* ha_region = hub_side_a.state().get(counter);
    auto snap_a = ha_region->crdt->snapshot();
    hub_side_b.state().apply_delta(counter.hash(), snap_a.data(), snap_a.size());

    auto* hb_region = hub_side_b.state().get(counter);
    auto snap_b = hb_region->crdt->snapshot();
    hub_side_a.state().apply_delta(counter.hash(), snap_b.data(), snap_b.size());

    // Verify hub has converged: 7 + 3 = 10
    auto* ha_gc = static_cast<GCounter*>(ha_region->crdt.get());
    auto* hb_gc = static_cast<GCounter*>(hb_region->crdt.get());
    EXPECT_EQ(ha_gc->value(), 10u);
    EXPECT_EQ(hb_gc->value(), 10u);
}

TEST_F(IntegrationTest, UpdateCallback) {
    Peer server(1, server_transport, server_keys);
    Peer client(2, client_transport, client_keys);
    exchange_keys(server, 1, client, 2);

    StatePath status("/app/status");
    server.declare(status, CrdtType::LWW_REGISTER);
    client.declare(status, CrdtType::LWW_REGISTER);

    // Track callbacks
    int callback_count = 0;
    std::string last_callback_path;
    server.state().set_update_callback([&](const StatePath& path, const uint8_t*, size_t) {
        callback_count++;
        last_callback_path = path.str();
    });

    std::thread server_thread([&]() { server.accept_from(client_ep, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(client.connect(server_ep));
    server_thread.join();

    std::string val = "active";
    client.set_lww(status, reinterpret_cast<const uint8_t*>(val.data()), val.size());
    client.flush();
    server.poll(500);

    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(last_callback_path, "/app/status");
}

// --- Security integration tests ---

TEST_F(IntegrationTest, SignedStateStream) {
    Peer server(1, server_transport, server_keys);
    Peer client(2, client_transport, client_keys);
    exchange_keys(server, 1, client, 2);

    StatePath pos("/game/player/1/pos");
    server.declare(pos, CrdtType::LWW_REGISTER);
    client.declare(pos, CrdtType::LWW_REGISTER);

    std::thread server_thread([&]() { server.accept_from(client_ep, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(client.connect(server_ep));
    server_thread.join();

    // Client sends signed state
    std::string val = "x:50,y:75";
    client.set_lww(pos, reinterpret_cast<const uint8_t*>(val.data()), val.size());
    int sent = client.flush();
    EXPECT_GT(sent, 0);

    // Server receives and verifies
    int changes = server.poll(1000);
    EXPECT_EQ(changes, 1);

    auto* region = server.state().get(pos);
    ASSERT_NE(region, nullptr);
    auto* lww = static_cast<LwwRegister*>(region->crdt.get());
    std::string received(lww->value().begin(), lww->value().end());
    EXPECT_EQ(received, "x:50,y:75");
}

TEST_F(IntegrationTest, RejectsUnknownSigner) {
    // Server doesn't know client's key — should reject
    Peer server(1, server_transport, server_keys);
    Peer client(2, client_transport, client_keys);
    // Deliberately NOT registering client's key on server

    StatePath pos("/game/pos");
    server.declare(pos, CrdtType::LWW_REGISTER);
    client.declare(pos, CrdtType::LWW_REGISTER);

    std::thread server_thread([&]() { server.accept_from(client_ep, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(client.connect(server_ep));
    server_thread.join();

    std::string val = "malicious";
    client.set_lww(pos, reinterpret_cast<const uint8_t*>(val.data()), val.size());
    client.flush();

    int sig_failures = 0;
    server.on_signature_failure([&](uint16_t, uint32_t) { sig_failures++; });
    int changes = server.poll(1000);
    EXPECT_EQ(changes, 0);
    EXPECT_GT(sig_failures, 0);
}

TEST_F(IntegrationTest, RejectsTamperedSignature) {
    auto attacker_keys = KeyPair::generate();

    Peer server(1, server_transport, server_keys);
    Peer client(2, client_transport, attacker_keys); // Using attacker's keys
    server.register_peer_key(2, client_keys.public_key); // Server expects real client key
    // Client signs with attacker_keys, but server verifies against client_keys — mismatch

    StatePath pos("/game/pos");
    server.declare(pos, CrdtType::LWW_REGISTER);
    client.declare(pos, CrdtType::LWW_REGISTER);

    std::thread server_thread([&]() { server.accept_from(client_ep, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(client.connect(server_ep));
    server_thread.join();

    std::string val = "impersonated";
    client.set_lww(pos, reinterpret_cast<const uint8_t*>(val.data()), val.size());
    client.flush();

    int changes = server.poll(1000);
    EXPECT_EQ(changes, 0); // Signature doesn't match registered key
}
