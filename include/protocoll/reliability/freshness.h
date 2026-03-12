#pragma once

// Freshness: manages delta expiry deadlines.
//
// Each STATE_DELTA can carry a freshness deadline. If the delta
// cannot be delivered before it expires, it is dropped — never
// retransmitted. This is the "real-time = willing to lose" principle.
//
// The sender stamps deltas with creation_time + deadline.
// The receiver (or sender's outbox) checks: is this delta still fresh?

#include <cstddef>
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace protocoll {

struct FreshnessStamp {
    uint32_t created_us;    // Timestamp when delta was created
    uint32_t deadline_us;   // Maximum age in microseconds (0 = no deadline)

    // Check if this delta is still fresh at the given time
    bool is_fresh(uint32_t now_us) const {
        if (deadline_us == 0) return true; // No deadline
        uint32_t age = now_us - created_us;
        return age <= deadline_us;
    }

    // Time remaining until expiry (0 if already expired)
    uint32_t remaining(uint32_t now_us) const {
        if (deadline_us == 0) return UINT32_MAX;
        uint32_t age = now_us - created_us;
        if (age >= deadline_us) return 0;
        return deadline_us - age;
    }
};

// Outbox entry: a pending delta with freshness tracking
struct OutboxEntry {
    uint32_t path_hash;
    uint8_t  crdt_type;
    uint8_t  reliability;
    FreshnessStamp freshness;
    std::vector<uint8_t> data;
    uint32_t target_sub_id;  // Which subscription this is for
    uint16_t target_conn_id;

    bool is_fresh(uint32_t now_us) const { return freshness.is_fresh(now_us); }
};

// DeltaOutbox: per-subscriber queue that respects freshness.
// Under congestion: stale deltas are dropped, only latest survives.
class DeltaOutbox {
public:
    // Enqueue a delta for delivery
    void push(OutboxEntry entry);

    // Pop the next fresh entry (drops expired ones). Returns false if empty.
    bool pop(OutboxEntry& out, uint32_t now_us);

    // Drop all stale entries
    size_t gc(uint32_t now_us);

    // Coalesce: for the same path_hash, keep only the latest entry.
    // This is the "skip intermediate versions" strategy.
    size_t coalesce();

    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

private:
    std::vector<OutboxEntry> entries_;
};

} // namespace protocoll
