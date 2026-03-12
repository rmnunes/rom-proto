#include "protocoll/routing/router.h"
#include <algorithm>

namespace protocoll {

Router::Router(uint16_t local_node_id, RouterConfig config)
    : local_node_id_(local_node_id), config_(config) {}

void Router::announce_path(uint32_t path_hash) {
    if (std::find(local_paths_.begin(), local_paths_.end(), path_hash) == local_paths_.end()) {
        local_paths_.push_back(path_hash);
    }
    if (on_announce_) {
        on_announce_(path_hash, local_node_id_);
    }
}

void Router::learn_route(uint32_t path_hash, uint16_t via_node) {
    if (via_node == local_node_id_) return; // Don't route to self

    // Check if we already have max routes for this path
    auto existing = table_.get_routes(path_hash);
    if (existing.size() >= config_.max_routes_per_path) {
        // Only add if this would replace a worse route
        if (!existing.empty() && existing.back().weight >= config_.min_route_weight) {
            return; // All routes are healthy, don't add more
        }
    }

    table_.add_route(path_hash, via_node, config_.min_route_weight * 2); // Start slightly above minimum
}

void Router::remove_node(uint16_t node_id) {
    table_.remove_node(node_id);
}

std::vector<uint16_t> Router::select_next_hops(uint32_t path_hash) const {
    auto routes = table_.routes_above(path_hash, config_.min_route_weight);
    std::vector<uint16_t> hops;
    for (const auto& r : routes) {
        hops.push_back(r.next_hop_node_id);
        if (hops.size() >= config_.max_routes_per_path) break;
    }
    return hops;
}

bool Router::has_route(uint32_t path_hash) const {
    return table_.best_route(path_hash).has_value();
}

void Router::on_delivery_success(uint16_t via_node, uint32_t path_hash, uint32_t rtt_us) {
    // Hebbian: strengthen the route by removing and re-adding with higher weight
    auto routes = table_.get_routes(path_hash);
    for (const auto& r : routes) {
        if (r.next_hop_node_id == via_node) {
            double new_weight = clamp_weight(r.weight + config_.success_increment);
            table_.remove_route(path_hash, via_node);
            table_.add_route(path_hash, via_node, new_weight);
            table_.on_success(path_hash, via_node, rtt_us);
            return;
        }
    }
}

void Router::on_delivery_failure(uint16_t via_node, uint32_t path_hash) {
    table_.on_failure(path_hash, via_node);

    // Hebbian: weaken the route
    auto routes = table_.get_routes(path_hash);
    for (const auto& r : routes) {
        if (r.next_hop_node_id == via_node) {
            double raw_weight = r.weight - config_.failure_decrement;
            table_.remove_route(path_hash, via_node);
            if (raw_weight >= config_.min_weight) {
                table_.add_route(path_hash, via_node, clamp_weight(raw_weight));
            }
            // If below min_weight, route is effectively dead (removed)
            break;
        }
    }
}

void Router::tick() {
    table_.decay_all(config_.decay_factor);
}

double Router::clamp_weight(double w) const {
    if (w < config_.min_weight) return config_.min_weight;
    if (w > config_.max_weight) return config_.max_weight;
    return w;
}

} // namespace protocoll
