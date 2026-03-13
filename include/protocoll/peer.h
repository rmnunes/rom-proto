#pragma once

// Peer: high-level API for a protocol endpoint.
//
// Every Peer has an Ed25519 identity. Every state delta is signed.
// Every received delta is verified. "Trust the data, not the channel."
//
// Supports multiple simultaneous connections for mesh topologies.
// Router integration enables multi-hop state propagation.
// DeltaFilter integration enables per-connection resolution tiers.
//
// Usage:
//   auto keys = KeyPair::generate();
//   Peer peer(node_id, transport, keys);
//   peer.register_peer_key(remote_node_id, remote_public_key);
//   peer.declare("/game/player/1/pos", CrdtType::LWW_REGISTER);
//   peer.connect(server_endpoint);
//   peer.set_lww("/game/player/1/pos", data, len);
//   peer.flush();   // sign + encode + send
//   peer.poll();    // receive + verify + decode + merge

#include <cstdint>
#include <memory>
#include <functional>
#include <unordered_map>

#include "protocoll/transport/transport.h"
#include "protocoll/connection/connection.h"
#include "protocoll/connection/connection_manager.h"
#include "protocoll/connection/handshake.h"
#include "protocoll/reliability/ack_tracker.h"
#include "protocoll/state/state_registry.h"
#include "protocoll/state/resolution.h"
#include "protocoll/security/crypto.h"
#include "protocoll/security/capability.h"
#include "protocoll/routing/router.h"
#include "protocoll/state/subscription.h"
#include "protocoll/wire/codec.h"

namespace protocoll {

class Peer {
public:
    Peer(uint16_t node_id, Transport& transport, const KeyPair& keys);

    // --- Identity ---
    uint16_t node_id() const { return node_id_; }
    const PublicKey& public_key() const { return keys_.public_key; }

    // Register a remote peer's public key (required to verify their deltas)
    void register_peer_key(uint16_t node_id, const PublicKey& pk);

    // Capability store for access control
    CapabilityStore& capabilities() { return cap_store_; }
    const CapabilityStore& capabilities() const { return cap_store_; }

    // When true, poll() enforces CAP_WRITE for incoming mutations
    void set_access_control(bool enabled) { access_control_ = enabled; }

    // Callback for signature verification failures
    using SecurityCallback = std::function<void(uint16_t author_id, uint32_t path_hash)>;
    void on_signature_failure(SecurityCallback cb) { on_sig_failure_ = std::move(cb); }

    // --- Connection (single-peer, backward-compatible) ---
    bool connect(const Endpoint& remote);
    bool accept_from(const Endpoint& expected_from, int timeout_ms = 5000);
    bool is_connected() const;
    void disconnect(CloseReason reason = CloseReason::NORMAL);

    // --- Non-blocking handshake (for WASM/browser) ---
    // connect_start: sends CONNECT packet, returns immediately.
    // connect_poll: checks for ACCEPT (non-blocking). Returns true when connected.
    bool connect_start(const Endpoint& remote);
    bool connect_poll();
    // accept_start: prepares to receive CONNECT.
    // accept_poll: checks for CONNECT, sends ACCEPT if found. Returns true when connected.
    bool accept_start();
    bool accept_poll();

    // --- Multi-connection ---
    bool connect_to(uint16_t remote_node_id, const Endpoint& remote);
    bool accept_node(uint16_t remote_node_id, const Endpoint& expected_from,
                     int timeout_ms = 5000);
    void disconnect_node(uint16_t remote_node_id, CloseReason reason = CloseReason::NORMAL);
    bool is_connected_to(uint16_t remote_node_id) const;
    std::vector<uint16_t> connected_nodes() const;

    // --- Resolution tiers (per-connection) ---
    void set_connection_resolution(uint16_t remote_node_id, ResolutionTier tier,
                                    ResolutionConfig config = {});

    // --- Subscriptions ---
    // Subscribe to a path pattern with a resolution tier.
    // Returns subscription ID (> 0) on success.
    uint32_t subscribe(const StatePath& pattern, ResolutionTier tier,
                       int64_t initial_credits = -1, uint32_t freshness_us = 0);
    SubscriptionManager& subscriptions() { return sub_mgr_; }
    const SubscriptionManager& subscriptions() const { return sub_mgr_; }

    // --- State declaration ---
    bool declare(const StatePath& path, CrdtType type,
                 Reliability rel = Reliability::RELIABLE);

    // --- State mutation (local writes) ---
    bool set_lww(const StatePath& path, const uint8_t* data, size_t len);
    bool increment_counter(const StatePath& path, uint64_t amount = 1);

    // --- Network I/O ---
    int flush();
    int poll(int timeout_ms = 0);

    // --- Accessors ---
    StateRegistry& state() { return state_; }
    const StateRegistry& state() const { return state_; }
    ConnectionManager& connections() { return conn_mgr_; }
    const ConnectionManager& connections() const { return conn_mgr_; }
    Router& router() { return router_; }
    const Router& router() const { return router_; }
    const Endpoint& local_endpoint() const { return local_ep_; }
    void set_local_endpoint(const Endpoint& ep) { local_ep_ = ep; }

    // Backward-compatible: get the primary connection
    Connection& connection();
    const Connection& connection() const;

private:
    uint16_t node_id_;
    Transport& transport_;
    Endpoint local_ep_;
    ConnectionManager conn_mgr_;
    StateRegistry state_;
    Router router_;
    SubscriptionManager sub_mgr_;

    // Security — always on
    KeyPair keys_;
    CapabilityStore cap_store_;
    std::unordered_map<uint16_t, PublicKey> peer_keys_;
    bool access_control_ = false;
    SecurityCallback on_sig_failure_;

    // Per-connection resolution filters (remote_node_id -> filter)
    std::unordered_map<uint16_t, DeltaFilter> delta_filters_;

    uint8_t recv_buf_[MAX_PACKET_SIZE];

    // Non-blocking handshake state
    enum class HandshakeState { NONE, CONNECTING, ACCEPTING };
    HandshakeState hs_state_ = HandshakeState::NONE;
    Endpoint hs_remote_;

    int send_deltas_to(ManagedConnection& mc,
                       const std::vector<StateRegistry::PendingDelta>& deltas);
    int process_data_frames(ManagedConnection& mc, PacketDecoder& dec);
    void send_ack(ManagedConnection& mc);

    // Route-based forwarding: forward deltas to other connected nodes
    void forward_deltas(uint16_t source_node_id,
                        const std::vector<StateRegistry::PendingDelta>& deltas);

    // Build signed frame payload: [base_header][delta_data][signature(64)]
    std::vector<uint8_t> build_signed_delta(const StateRegistry::PendingDelta& d);
    // Verify signature on received frame. Returns true if valid.
    bool verify_frame_signature(const uint8_t* payload, size_t len,
                                 uint16_t author_node_id, size_t base_size);
};

} // namespace protocoll
