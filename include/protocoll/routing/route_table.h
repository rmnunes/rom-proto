#pragma once

// RouteTable: maps state path hashes to candidate next-hop routes.
//
// Each route tracks a Hebbian weight that strengthens with successful
// delivery and weakens with failure — paths that reliably deliver
// state updates get preferred over time.

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <optional>

namespace protocoll {

struct Route {
    uint16_t next_hop_node_id;
    double   weight;             // Hebbian weight [0.0, 1.0]
    double   latency_ema_us;     // Exponential moving average RTT in microseconds
    uint32_t success_count = 0;
    uint32_t failure_count = 0;
    uint64_t last_success_us = 0;
    uint64_t last_failure_us = 0;
};

class RouteTable {
public:
    // Add or update a route for a given path hash
    void add_route(uint32_t path_hash, uint16_t next_hop, double initial_weight = 0.5);

    // Remove a specific route
    bool remove_route(uint32_t path_hash, uint16_t next_hop);

    // Remove all routes through a specific node (e.g., on disconnect)
    void remove_node(uint16_t node_id);

    // Get all routes for a path hash, sorted by weight descending
    std::vector<Route> get_routes(uint32_t path_hash) const;

    // Get the best route (highest weight) for a path hash
    std::optional<Route> best_route(uint32_t path_hash) const;

    // Get all routes for a path hash with weight above threshold
    std::vector<Route> routes_above(uint32_t path_hash, double min_weight) const;

    // Update route weight on delivery success
    void on_success(uint32_t path_hash, uint16_t via_node, uint32_t rtt_us);

    // Update route weight on delivery failure
    void on_failure(uint32_t path_hash, uint16_t via_node);

    // Apply time-based decay to all routes (call periodically)
    // decay_factor in (0, 1): multiplies all weights
    void decay_all(double decay_factor);

    // Total number of routes across all paths
    size_t total_routes() const;

    // Number of distinct path hashes with routes
    size_t path_count() const { return routes_.size(); }

    // Clear all routes
    void clear();

    // Iterate all (path_hash, routes) pairs
    template<typename Fn>
    void for_each(Fn fn) const {
        for (const auto& [hash, routes] : routes_) {
            fn(hash, routes);
        }
    }

private:
    // path_hash -> vector of candidate routes
    std::unordered_map<uint32_t, std::vector<Route>> routes_;

    // Find a route by (path_hash, next_hop)
    Route* find_route(uint32_t path_hash, uint16_t next_hop);
    const Route* find_route(uint32_t path_hash, uint16_t next_hop) const;
};

} // namespace protocoll
