#pragma once

/*
 * VersionVector: tracks causal ordering across nodes.
 * Each entry: {node_id -> sequence_number}.
 *
 * Wire format: uint8_t entry_count, then entry_count * (uint16_t node_id, uint32_t seq)
 * = 1 + 6*N bytes
 */

#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>

namespace protocoll {

struct VersionEntry {
    uint16_t node_id;
    uint32_t sequence;
};

class VersionVector {
public:
    VersionVector() = default;

    // Increment this node's version. Returns the new sequence number.
    uint32_t increment(uint16_t node_id);

    // Get sequence for a node (0 if not present)
    uint32_t get(uint16_t node_id) const;

    // Set sequence for a node
    void set(uint16_t node_id, uint32_t seq);

    // Merge with another version vector (take max per node)
    void merge(const VersionVector& other);

    // Comparison
    // Returns true if this vector dominates (>=) the other on all entries
    bool dominates(const VersionVector& other) const;

    // Returns true if vectors are concurrent (neither dominates)
    bool concurrent_with(const VersionVector& other) const;

    // Returns true if this vector is strictly less than other
    bool less_than(const VersionVector& other) const;

    bool operator==(const VersionVector& other) const;

    // Wire encoding
    size_t wire_size() const { return 1 + entries_.size() * 6; }
    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);

    const std::vector<VersionEntry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }

private:
    std::vector<VersionEntry> entries_;

    VersionEntry* find(uint16_t node_id);
    const VersionEntry* find(uint16_t node_id) const;
};

} // namespace protocoll
