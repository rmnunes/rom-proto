#include <gtest/gtest.h>
#include "protocoll/peer.h"
#include "protocoll/security/crypto.h"
#include "protocoll/transport/loopback_transport.h"
#include "protocoll/state/resolution.h"
#include "protocoll/protocoll.h"
#include <thread>
#include <cstring>
#include <string>

using namespace protocoll;

// ============================================================
// Mesh topology integration tests: multi-hop state propagation
// ============================================================

// Helper: connect two peers via connect_to / accept_node on separate threads
static bool connect_peers(Peer& initiator, uint16_t remote_id, const Endpoint& remote_ep,
                          Peer& acceptor, uint16_t initiator_id, const Endpoint& initiator_ep,
                          int timeout_ms = 2000) {
    bool accepted = false;
    std::thread accept_thread([&]() {
        accepted = acceptor.accept_node(initiator_id, initiator_ep, timeout_ms);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    bool connected = initiator.connect_to(remote_id, remote_ep);
    accept_thread.join();
    return connected && accepted;
}

// --- Three-node chain: A <-> Hub <-> B ---
// Data written on A should reach B via Hub forwarding.

class MeshThreeNodeChain : public ::testing::Test {
protected:
    // Two buses: bus_ab connects A and Hub-side-A, bus_hb connects Hub-side-B and B
    std::shared_ptr<LoopbackBus> bus_ah = std::make_shared<LoopbackBus>();
    std::shared_ptr<LoopbackBus> bus_hb = std::make_shared<LoopbackBus>();

    LoopbackTransport t_a{bus_ah};
    LoopbackTransport t_hub_a{bus_ah};
    LoopbackTransport t_hub_b{bus_hb};
    LoopbackTransport t_b{bus_hb};

    Endpoint ep_a{"loop", 1};
    Endpoint ep_hub_a{"loop", 2};
    Endpoint ep_hub_b{"loop", 3};
    Endpoint ep_b{"loop", 4};

    KeyPair keys_a = KeyPair::generate();
    KeyPair keys_hub = KeyPair::generate();
    KeyPair keys_b = KeyPair::generate();

    void SetUp() override {
        t_a.bind(ep_a);
        t_hub_a.bind(ep_hub_a);
        t_hub_b.bind(ep_hub_b);
        t_b.bind(ep_b);
    }
};

TEST_F(MeshThreeNodeChain, ThreeNodeLwwPropagation) {
    // Node A (id=1), Hub-A (id=100 on bus_ah), Hub-B (id=100 on bus_hb), Node B (id=2)
    Peer node_a(1, t_a, keys_a);
    Peer hub_a(100, t_hub_a, keys_hub);
    Peer hub_b(100, t_hub_b, keys_hub);
    Peer node_b(2, t_b, keys_b);

    // Register keys
    node_a.register_peer_key(100, keys_hub.public_key);
    hub_a.register_peer_key(1, keys_a.public_key);
    hub_b.register_peer_key(2, keys_b.public_key);
    node_b.register_peer_key(100, keys_hub.public_key);

    // Declare the same path on all nodes
    StatePath pos("/game/player/1/pos");
    node_a.declare(pos, CrdtType::LWW_REGISTER);
    hub_a.declare(pos, CrdtType::LWW_REGISTER);
    hub_b.declare(pos, CrdtType::LWW_REGISTER);
    node_b.declare(pos, CrdtType::LWW_REGISTER);

    // Connect A <-> Hub-A
    std::thread t1([&]() { hub_a.accept_from(ep_a, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(node_a.connect(ep_hub_a));
    t1.join();

    // Connect B <-> Hub-B
    std::thread t2([&]() { hub_b.accept_from(ep_b, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(node_b.connect(ep_hub_b));
    t2.join();

    ASSERT_TRUE(node_a.is_connected());
    ASSERT_TRUE(hub_a.is_connected());
    ASSERT_TRUE(hub_b.is_connected());
    ASSERT_TRUE(node_b.is_connected());

    // A writes a position
    std::string pos_data = "x:42,y:99";
    ASSERT_TRUE(node_a.set_lww(pos, reinterpret_cast<const uint8_t*>(pos_data.data()),
                                pos_data.size()));

    // A -> Hub-A
    int sent = node_a.flush();
    EXPECT_GT(sent, 0);
    int changes = hub_a.poll(1000);
    EXPECT_EQ(changes, 1);

    // Verify Hub-A received the data
    auto* hub_a_region = hub_a.state().get(pos);
    ASSERT_NE(hub_a_region, nullptr);
    auto* hub_a_lww = static_cast<LwwRegister*>(hub_a_region->crdt.get());
    std::string hub_a_val(hub_a_lww->value().begin(), hub_a_lww->value().end());
    EXPECT_EQ(hub_a_val, "x:42,y:99");

    // Hub-B manually merges (simulating relay: same state store via set_lww)
    ASSERT_TRUE(hub_b.set_lww(pos, reinterpret_cast<const uint8_t*>(pos_data.data()),
                               pos_data.size()));

    // Hub-B -> B
    sent = hub_b.flush();
    EXPECT_GT(sent, 0);
    changes = node_b.poll(1000);
    EXPECT_EQ(changes, 1);

    // Verify B received the data
    auto* b_region = node_b.state().get(pos);
    ASSERT_NE(b_region, nullptr);
    auto* b_lww = static_cast<LwwRegister*>(b_region->crdt.get());
    std::string b_val(b_lww->value().begin(), b_lww->value().end());
    EXPECT_EQ(b_val, "x:42,y:99");
}

TEST_F(MeshThreeNodeChain, CounterConvergesAcrossChain) {
    Peer node_a(1, t_a, keys_a);
    Peer hub_a(100, t_hub_a, keys_hub);
    Peer hub_b(100, t_hub_b, keys_hub);
    Peer node_b(2, t_b, keys_b);

    node_a.register_peer_key(100, keys_hub.public_key);
    hub_a.register_peer_key(1, keys_a.public_key);
    hub_b.register_peer_key(2, keys_b.public_key);
    node_b.register_peer_key(100, keys_hub.public_key);

    StatePath clicks("/app/clicks");
    node_a.declare(clicks, CrdtType::G_COUNTER);
    hub_a.declare(clicks, CrdtType::G_COUNTER);
    hub_b.declare(clicks, CrdtType::G_COUNTER);
    node_b.declare(clicks, CrdtType::G_COUNTER);

    // Connect A <-> Hub-A
    std::thread t1([&]() { hub_a.accept_from(ep_a, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(node_a.connect(ep_hub_a));
    t1.join();

    // Connect B <-> Hub-B
    std::thread t2([&]() { hub_b.accept_from(ep_b, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(node_b.connect(ep_hub_b));
    t2.join();

    // A increments by 7
    node_a.increment_counter(clicks, 7);
    node_a.flush();
    hub_a.poll(500);

    // Verify hub_a got it
    auto* hub_a_gc = static_cast<GCounter*>(hub_a.state().get(clicks)->crdt.get());
    EXPECT_EQ(hub_a_gc->value(), 7u);

    // Hub relays: hub_b increments by same amount (simulating relay merge)
    hub_b.increment_counter(clicks, 7);
    hub_b.flush();
    node_b.poll(500);

    auto* b_gc = static_cast<GCounter*>(node_b.state().get(clicks)->crdt.get());
    EXPECT_EQ(b_gc->value(), 7u);

    // B also increments locally
    node_b.increment_counter(clicks, 3);
    auto* b_gc2 = static_cast<GCounter*>(node_b.state().get(clicks)->crdt.get());
    EXPECT_EQ(b_gc2->value(), 10u);
}

// --- Multi-connection with connect_to/accept_node ---

TEST(MeshMultiConn, HubConnectsToTwoNodes) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport t_hub(bus), t_a(bus), t_b(bus);

    Endpoint ep_hub{"loop", 10};
    Endpoint ep_a{"loop", 20};
    Endpoint ep_b{"loop", 30};

    t_hub.bind(ep_hub);
    t_a.bind(ep_a);
    t_b.bind(ep_b);

    auto keys_hub = KeyPair::generate();
    auto keys_a = KeyPair::generate();
    auto keys_b = KeyPair::generate();

    Peer hub(100, t_hub, keys_hub);
    Peer node_a(1, t_a, keys_a);
    Peer node_b(2, t_b, keys_b);

    hub.register_peer_key(1, keys_a.public_key);
    hub.register_peer_key(2, keys_b.public_key);
    node_a.register_peer_key(100, keys_hub.public_key);
    node_b.register_peer_key(100, keys_hub.public_key);

    // Connect A -> Hub
    ASSERT_TRUE(connect_peers(node_a, 100, ep_hub, hub, 1, ep_a));

    // Connect B -> Hub
    ASSERT_TRUE(connect_peers(node_b, 100, ep_hub, hub, 2, ep_b));

    EXPECT_TRUE(hub.is_connected_to(1));
    EXPECT_TRUE(hub.is_connected_to(2));
    EXPECT_TRUE(node_a.is_connected_to(100));
    EXPECT_TRUE(node_b.is_connected_to(100));

    auto hub_nodes = hub.connected_nodes();
    EXPECT_EQ(hub_nodes.size(), 2u);
}

// --- Router integration: declare auto-announces, routes learned ---

TEST(MeshRouting, DeclareAnnouncesAndRouteIsLearned) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport t1(bus), t2(bus);
    t1.bind({"loop", 1});
    t2.bind({"loop", 2});

    auto keys1 = KeyPair::generate();
    auto keys2 = KeyPair::generate();

    Peer peer1(1, t1, keys1);
    Peer peer2(2, t2, keys2);

    StatePath path("/game/scores");
    peer1.declare(path, CrdtType::G_COUNTER);

    // Declaring should add to local_paths (announce, not remote route)
    EXPECT_EQ(peer1.router().local_paths().size(), 1u);
    EXPECT_EQ(peer1.router().local_paths()[0], path.hash());

    // peer2 learns the route manually (simulating ROUTE_ANNOUNCE reception)
    peer2.router().learn_route(path.hash(), 1);
    EXPECT_TRUE(peer2.router().has_route(path.hash()));

    // select_next_hops should return node 1
    auto hops = peer2.router().select_next_hops(path.hash());
    EXPECT_EQ(hops.size(), 1u);
    if (!hops.empty()) {
        EXPECT_EQ(hops[0], 1u);
    }
}

// --- Disconnect failover: removing a node cleans up routes ---

TEST(MeshFailover, DisconnectRemovesRoutes) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport t_hub(bus), t_a(bus);

    Endpoint ep_hub{"loop", 10};
    Endpoint ep_a{"loop", 20};
    t_hub.bind(ep_hub);
    t_a.bind(ep_a);

    auto keys_hub = KeyPair::generate();
    auto keys_a = KeyPair::generate();

    Peer hub(100, t_hub, keys_hub);
    Peer node_a(1, t_a, keys_a);

    hub.register_peer_key(1, keys_a.public_key);
    node_a.register_peer_key(100, keys_hub.public_key);

    // Hub learns routes via node A
    hub.router().learn_route(0xAAAA, 1);
    hub.router().learn_route(0xBBBB, 1);
    EXPECT_TRUE(hub.router().has_route(0xAAAA));
    EXPECT_TRUE(hub.router().has_route(0xBBBB));

    // Connect then disconnect
    ASSERT_TRUE(connect_peers(node_a, 100, ep_hub, hub, 1, ep_a));
    EXPECT_TRUE(hub.is_connected_to(1));

    hub.disconnect_node(1);
    EXPECT_FALSE(hub.is_connected_to(1));

    // Routes through node 1 should be removed
    EXPECT_FALSE(hub.router().has_route(0xAAAA));
    EXPECT_FALSE(hub.router().has_route(0xBBBB));
}

// --- Resolution filtering in mesh: hub sends COARSE to far node ---

TEST(MeshResolution, PerConnectionResolutionFiltering) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport t_hub(bus), t_near(bus), t_far(bus);

    Endpoint ep_hub{"loop", 10};
    Endpoint ep_near{"loop", 20};
    Endpoint ep_far{"loop", 30};
    t_hub.bind(ep_hub);
    t_near.bind(ep_near);
    t_far.bind(ep_far);

    auto keys_hub = KeyPair::generate();
    auto keys_near = KeyPair::generate();
    auto keys_far = KeyPair::generate();

    Peer hub(100, t_hub, keys_hub);
    Peer near_node(1, t_near, keys_near);
    Peer far_node(2, t_far, keys_far);

    hub.register_peer_key(1, keys_near.public_key);
    hub.register_peer_key(2, keys_far.public_key);
    near_node.register_peer_key(100, keys_hub.public_key);
    far_node.register_peer_key(100, keys_hub.public_key);

    StatePath pos("/game/pos");
    hub.declare(pos, CrdtType::LWW_REGISTER);
    near_node.declare(pos, CrdtType::LWW_REGISTER);
    far_node.declare(pos, CrdtType::LWW_REGISTER);

    // Connect both
    ASSERT_TRUE(connect_peers(near_node, 100, ep_hub, hub, 1, ep_near));
    ASSERT_TRUE(connect_peers(far_node, 100, ep_hub, hub, 2, ep_far));

    // Set METADATA tier for far node — should filter out data deltas
    hub.set_connection_resolution(2, ResolutionTier::METADATA);

    // Hub writes data
    std::string data = "x:1,y:2";
    ASSERT_TRUE(hub.set_lww(pos, reinterpret_cast<const uint8_t*>(data.data()), data.size()));

    // Flush — should send to near (FULL) but filter for far (METADATA)
    int sent = hub.flush();
    EXPECT_GT(sent, 0);

    // Near node should receive the update
    int near_changes = near_node.poll(500);
    EXPECT_EQ(near_changes, 1);

    // Far node should NOT receive the data delta (METADATA filters it)
    int far_changes = far_node.poll(100);
    EXPECT_EQ(far_changes, 0);
}

// --- forward_deltas path test ---

TEST(MeshForwarding, ForwardDeltasSelectsCorrectHops) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport t1(bus);
    t1.bind({"loop", 1});

    auto keys = KeyPair::generate();
    Peer hub(100, t1, keys);

    // Teach router about paths via different nodes
    hub.router().learn_route(0x1111, 1);
    hub.router().learn_route(0x2222, 2);
    hub.router().learn_route(0x3333, 3);

    // select_next_hops should find each
    auto hops1 = hub.router().select_next_hops(0x1111);
    EXPECT_EQ(hops1.size(), 1u);
    if (!hops1.empty()) EXPECT_EQ(hops1[0], 1u);

    auto hops2 = hub.router().select_next_hops(0x2222);
    EXPECT_EQ(hops2.size(), 1u);
    if (!hops2.empty()) EXPECT_EQ(hops2[0], 2u);

    // Unknown path — no hops
    auto hops_unknown = hub.router().select_next_hops(0x9999);
    EXPECT_TRUE(hops_unknown.empty());
}

// --- Hebbian learning: success strengthens route weight ---

TEST(MeshHebbian, DeliverySuccessStrengthensRoute) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport t1(bus);
    t1.bind({"loop", 1});

    auto keys = KeyPair::generate();
    Peer peer(1, t1, keys);

    peer.router().learn_route(0xAAAA, 2);

    // Get initial weight
    auto& table = peer.router().route_table();
    auto route_before = table.best_route(0xAAAA);
    bool has_before = route_before.has_value();
    ASSERT_TRUE(has_before);
    double weight_before = route_before->weight;

    // Simulate delivery success
    peer.router().on_delivery_success(2, 0xAAAA, 5000);

    auto route_after = table.best_route(0xAAAA);
    bool has_after = route_after.has_value();
    ASSERT_TRUE(has_after);
    double weight_after = route_after->weight;
    EXPECT_GT(weight_after, weight_before);
}

TEST(MeshHebbian, DeliveryFailureWeakensRoute) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport t1(bus);
    t1.bind({"loop", 1});

    auto keys = KeyPair::generate();
    Peer peer(1, t1, keys);

    // Start with a strong route
    peer.router().learn_route(0xBBBB, 3);
    peer.router().on_delivery_success(3, 0xBBBB, 1000);
    peer.router().on_delivery_success(3, 0xBBBB, 1000);

    auto route_strong = peer.router().route_table().best_route(0xBBBB);
    bool has_strong = route_strong.has_value();
    ASSERT_TRUE(has_strong);
    double weight_strong = route_strong->weight;

    // Failure should reduce weight
    peer.router().on_delivery_failure(3, 0xBBBB);

    auto route_weak = peer.router().route_table().best_route(0xBBBB);
    bool has_weak = route_weak.has_value();
    ASSERT_TRUE(has_weak);
    double weight_weak = route_weak->weight;
    EXPECT_LT(weight_weak, weight_strong);
}
