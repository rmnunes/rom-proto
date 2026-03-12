#pragma once

// PN-Counter (Positive-Negative Counter)
//
// Two G-Counters: one for increments (P), one for decrements (N).
// Value = sum(P) - sum(N). Merge = merge each G-Counter independently.
//
// Wire format: [uint8_t p_count][p entries...][uint8_t n_count][n entries...]
// Each entry: uint16_t node_id + uint64_t count (10 bytes)

#include "protocoll/state/crdt/crdt.h"
#include "protocoll/state/crdt/g_counter.h"
#include <cstdint>

namespace protocoll {

class PnCounter : public Crdt {
public:
    PnCounter() = default;
    explicit PnCounter(uint16_t node_id);

    void increment(uint64_t amount = 1);
    void decrement(uint64_t amount = 1);

    // Value can be negative (returned as int64_t)
    int64_t value() const;

    // CRDT interface
    bool merge(const uint8_t* data, size_t len) override;
    std::vector<uint8_t> snapshot() const override;
    std::vector<uint8_t> delta() override;
    bool has_pending_delta() const override;

private:
    GCounter p_; // positive
    GCounter n_; // negative
};

} // namespace protocoll
