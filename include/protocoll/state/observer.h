#pragma once

// StateObserver: reactive local observation of state changes.
//
// While SubscriptionManager handles remote peer subscriptions over the wire,
// StateObserver provides a local reactive API for application code:
//
//   auto handle = observer.watch("/game/player/*/pos", [](auto& event) {
//       printf("Player moved: %s\n", event.path.str().c_str());
//   });
//   // ...later:
//   observer.unwatch(handle);
//
// Observers can:
//   - Watch exact paths or wildcard patterns
//   - Filter by CRDT type
//   - Batch notifications (debounce)
//   - Auto-remove after N notifications (one-shot, N-shot)
//
// Thread safety: single-threaded (same thread as Peer::poll).

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include <unordered_map>
#include <optional>

#include "protocoll/state/state_path.h"
#include "protocoll/wire/frame_types.h"

namespace protocoll {

// Handle returned by watch()
using WatchHandle = uint32_t;

// Event delivered to watchers
struct StateChangeEvent {
    const StatePath& path;
    uint32_t path_hash;
    CrdtType crdt_type;
    const uint8_t* data;     // New value (CRDT-specific encoding)
    size_t data_len;
    uint16_t author_node_id; // Who made the change (0 = local)
};

// Configuration for a watch
struct WatchOptions {
    std::optional<CrdtType> crdt_filter;  // Only notify for this CRDT type
    int32_t max_notifications = -1;       // -1 = unlimited, 1 = one-shot, N = N-shot
};

using WatchCallback = std::function<void(const StateChangeEvent&)>;

class StateObserver {
public:
    // Watch a path pattern. Returns a handle for later removal.
    WatchHandle watch(const StatePath& pattern, WatchCallback cb,
                      WatchOptions opts = {});

    // Remove a watch.
    bool unwatch(WatchHandle handle);

    // Notify all matching watchers of a state change.
    // Called internally by Peer/StateRegistry when state is updated.
    void notify(const StateChangeEvent& event);

    // Number of active watches.
    size_t count() const { return watches_.size(); }

    // Remove all watches.
    void clear();

private:
    struct WatchEntry {
        WatchHandle handle;
        StatePath pattern;
        WatchCallback callback;
        WatchOptions options;
        int32_t remaining;  // Decremented on each notification
    };

    WatchHandle next_handle_ = 1;
    std::unordered_map<WatchHandle, WatchEntry> watches_;

    // Watches scheduled for removal after notification (expired N-shot)
    std::vector<WatchHandle> pending_removal_;

    void cleanup_expired();
};

} // namespace protocoll
