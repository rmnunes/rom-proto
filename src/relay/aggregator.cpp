#include "protocoll/relay/aggregator.h"
#include "protocoll/state/crdt/lww_register.h"
#include "protocoll/state/crdt/g_counter.h"
#include "protocoll/state/crdt/pn_counter.h"
#include "protocoll/state/crdt/or_set.h"

namespace protocoll {

Aggregator::Aggregator(AggregatorConfig config)
    : config_(config) {}

std::unique_ptr<Crdt> Aggregator::create_crdt(CrdtType type) {
    // Use node_id=0 for relay-owned CRDTs (merge doesn't depend on local ID)
    constexpr uint16_t relay_id = 0;
    switch (type) {
        case CrdtType::LWW_REGISTER:
            return std::make_unique<LwwRegister>(relay_id);
        case CrdtType::G_COUNTER:
            return std::make_unique<GCounter>(relay_id);
        case CrdtType::PN_COUNTER:
            return std::make_unique<PnCounter>(relay_id);
        case CrdtType::OR_SET:
            return std::make_unique<OrSet>(relay_id);
        default:
            return nullptr;
    }
}

void Aggregator::ingest(uint32_t path_hash, CrdtType crdt_type,
                        const uint8_t* delta_data, size_t delta_len) {
    total_ingested_++;

    auto it = buckets_.find(path_hash);
    if (it == buckets_.end()) {
        Bucket bucket;
        bucket.crdt_type = crdt_type;
        bucket.crdt = create_crdt(crdt_type);
        bucket.first_ingest = std::chrono::steady_clock::now();
        if (!bucket.crdt) return; // Unsupported CRDT type

        bucket.crdt->merge(delta_data, delta_len);
        bucket.delta_count = 1;
        it = buckets_.emplace(path_hash, std::move(bucket)).first;
    } else {
        it->second.crdt->merge(delta_data, delta_len);
        it->second.delta_count++;
    }

    // Auto-flush if batch size reached
    if (it->second.delta_count >= config_.max_batch_size) {
        flush_bucket(path_hash, it->second);
        buckets_.erase(it);
    }
}

void Aggregator::flush_bucket(uint32_t path_hash, Bucket& bucket) {
    if (!bucket.crdt || bucket.delta_count == 0) return;

    auto snapshot = bucket.crdt->snapshot();
    if (!snapshot.empty() && emit_cb_) {
        emit_cb_(path_hash, bucket.crdt_type, snapshot.data(), snapshot.size());
        total_emitted_++;
    }
}

size_t Aggregator::flush() {
    size_t count = 0;
    for (auto& [hash, bucket] : buckets_) {
        if (bucket.delta_count > 0) {
            flush_bucket(hash, bucket);
            count++;
        }
    }
    buckets_.clear();
    return count;
}

size_t Aggregator::flush_if_ready() {
    auto now = std::chrono::steady_clock::now();
    std::vector<uint32_t> to_flush;

    for (auto& [hash, bucket] : buckets_) {
        if (bucket.delta_count >= config_.max_batch_size) {
            to_flush.push_back(hash);
            continue;
        }
        if (config_.max_batch_age_us > 0) {
            auto age_us = std::chrono::duration_cast<std::chrono::microseconds>(
                now - bucket.first_ingest).count();
            if (static_cast<uint64_t>(age_us) >= config_.max_batch_age_us) {
                to_flush.push_back(hash);
            }
        }
    }

    for (uint32_t hash : to_flush) {
        auto it = buckets_.find(hash);
        if (it != buckets_.end()) {
            flush_bucket(hash, it->second);
            buckets_.erase(it);
        }
    }

    return to_flush.size();
}

void Aggregator::reset() {
    buckets_.clear();
    total_ingested_ = 0;
    total_emitted_ = 0;
}

} // namespace protocoll
