#include "protocoll/state/resolution.h"
#include <cstring>
#include <algorithm>

namespace protocoll {

DeltaFilter::DeltaFilter(ResolutionTier tier, ResolutionConfig config)
    : tier_(tier), config_(config) {}

bool DeltaFilter::should_forward(uint32_t path_hash, const uint8_t* delta_data, size_t delta_len) {
    switch (tier_) {
        case ResolutionTier::FULL:
            deltas_forwarded_++;
            return true;

        case ResolutionTier::NORMAL:
            if (check_rate_limit(path_hash)) {
                deltas_forwarded_++;
                return true;
            }
            deltas_filtered_++;
            return false;

        case ResolutionTier::COARSE:
            if (check_change_threshold(path_hash, delta_data, delta_len)) {
                deltas_forwarded_++;
                return true;
            }
            deltas_filtered_++;
            return false;

        case ResolutionTier::METADATA:
            // METADATA tier never forwards delta payloads
            // Just mark that there's a pending version update
            metadata_states_[path_hash].has_pending_version = true;
            deltas_filtered_++;
            return false;
    }
    return false;
}

bool DeltaFilter::should_send_version_info(uint32_t path_hash) {
    if (tier_ != ResolutionTier::METADATA) return false;

    auto it = metadata_states_.find(path_hash);
    if (it == metadata_states_.end()) return false;

    if (it->second.has_pending_version) {
        it->second.has_pending_version = false;
        return true;
    }
    return false;
}

bool DeltaFilter::check_rate_limit(uint32_t path_hash) {
    auto now = std::chrono::steady_clock::now();
    auto& state = rate_states_[path_hash];

    if (state.sent_this_interval == 0) {
        // First send for this path
        state.last_sent = now;
        state.sent_this_interval = 1;
        return true;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state.last_sent).count();

    // Reset interval every second
    if (elapsed >= 1000) {
        state.last_sent = now;
        state.sent_this_interval = 1;
        return true;
    }

    // Within current second: check if under the limit
    if (state.sent_this_interval < config_.normal_max_updates_per_sec) {
        state.sent_this_interval++;
        return true;
    }

    return false;
}

bool DeltaFilter::check_change_threshold(uint32_t path_hash,
                                           const uint8_t* data, size_t len) {
    auto& state = coarse_states_[path_hash];

    if (state.last_delta.empty()) {
        // First delta — always forward
        state.last_delta.assign(data, data + len);
        return true;
    }

    // Count differing bytes
    size_t max_len = std::max(state.last_delta.size(), len);
    size_t min_len = std::min(state.last_delta.size(), len);
    size_t diff_bytes = max_len - min_len; // extra bytes are always different

    for (size_t i = 0; i < min_len; i++) {
        if (i < state.last_delta.size() && data[i] != state.last_delta[i]) {
            diff_bytes++;
        }
    }

    if (diff_bytes >= config_.coarse_min_change_bytes) {
        state.last_delta.assign(data, data + len);
        return true;
    }

    return false;
}

void DeltaFilter::reset() {
    rate_states_.clear();
    coarse_states_.clear();
    metadata_states_.clear();
    deltas_forwarded_ = 0;
    deltas_filtered_ = 0;
}

} // namespace protocoll
