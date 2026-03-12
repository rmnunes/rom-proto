#include "protocoll/relay/relay_node.h"

namespace protocoll {

RelayNode::RelayNode(uint16_t node_id, RelayConfig config)
    : node_id_(node_id)
    , config_(config)
    , aggregator_(config.aggregator)
    , filter_(config.forward_tier, config.resolution) {
    // Wire up aggregator -> relay forwarding pipeline
    aggregator_.set_emit_callback(
        [this](uint32_t path_hash, CrdtType crdt_type,
               const uint8_t* data, size_t len) {
            on_aggregator_emit(path_hash, crdt_type, data, len);
        });
}

void RelayNode::set_config(const RelayConfig& cfg) {
    config_ = cfg;
    aggregator_.set_config(cfg.aggregator);
    filter_.set_tier(cfg.forward_tier);
    filter_.set_config(cfg.resolution);
}

void RelayNode::receive_delta(uint32_t path_hash, CrdtType crdt_type,
                              const uint8_t* data, size_t len,
                              uint16_t source_node) {
    deltas_received_++;

    // Track last source for loop prevention
    last_source_[path_hash] = source_node;

    // Feed into aggregator for merging
    aggregator_.ingest(path_hash, crdt_type, data, len);
}

void RelayNode::on_aggregator_emit(uint32_t path_hash, CrdtType crdt_type,
                                   const uint8_t* data, size_t len) {
    // Apply resolution filter
    if (config_.forward_tier != ResolutionTier::FULL) {
        if (!filter_.should_forward(path_hash, data, len)) {
            deltas_filtered_++;
            return;
        }
    }

    // Queue for forwarding
    forward_queue_.push_back({path_hash, crdt_type,
                              std::vector<uint8_t>(data, data + len)});
}

void RelayNode::tick() {
    // Flush aggregator (time-based)
    aggregator_.flush_if_ready();

    // Process forward queue
    for (auto& fwd : forward_queue_) {
        if (forward_cb_) {
            forward_cb_(fwd.path_hash, fwd.crdt_type,
                        fwd.data.data(), fwd.data.size());
            deltas_forwarded_++;
        }
    }
    forward_queue_.clear();
}

size_t RelayNode::flush() {
    size_t flushed = aggregator_.flush();

    // Process any newly queued forwards
    for (auto& fwd : forward_queue_) {
        if (forward_cb_) {
            forward_cb_(fwd.path_hash, fwd.crdt_type,
                        fwd.data.data(), fwd.data.size());
            deltas_forwarded_++;
        }
    }
    forward_queue_.clear();

    return flushed;
}

void RelayNode::add_downstream(uint16_t node_id) {
    downstream_nodes_.insert(node_id);
}

void RelayNode::remove_downstream(uint16_t node_id) {
    downstream_nodes_.erase(node_id);
}

bool RelayNode::is_downstream(uint16_t node_id) const {
    return downstream_nodes_.count(node_id) > 0;
}

} // namespace protocoll
