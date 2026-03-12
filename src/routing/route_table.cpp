#include "protocoll/routing/route_table.h"
#include <algorithm>

namespace protocoll {

void RouteTable::add_route(uint32_t path_hash, uint16_t next_hop, double initial_weight) {
    auto* existing = find_route(path_hash, next_hop);
    if (existing) {
        // Route already exists — don't reset it
        return;
    }

    Route r;
    r.next_hop_node_id = next_hop;
    r.weight = initial_weight;
    r.latency_ema_us = 0;
    routes_[path_hash].push_back(r);
}

bool RouteTable::remove_route(uint32_t path_hash, uint16_t next_hop) {
    auto it = routes_.find(path_hash);
    if (it == routes_.end()) return false;

    auto& vec = it->second;
    auto removed = std::remove_if(vec.begin(), vec.end(),
        [next_hop](const Route& r) { return r.next_hop_node_id == next_hop; });

    if (removed == vec.end()) return false;
    vec.erase(removed, vec.end());

    if (vec.empty()) routes_.erase(it);
    return true;
}

void RouteTable::remove_node(uint16_t node_id) {
    for (auto it = routes_.begin(); it != routes_.end(); ) {
        auto& vec = it->second;
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                [node_id](const Route& r) { return r.next_hop_node_id == node_id; }),
            vec.end());

        if (vec.empty()) {
            it = routes_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<Route> RouteTable::get_routes(uint32_t path_hash) const {
    auto it = routes_.find(path_hash);
    if (it == routes_.end()) return {};

    auto routes = it->second;
    std::sort(routes.begin(), routes.end(),
        [](const Route& a, const Route& b) { return a.weight > b.weight; });
    return routes;
}

std::optional<Route> RouteTable::best_route(uint32_t path_hash) const {
    auto it = routes_.find(path_hash);
    if (it == routes_.end() || it->second.empty()) return std::nullopt;

    auto best = std::max_element(it->second.begin(), it->second.end(),
        [](const Route& a, const Route& b) { return a.weight < b.weight; });
    return *best;
}

std::vector<Route> RouteTable::routes_above(uint32_t path_hash, double min_weight) const {
    auto it = routes_.find(path_hash);
    if (it == routes_.end()) return {};

    std::vector<Route> result;
    for (const auto& r : it->second) {
        if (r.weight >= min_weight) result.push_back(r);
    }
    std::sort(result.begin(), result.end(),
        [](const Route& a, const Route& b) { return a.weight > b.weight; });
    return result;
}

void RouteTable::on_success(uint32_t path_hash, uint16_t via_node, uint32_t rtt_us) {
    auto* r = find_route(path_hash, via_node);
    if (!r) return;

    r->success_count++;
    r->last_success_us = rtt_us; // reusing as timestamp placeholder

    // Update latency EMA
    if (r->latency_ema_us == 0) {
        r->latency_ema_us = static_cast<double>(rtt_us);
    } else {
        constexpr double alpha = 0.3;
        r->latency_ema_us = alpha * rtt_us + (1.0 - alpha) * r->latency_ema_us;
    }
}

void RouteTable::on_failure(uint32_t path_hash, uint16_t via_node) {
    auto* r = find_route(path_hash, via_node);
    if (!r) return;

    r->failure_count++;
}

void RouteTable::decay_all(double decay_factor) {
    for (auto& [hash, routes] : routes_) {
        for (auto& r : routes) {
            r.weight *= decay_factor;
        }
    }
}

size_t RouteTable::total_routes() const {
    size_t total = 0;
    for (const auto& [hash, routes] : routes_) {
        total += routes.size();
    }
    return total;
}

void RouteTable::clear() {
    routes_.clear();
}

Route* RouteTable::find_route(uint32_t path_hash, uint16_t next_hop) {
    auto it = routes_.find(path_hash);
    if (it == routes_.end()) return nullptr;

    for (auto& r : it->second) {
        if (r.next_hop_node_id == next_hop) return &r;
    }
    return nullptr;
}

const Route* RouteTable::find_route(uint32_t path_hash, uint16_t next_hop) const {
    auto it = routes_.find(path_hash);
    if (it == routes_.end()) return nullptr;

    for (const auto& r : it->second) {
        if (r.next_hop_node_id == next_hop) return &r;
    }
    return nullptr;
}

} // namespace protocoll
