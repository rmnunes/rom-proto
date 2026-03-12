#pragma once

// Multi-resolution propagation: subscribers receive different
// granularity of state updates based on their resolution tier.
//
// ResolutionTier:
//   FULL     — every delta, full precision
//   NORMAL   — rate-limited (max N updates/sec per path)
//   COARSE   — threshold-based (only significant changes)
//   METADATA — version vectors only (for pull-on-demand)
//
// DeltaFilter sits between delta generation and dispatch in Peer::flush().

#include <cstdint>
#include <cstddef>
#include <chrono>
#include <unordered_map>
#include <vector>

namespace protocoll {

enum class ResolutionTier : uint8_t {
    FULL     = 0,   // Every delta, full precision
    NORMAL   = 1,   // Rate-limited (max N updates/sec per path)
    COARSE   = 2,   // Threshold-based (only significant changes)
    METADATA = 3,   // Version vectors only (pull-on-demand)
};

struct ResolutionConfig {
    // NORMAL tier: max updates per second per path
    uint32_t normal_max_updates_per_sec = 10;

    // COARSE tier: minimum byte change threshold to emit
    size_t coarse_min_change_bytes = 4;
};

// Tracks per-path rate-limiting state for a single subscriber
class DeltaFilter {
public:
    explicit DeltaFilter(ResolutionTier tier = ResolutionTier::FULL,
                          ResolutionConfig config = {});

    // Returns true if this delta should be forwarded to the subscriber.
    // path_hash: identifies the state path
    // delta_data: the delta payload
    // delta_len: byte length of delta
    bool should_forward(uint32_t path_hash, const uint8_t* delta_data, size_t delta_len);

    // For METADATA tier: check if version changed (no payload forwarding)
    // Returns true if version info should be sent
    bool should_send_version_info(uint32_t path_hash);

    ResolutionTier tier() const { return tier_; }
    void set_tier(ResolutionTier tier) { tier_ = tier; }

    const ResolutionConfig& config() const { return config_; }
    void set_config(const ResolutionConfig& config) { config_ = config; }

    // Reset all rate-limiting state
    void reset();

    // Statistics
    uint64_t deltas_forwarded() const { return deltas_forwarded_; }
    uint64_t deltas_filtered() const { return deltas_filtered_; }

private:
    ResolutionTier tier_;
    ResolutionConfig config_;

    // Per-path tracking for NORMAL tier (rate limiting)
    struct PathRateState {
        std::chrono::steady_clock::time_point last_sent;
        uint32_t sent_this_interval = 0;
    };
    std::unordered_map<uint32_t, PathRateState> rate_states_;

    // Per-path tracking for COARSE tier (change detection)
    struct PathCoarseState {
        std::vector<uint8_t> last_delta;
    };
    std::unordered_map<uint32_t, PathCoarseState> coarse_states_;

    // Per-path tracking for METADATA tier
    struct PathMetadataState {
        bool has_pending_version = false;
    };
    std::unordered_map<uint32_t, PathMetadataState> metadata_states_;

    uint64_t deltas_forwarded_ = 0;
    uint64_t deltas_filtered_ = 0;

    bool check_rate_limit(uint32_t path_hash);
    bool check_change_threshold(uint32_t path_hash, const uint8_t* data, size_t len);
};

} // namespace protocoll
