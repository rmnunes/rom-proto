#pragma once

// Aggregator: batches incoming deltas by path, merges them via CRDT
// semantics, and emits a single merged delta per path per batch window.
//
// This reduces bandwidth when multiple sources update the same path
// rapidly — the relay merges N deltas into 1 before forwarding.
//
// Usage:
//   Aggregator agg;
//   agg.set_emit_callback([](uint32_t path, CrdtType type, auto& data) { ... });
//   agg.ingest(path_hash, CrdtType::LWW_REGISTER, delta_data, len);
//   agg.flush();  // or flush_if_ready(max_batch_size, max_age_us)

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <functional>
#include <chrono>

#include "protocoll/wire/frame_types.h"
#include "protocoll/state/crdt/crdt.h"

namespace protocoll {

struct AggregatorConfig {
    // Max deltas per path before auto-flush
    uint32_t max_batch_size = 10;

    // Max age (microseconds) of oldest delta before auto-flush (0 = manual only)
    uint64_t max_batch_age_us = 50000; // 50ms default
};

// Callback when a merged delta is ready to emit
using AggregatorEmitCallback = std::function<void(
    uint32_t path_hash, CrdtType crdt_type,
    const uint8_t* merged_data, size_t merged_len)>;

class Aggregator {
public:
    explicit Aggregator(AggregatorConfig config = {});

    // Ingest a delta for a given path. May trigger auto-flush if batch is full.
    void ingest(uint32_t path_hash, CrdtType crdt_type,
                const uint8_t* delta_data, size_t delta_len);

    // Flush all paths — emits merged delta for each path with pending data.
    // Returns number of paths flushed.
    size_t flush();

    // Flush paths that have reached batch size or age limits.
    // Returns number of paths flushed.
    size_t flush_if_ready();

    // Set the callback for emitting merged deltas
    void set_emit_callback(AggregatorEmitCallback cb) { emit_cb_ = std::move(cb); }

    // Reset all state
    void reset();

    // --- Accessors ---
    const AggregatorConfig& config() const { return config_; }
    void set_config(const AggregatorConfig& cfg) { config_ = cfg; }

    // Number of paths with pending deltas
    size_t pending_path_count() const { return buckets_.size(); }

    // Total deltas ingested (lifetime)
    uint64_t total_ingested() const { return total_ingested_; }

    // Total merged deltas emitted (lifetime)
    uint64_t total_emitted() const { return total_emitted_; }

private:
    struct Bucket {
        CrdtType crdt_type;
        std::unique_ptr<Crdt> crdt;
        uint32_t delta_count = 0;
        std::chrono::steady_clock::time_point first_ingest;
    };

    AggregatorConfig config_;
    std::unordered_map<uint32_t, Bucket> buckets_;
    AggregatorEmitCallback emit_cb_;

    uint64_t total_ingested_ = 0;
    uint64_t total_emitted_ = 0;

    void flush_bucket(uint32_t path_hash, Bucket& bucket);
    std::unique_ptr<Crdt> create_crdt(CrdtType type);
};

} // namespace protocoll
