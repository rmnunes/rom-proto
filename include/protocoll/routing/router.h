#pragma once

// Router: Hebbian-inspired adaptive routing for mesh topologies.
//
// Routes strengthen with successful state propagation and weaken
// with failures. The router selects optimal next-hop nodes for
// forwarding state deltas based on learned path quality.
//
// Key concepts:
//   - Hebbian learning: "neurons that fire together wire together"
//     Applied: paths that successfully deliver get higher weight
//   - Multi-path: can select multiple routes for redundancy
//   - Decay: unused routes gradually lose weight

#include <cstdint>
#include <cstddef>
#include <vector>
#include <functional>

#include "protocoll/routing/route_table.h"

namespace protocoll {

struct RouterConfig {
    // Hebbian learning parameters
    double success_increment = 0.1;    // Weight increase on success
    double failure_decrement = 0.2;    // Weight decrease on failure
    double decay_factor = 0.995;       // Per-tick decay (call decay() periodically)

    // Route selection
    double min_route_weight = 0.1;     // Routes below this are considered dead
    uint8_t max_routes_per_path = 3;   // Max concurrent routes per path hash

    // Latency EMA smoothing factor (0-1, higher = more responsive)
    double latency_alpha = 0.3;

    // Weight bounds
    double min_weight = 0.01;
    double max_weight = 1.0;
};

// Callback when a route discovery/announcement should be broadcast
using RouteAnnounceCallback = std::function<void(uint32_t path_hash, uint16_t node_id)>;

class Router {
public:
    explicit Router(uint16_t local_node_id, RouterConfig config = {});

    // --- Route management ---

    // Announce that this node can serve a path hash (e.g., has declared it)
    void announce_path(uint32_t path_hash);

    // Learn a route from a neighbor announcement or forwarded packet
    void learn_route(uint32_t path_hash, uint16_t via_node);

    // Remove all routes through a node (on disconnect)
    void remove_node(uint16_t node_id);

    // --- Route selection ---

    // Select the best next-hop(s) for forwarding a delta to a given path
    // Returns empty if no route is known (local delivery only)
    std::vector<uint16_t> select_next_hops(uint32_t path_hash) const;

    // Check if we have any route for a path hash
    bool has_route(uint32_t path_hash) const;

    // --- Feedback (Hebbian learning) ---

    // Called when we get an ACK confirming delivery via a specific node
    void on_delivery_success(uint16_t via_node, uint32_t path_hash, uint32_t rtt_us);

    // Called when delivery fails (NACK or timeout) via a specific node
    void on_delivery_failure(uint16_t via_node, uint32_t path_hash);

    // --- Maintenance ---

    // Apply time-based weight decay (call periodically, e.g., every 100ms)
    void tick();

    // --- Accessors ---

    uint16_t local_node_id() const { return local_node_id_; }
    const RouteTable& route_table() const { return table_; }
    const RouterConfig& config() const { return config_; }
    void set_config(const RouterConfig& cfg) { config_ = cfg; }

    // Set callback for route announcements
    void set_announce_callback(RouteAnnounceCallback cb) { on_announce_ = std::move(cb); }

    // Get locally announced paths
    const std::vector<uint32_t>& local_paths() const { return local_paths_; }

private:
    uint16_t local_node_id_;
    RouterConfig config_;
    RouteTable table_;
    std::vector<uint32_t> local_paths_; // paths announced by this node
    RouteAnnounceCallback on_announce_;

    // Clamp weight to [min_weight, max_weight]
    double clamp_weight(double w) const;
};

} // namespace protocoll
