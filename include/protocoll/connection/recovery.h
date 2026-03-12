#pragma once

// Recovery: handles reconnection with minimal state re-sync.
//
// When a peer reconnects, it sends its last-known version vectors.
// The server computes the diff and sends either:
//   - Deltas since that version (if diff is small)
//   - Snapshot + recent deltas (if version is too old)
//
// This is analogous to video: I-frame (snapshot) when you need full
// state, P-frames (deltas) for incremental updates.

#include <cstdint>
#include <vector>
#include <unordered_map>

#include "protocoll/state/version_vector.h"
#include "protocoll/state/event_log.h"
#include "protocoll/state/state_registry.h"

namespace protocoll {

struct RecoveryRequest {
    // Per-path version vectors: "here's what I have"
    std::unordered_map<uint32_t, VersionVector> known_versions;

    // Wire encode/decode
    std::vector<uint8_t> encode() const;
    static bool decode(const uint8_t* data, size_t len, RecoveryRequest& out);
};

struct RecoveryPlan {
    // Per-path: either send deltas or full snapshot
    struct PathRecovery {
        uint32_t path_hash;
        bool     needs_snapshot;  // true = send full state, false = send deltas
        std::vector<const LogEntry*> entries; // deltas to send (if !needs_snapshot)
    };

    std::vector<PathRecovery> paths;
};

class RecoveryManager {
public:
    // Compute a recovery plan: what to send to a reconnecting peer.
    // Uses the event log and current state registry to determine
    // the minimal set of data needed.
    //
    // max_delta_entries: if a path needs more than this many deltas,
    //                    send a snapshot instead.
    static RecoveryPlan compute_plan(
        const RecoveryRequest& request,
        const EventLog& log,
        const StateRegistry& registry,
        size_t max_delta_entries = 50
    );

    // Build a recovery request from a state registry (client side).
    // Captures current version vectors for all declared state regions.
    static RecoveryRequest build_request(const StateRegistry& registry);
};

} // namespace protocoll
