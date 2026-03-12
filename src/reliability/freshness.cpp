#include "protocoll/reliability/freshness.h"
#include <algorithm>

namespace protocoll {

void DeltaOutbox::push(OutboxEntry entry) {
    entries_.push_back(std::move(entry));
}

bool DeltaOutbox::pop(OutboxEntry& out, uint32_t now_us) {
    while (!entries_.empty()) {
        auto& front = entries_.front();
        if (front.is_fresh(now_us)) {
            out = std::move(front);
            entries_.erase(entries_.begin());
            return true;
        }
        // Stale — drop it
        entries_.erase(entries_.begin());
    }
    return false;
}

size_t DeltaOutbox::gc(uint32_t now_us) {
    size_t before = entries_.size();
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
            [now_us](const OutboxEntry& e) { return !e.is_fresh(now_us); }),
        entries_.end()
    );
    return before - entries_.size();
}

size_t DeltaOutbox::coalesce() {
    if (entries_.size() <= 1) return 0;

    // For each path_hash, keep only the last entry
    // Walk backwards, mark first occurrence per path_hash
    std::unordered_map<uint32_t, size_t> latest; // path_hash -> index
    for (size_t i = entries_.size(); i > 0; i--) {
        uint32_t ph = entries_[i - 1].path_hash;
        if (latest.find(ph) == latest.end()) {
            latest[ph] = i - 1;
        }
    }

    size_t before = entries_.size();
    std::vector<OutboxEntry> kept;
    kept.reserve(latest.size());
    for (size_t i = 0; i < entries_.size(); i++) {
        uint32_t ph = entries_[i].path_hash;
        if (latest[ph] == i) {
            kept.push_back(std::move(entries_[i]));
        }
    }
    entries_ = std::move(kept);
    return before - entries_.size();
}

} // namespace protocoll
