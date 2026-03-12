#pragma once

// StateRegistry: manages declared state regions and their CRDTs.
// Maps path_hash -> CRDT instance. Handles incoming deltas/snapshots
// by dispatching to the correct CRDT for merge.

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>

#include "protocoll/wire/frame_types.h"
#include "protocoll/state/state_path.h"
#include "protocoll/state/version_vector.h"
#include "protocoll/state/crdt/crdt.h"
#include "protocoll/state/crdt/lww_register.h"
#include "protocoll/state/crdt/g_counter.h"

namespace protocoll {

struct StateRegion {
    StatePath path;
    CrdtType crdt_type;
    Reliability reliability;
    std::unique_ptr<Crdt> crdt;
    VersionVector version;
};

// Callback when a state region is updated (for reactive notifications)
using StateUpdateCallback = std::function<void(const StatePath& path, const uint8_t* data, size_t len)>;

class StateRegistry {
public:
    explicit StateRegistry(uint16_t node_id);

    // Declare a state region with a CRDT type. Creates the CRDT instance.
    bool declare(const StatePath& path, CrdtType type, Reliability rel);

    // Get a region by path hash
    StateRegion* get(uint32_t path_hash);
    const StateRegion* get(uint32_t path_hash) const;

    // Get by path
    StateRegion* get(const StatePath& path) { return get(path.hash()); }

    // Apply an incoming delta to the appropriate CRDT.
    // Returns true if state changed.
    bool apply_delta(uint32_t path_hash, const uint8_t* data, size_t len);

    // Apply an incoming snapshot
    bool apply_snapshot(uint32_t path_hash, const uint8_t* data, size_t len);

    // Collect deltas from all dirty regions.
    // Returns list of (path_hash, delta_bytes) pairs.
    struct PendingDelta {
        uint32_t path_hash;
        CrdtType crdt_type;
        Reliability reliability;
        std::vector<uint8_t> data;
    };
    std::vector<PendingDelta> collect_deltas();

    // Set callback for state updates
    void set_update_callback(StateUpdateCallback cb) { on_update_ = std::move(cb); }

    uint16_t node_id() const { return node_id_; }
    size_t region_count() const { return regions_.size(); }

    // Iterate all regions
    template<typename Fn>
    void for_each(Fn fn) {
        for (auto& [hash, region] : regions_) {
            fn(region);
        }
    }

private:
    uint16_t node_id_;
    std::unordered_map<uint32_t, StateRegion> regions_;
    StateUpdateCallback on_update_;

    std::unique_ptr<Crdt> create_crdt(CrdtType type);
};

} // namespace protocoll
