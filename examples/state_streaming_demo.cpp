// State Streaming Demo
//
// Demonstrates the core ProtoCol paradigm: two peers continuously
// stream state updates through CRDT-backed state regions.
// No request/response. No explicit fetch. Just mutate locally,
// flush, and the other side sees it instantly.
//
// This is the "video frame" model applied to application state.

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

#include "protocoll/peer.h"
#include "protocoll/security/crypto.h"
#include "protocoll/transport/loopback_transport.h"
#include "protocoll/state/crdt/lww_register.h"
#include "protocoll/state/crdt/g_counter.h"

using namespace protocoll;

int main() {
    printf("=== ProtoCol State Streaming Demo ===\n\n");

    // Create in-process transport (simulates network)
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport client_transport(bus), server_transport(bus);

    Endpoint client_ep{"loopback", 1};
    Endpoint server_ep{"loopback", 2};
    client_transport.bind(client_ep);
    server_transport.bind(server_ep);

    // Generate Ed25519 key pairs — every peer signs its state deltas
    auto server_keys = KeyPair::generate();
    auto client_keys = KeyPair::generate();

    // Create peers with mandatory identity
    Peer server(1, server_transport, server_keys);
    Peer client(2, client_transport, client_keys);

    // Exchange public keys so each side can verify the other's signatures
    server.register_peer_key(2, client_keys.public_key);
    client.register_peer_key(1, server_keys.public_key);

    // Declare shared state regions
    StatePath player_pos("/game/player/1/position");
    StatePath player_hp("/game/player/1/health");
    StatePath score("/game/score");

    server.declare(player_pos, CrdtType::LWW_REGISTER);
    server.declare(player_hp, CrdtType::LWW_REGISTER);
    server.declare(score, CrdtType::G_COUNTER);

    client.declare(player_pos, CrdtType::LWW_REGISTER);
    client.declare(player_hp, CrdtType::LWW_REGISTER);
    client.declare(score, CrdtType::G_COUNTER);

    // Reactive callback on server: fires when any state changes
    server.state().set_update_callback([](const StatePath& path, const uint8_t*, size_t) {
        printf("  [server] state updated: %s\n", path.str().c_str());
    });

    client.state().set_update_callback([](const StatePath& path, const uint8_t*, size_t) {
        printf("  [client] state updated: %s\n", path.str().c_str());
    });

    // --- Handshake ---
    printf("1. Connecting...\n");
    std::thread accept_thread([&]() { server.accept_from(client_ep, 5000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    bool ok = client.connect(server_ep);
    accept_thread.join();
    printf("   Connected: %s\n\n", ok ? "yes" : "no");

    // --- Stream state updates (like video frames) ---
    printf("2. Streaming position updates (simulating 10 'frames')...\n");
    for (int i = 0; i < 10; i++) {
        // Client moves player
        std::string pos = "x:" + std::to_string(i * 10) + ",y:" + std::to_string(i * 5);
        client.set_lww(player_pos,
            reinterpret_cast<const uint8_t*>(pos.data()), pos.size());

        // Client scores points
        client.increment_counter(score, 100);

        // Flush: sign + encode deltas -> send over transport
        client.flush();

        // Server receives: decode -> verify signature -> CRDT merge -> callback fires
        server.poll(100);
    }

    // --- Read final state on server ---
    printf("\n3. Final state on server:\n");
    auto* pos_region = server.state().get(player_pos);
    auto* lww = static_cast<LwwRegister*>(pos_region->crdt.get());
    printf("   Position: %s\n", std::string(lww->value().begin(), lww->value().end()).c_str());

    auto* score_region = server.state().get(score);
    auto* gc = static_cast<GCounter*>(score_region->crdt.get());
    printf("   Score: %llu\n", static_cast<unsigned long long>(gc->value()));

    // --- Server pushes health update back to client ---
    printf("\n4. Server sets player health, client receives:\n");
    std::string hp = "hp:85/100";
    server.set_lww(player_hp,
        reinterpret_cast<const uint8_t*>(hp.data()), hp.size());
    server.flush();
    client.poll(100);

    auto* hp_region = client.state().get(player_hp);
    auto* hp_lww = static_cast<LwwRegister*>(hp_region->crdt.get());
    printf("   Client sees health: %s\n",
        std::string(hp_lww->value().begin(), hp_lww->value().end()).c_str());

    // --- Disconnect ---
    printf("\n5. Disconnecting...\n");
    client.disconnect();
    printf("   Done.\n");

    printf("\n=== Demo Complete ===\n");
    printf("No HTTP. No REST. No JSON. No request/response.\n");
    printf("Just continuous state streaming with automatic CRDT merge.\n");
    printf("Every delta is Ed25519-signed. Trust the data, not the channel.\n");

    return 0;
}
