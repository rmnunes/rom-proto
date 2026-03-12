#pragma once

// ConnectionManager: manages multiple concurrent connections for a Peer.
//
// Replaces the single `Connection conn_` in Peer with a map of
// connection_id -> Connection, enabling mesh topologies where a
// single peer maintains connections to multiple remote peers.
//
// Each connection is identified by the remote node_id.

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <optional>

#include "protocoll/connection/connection.h"
#include "protocoll/connection/connection_id.h"
#include "protocoll/connection/handshake.h"
#include "protocoll/reliability/ack_tracker.h"

namespace protocoll {

struct ManagedConnection {
    uint16_t remote_node_id;
    Connection conn;
    SendTracker send_tracker;
    RecvTracker recv_tracker;
};

class ConnectionManager {
public:
    ConnectionManager() = default;

    // Add a new connection for a remote node.
    // Returns pointer to the managed connection, or nullptr if already exists.
    ManagedConnection* add(uint16_t remote_node_id);

    // Remove connection to a remote node. Returns true if found.
    bool remove(uint16_t remote_node_id);

    // Get connection by remote node ID
    ManagedConnection* get(uint16_t remote_node_id);
    const ManagedConnection* get(uint16_t remote_node_id) const;

    // Get connection by local connection ID (for incoming packets)
    ManagedConnection* get_by_conn_id(uint16_t local_conn_id);

    // Check if any connection is active
    bool has_connections() const;

    // Get the primary (first/only) connection — backward compatible
    ManagedConnection* primary();
    const ManagedConnection* primary() const;

    // Number of active connections
    size_t count() const { return conns_.size(); }

    // Get all connected remote node IDs
    std::vector<uint16_t> connected_nodes() const;

    // Iterate all connections
    template<typename Fn>
    void for_each(Fn fn) {
        for (auto& [id, mc] : conns_) fn(mc);
    }

    template<typename Fn>
    void for_each(Fn fn) const {
        for (const auto& [id, mc] : conns_) fn(mc);
    }

    // Allocate next connection ID
    uint16_t next_conn_id() { return id_alloc_.next(); }

private:
    std::unordered_map<uint16_t, ManagedConnection> conns_;
    ConnectionIdAllocator id_alloc_;
};

} // namespace protocoll
