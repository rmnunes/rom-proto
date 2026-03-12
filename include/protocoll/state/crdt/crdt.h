#pragma once

/*
 * CRDT base interface. All CRDTs implement merge semantics.
 */

#include <cstdint>
#include <cstddef>
#include <vector>

namespace protocoll {

// Abstract CRDT interface
class Crdt {
public:
    virtual ~Crdt() = default;

    // Merge with incoming delta/state bytes. Returns true if local state changed.
    virtual bool merge(const uint8_t* data, size_t len) = 0;

    // Serialize current state to bytes
    virtual std::vector<uint8_t> snapshot() const = 0;

    // Serialize delta since last snapshot/delta (for differential propagation)
    // Returns empty if no changes since last call.
    virtual std::vector<uint8_t> delta() = 0;

    // Check if there are pending changes not yet captured by delta()
    virtual bool has_pending_delta() const = 0;
};

} // namespace protocoll
