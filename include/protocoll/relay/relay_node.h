#pragma once

// RelayNode: an intermediate node that receives state deltas,
// merges them via CRDTs, and forwards the merged result.
//
// A relay subscribes at FULL resolution to upstream peers and
// can forward at any resolution tier to downstream subscribers.
// It uses an Aggregator to batch and merge deltas before forwarding,
// reducing bandwidth in mesh topologies.
//
// Usage:
//   RelayNode relay(node_id);
//   relay.set_forward_callback([](uint32_t path, CrdtType type, auto& data) { ... });
//   relay.receive_delta(path_hash, CrdtType::LWW_REGISTER, data, len, source_node);
//   relay.tick();  // periodic flush

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_set>
#include <functional>

#include "protocoll/wire/frame_types.h"
#include "protocoll/relay/aggregator.h"
#include "protocoll/state/resolution.h"

namespace protocoll {

struct RelayConfig {
    AggregatorConfig aggregator;

    // Resolution tier for forwarding (downstream gets this granularity)
    ResolutionTier forward_tier = ResolutionTier::FULL;
    ResolutionConfig resolution;

    // If true, relay also stores a local copy of merged state
    bool store_local = false;
};

// Callback when the relay wants to forward a merged delta downstream
using RelayForwardCallback = std::function<void(
    uint32_t path_hash, CrdtType crdt_type,
    const uint8_t* data, size_t len)>;

class RelayNode {
public:
    explicit RelayNode(uint16_t node_id, RelayConfig config = {});

    // --- Ingestion ---

    // Receive a delta from an upstream peer.
    // source_node: the originating node ID (for loop prevention)
    void receive_delta(uint32_t path_hash, CrdtType crdt_type,
                       const uint8_t* data, size_t len, uint16_t source_node);

    // --- Forwarding ---

    // Set callback for forwarding merged deltas downstream
    void set_forward_callback(RelayForwardCallback cb) { forward_cb_ = std::move(cb); }

    // Periodic tick — flushes aggregator, applies resolution filter, forwards
    void tick();

    // Force flush all pending deltas
    size_t flush();

    // --- Loop prevention ---

    // Mark a node as a downstream subscriber (won't forward deltas back to source)
    void add_downstream(uint16_t node_id);
    void remove_downstream(uint16_t node_id);
    bool is_downstream(uint16_t node_id) const;

    // --- Accessors ---

    uint16_t node_id() const { return node_id_; }
    const RelayConfig& config() const { return config_; }
    void set_config(const RelayConfig& cfg);

    const Aggregator& aggregator() const { return aggregator_; }
    const DeltaFilter& filter() const { return filter_; }

    // Stats
    uint64_t deltas_received() const { return deltas_received_; }
    uint64_t deltas_forwarded() const { return deltas_forwarded_; }
    uint64_t deltas_filtered() const { return deltas_filtered_; }
    uint64_t loops_prevented() const { return loops_prevented_; }

private:
    uint16_t node_id_;
    RelayConfig config_;
    Aggregator aggregator_;
    DeltaFilter filter_;
    RelayForwardCallback forward_cb_;

    // Track source nodes for loop prevention per path
    struct PendingForward {
        uint32_t path_hash;
        CrdtType crdt_type;
        std::vector<uint8_t> data;
    };
    std::vector<PendingForward> forward_queue_;

    // Downstream node IDs (don't forward back to sources)
    std::unordered_set<uint16_t> downstream_nodes_;

    // Per-path last source tracking for loop prevention
    std::unordered_map<uint32_t, uint16_t> last_source_;

    uint64_t deltas_received_ = 0;
    uint64_t deltas_forwarded_ = 0;
    uint64_t deltas_filtered_ = 0;
    uint64_t loops_prevented_ = 0;

    void on_aggregator_emit(uint32_t path_hash, CrdtType crdt_type,
                            const uint8_t* data, size_t len);
};

} // namespace protocoll
