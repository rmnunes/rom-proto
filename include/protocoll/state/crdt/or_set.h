#pragma once

// OR-Set (Observed-Remove Set)
//
// Add-wins semantics: concurrent add + remove → element is present.
// Each element tagged with unique IDs. Remove only removes observed tags.
//
// Internal: map<element_bytes, set<tag>>
// Tag = {node_id, sequence} — globally unique, monotonic per node.
//
// Wire format (snapshot/delta):
//   uint16_t entry_count
//   For each entry:
//     uint16_t element_length
//     [element bytes]
//     uint8_t  tag_count
//     For each tag:
//       uint16_t node_id
//       uint32_t sequence
//   uint16_t tombstone_count    (tags to remove)
//   For each tombstone:
//     uint16_t node_id
//     uint32_t sequence

#include "protocoll/state/crdt/crdt.h"
#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <string>

namespace protocoll {

struct OrSetTag {
    uint16_t node_id;
    uint32_t sequence;

    bool operator<(const OrSetTag& o) const {
        if (node_id != o.node_id) return node_id < o.node_id;
        return sequence < o.sequence;
    }
    bool operator==(const OrSetTag& o) const {
        return node_id == o.node_id && sequence == o.sequence;
    }
};

class OrSet : public Crdt {
public:
    OrSet() = default;
    explicit OrSet(uint16_t node_id);

    // Add an element. Returns true.
    bool add(const uint8_t* elem, size_t len);
    bool add(std::string_view s) {
        return add(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    // Remove an element (observed-remove). Returns true if it was present.
    bool remove(const uint8_t* elem, size_t len);
    bool remove(std::string_view s) {
        return remove(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    // Check membership
    bool contains(const uint8_t* elem, size_t len) const;
    bool contains(std::string_view s) const {
        return contains(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    // Get all elements
    std::vector<std::vector<uint8_t>> elements() const;
    size_t size() const;

    // CRDT interface
    bool merge(const uint8_t* data, size_t len) override;
    std::vector<uint8_t> snapshot() const override;
    std::vector<uint8_t> delta() override;
    bool has_pending_delta() const override { return dirty_; }

private:
    using ElemKey = std::vector<uint8_t>;

    uint16_t node_id_ = 0;
    uint32_t seq_ = 0;  // Monotonic tag sequence for this node

    // element -> set of tags
    std::map<ElemKey, std::set<OrSetTag>> entries_;

    // Delta tracking
    bool dirty_ = false;
    std::map<ElemKey, std::set<OrSetTag>> pending_adds_;
    std::set<OrSetTag> pending_removes_;

    OrSetTag next_tag() { return {node_id_, ++seq_}; }

    // Wire helpers
    static void encode_entries(const std::map<ElemKey, std::set<OrSetTag>>& entries,
                               const std::set<OrSetTag>& tombstones,
                               std::vector<uint8_t>& buf);
    struct Decoded {
        std::map<ElemKey, std::set<OrSetTag>> entries;
        std::set<OrSetTag> tombstones;
    };
    static bool decode_entries(const uint8_t* data, size_t len, Decoded& out);
};

} // namespace protocoll
