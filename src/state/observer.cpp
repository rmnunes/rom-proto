#include "protocoll/state/observer.h"

namespace protocoll {

WatchHandle StateObserver::watch(const StatePath& pattern, WatchCallback cb,
                                  WatchOptions opts) {
    WatchHandle h = next_handle_++;
    WatchEntry entry;
    entry.handle = h;
    entry.pattern = pattern;
    entry.callback = std::move(cb);
    entry.options = std::move(opts);
    entry.remaining = opts.max_notifications;
    watches_.emplace(h, std::move(entry));
    return h;
}

bool StateObserver::unwatch(WatchHandle handle) {
    return watches_.erase(handle) > 0;
}

void StateObserver::notify(const StateChangeEvent& event) {
    pending_removal_.clear();

    for (auto& [handle, entry] : watches_) {
        // Check CRDT type filter
        if (entry.options.crdt_filter.has_value() &&
            entry.options.crdt_filter.value() != event.crdt_type) {
            continue;
        }

        // Check path pattern match: path.matches(pattern)
        StatePath event_path_copy(event.path);
        if (!event_path_copy.matches(entry.pattern)) {
            continue;
        }

        // Check remaining notifications
        if (entry.remaining == 0) {
            pending_removal_.push_back(handle);
            continue;
        }

        // Fire callback
        entry.callback(event);

        // Decrement remaining (if not unlimited)
        if (entry.remaining > 0) {
            entry.remaining--;
            if (entry.remaining == 0) {
                pending_removal_.push_back(handle);
            }
        }
    }

    cleanup_expired();
}

void StateObserver::cleanup_expired() {
    for (auto h : pending_removal_) {
        watches_.erase(h);
    }
    pending_removal_.clear();
}

void StateObserver::clear() {
    watches_.clear();
    pending_removal_.clear();
}

} // namespace protocoll
