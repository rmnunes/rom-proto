#include "protocoll/connection/connection_manager.h"

namespace protocoll {

ManagedConnection* ConnectionManager::add(uint16_t remote_node_id) {
    auto [it, inserted] = conns_.try_emplace(remote_node_id);
    if (!inserted) return nullptr;
    it->second.remote_node_id = remote_node_id;
    return &it->second;
}

bool ConnectionManager::remove(uint16_t remote_node_id) {
    return conns_.erase(remote_node_id) > 0;
}

ManagedConnection* ConnectionManager::get(uint16_t remote_node_id) {
    auto it = conns_.find(remote_node_id);
    return it != conns_.end() ? &it->second : nullptr;
}

const ManagedConnection* ConnectionManager::get(uint16_t remote_node_id) const {
    auto it = conns_.find(remote_node_id);
    return it != conns_.end() ? &it->second : nullptr;
}

ManagedConnection* ConnectionManager::get_by_conn_id(uint16_t local_conn_id) {
    for (auto& [id, mc] : conns_) {
        if (mc.conn.local_conn_id() == local_conn_id) {
            return &mc;
        }
    }
    return nullptr;
}

bool ConnectionManager::has_connections() const {
    for (const auto& [id, mc] : conns_) {
        if (mc.conn.state() == ConnectionState::CONNECTED) return true;
    }
    return false;
}

ManagedConnection* ConnectionManager::primary() {
    if (conns_.empty()) return nullptr;
    return &conns_.begin()->second;
}

const ManagedConnection* ConnectionManager::primary() const {
    if (conns_.empty()) return nullptr;
    return &conns_.begin()->second;
}

std::vector<uint16_t> ConnectionManager::connected_nodes() const {
    std::vector<uint16_t> nodes;
    for (const auto& [id, mc] : conns_) {
        if (mc.conn.state() == ConnectionState::CONNECTED) {
            nodes.push_back(id);
        }
    }
    return nodes;
}

} // namespace protocoll
