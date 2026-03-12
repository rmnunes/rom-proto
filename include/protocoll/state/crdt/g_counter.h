#pragma once

/*
 * G-Counter (Grow-only Counter)
 *
 * Each node maintains its own count. Total = sum of all counts.
 * Merge = max per node.
 *
 * Wire format: uint8_t entry_count, then entry_count * (uint16_t node_id, uint64_t count)
 * = 1 + 10*N bytes
 *
 * Can also send delta: only the incremented node's entry.
 */

#include "protocoll/state/crdt/crdt.h"
#include <cstdint>
#include <vector>

namespace protocoll {

struct GCounterEntry {
    uint16_t node_id;
    uint64_t count;
};

class GCounter : public Crdt {
public:
    GCounter() = default;
    explicit GCounter(uint16_t node_id);

    // Increment this node's count by `amount`
    void increment(uint64_t amount = 1);

    // Get total count (sum of all nodes)
    uint64_t value() const;

    // Get this node's count
    uint64_t local_count() const;

    // CRDT interface
    bool merge(const uint8_t* data, size_t len) override;
    std::vector<uint8_t> snapshot() const override;
    std::vector<uint8_t> delta() override;
    bool has_pending_delta() const override { return dirty_; }

    const std::vector<GCounterEntry>& entries() const { return entries_; }

    static constexpr size_t ENTRY_WIRE_SIZE = 10; // 2 (node_id) + 8 (count)

private:
    uint16_t node_id_ = 0;
    std::vector<GCounterEntry> entries_;
    bool dirty_ = false;

    GCounterEntry* find(uint16_t node_id);
    const GCounterEntry* find(uint16_t node_id) const;
};

} // namespace protocoll
