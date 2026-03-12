#pragma once

// SubscriptionManager: manages subscriptions from remote peers.
//
// Each subscription:
//   - path pattern (supports wildcards)
//   - subscriber ID (connection_id)
//   - credits remaining (backpressure)
//   - freshness deadline (microseconds; 0 = no deadline)
//
// When state changes, the manager filters which subscribers should
// receive the delta based on path matching and available credits.

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <unordered_map>

#include "protocoll/state/state_path.h"
#include "protocoll/state/resolution.h"
#include "protocoll/wire/frame_types.h"

namespace protocoll {

struct Subscription {
    uint32_t       sub_id;          // Unique subscription ID
    uint16_t       conn_id;         // Subscriber's connection ID
    StatePath      pattern;         // Path pattern (may contain wildcards)
    int64_t        credits;         // Remaining credits (-1 = unlimited)
    uint32_t       freshness_us;    // Max age for deltas (0 = no limit)
    ResolutionTier tier = ResolutionTier::FULL; // Resolution tier for this subscription

    bool has_credits() const { return credits != 0; }
    void consume_credit() { if (credits > 0) credits--; }
};

// Result of matching: which subscriptions should receive a given delta
struct MatchResult {
    uint32_t sub_id;
    uint16_t conn_id;
};

class SubscriptionManager {
public:
    // Add a subscription. Returns subscription ID.
    uint32_t subscribe(uint16_t conn_id, const StatePath& pattern,
                       int64_t initial_credits = -1, uint32_t freshness_us = 0);

    // Remove a subscription by ID. Returns true if found.
    bool unsubscribe(uint32_t sub_id);

    // Remove all subscriptions for a connection (on disconnect).
    void remove_connection(uint16_t conn_id);

    // Add credits to a subscription (backpressure replenishment).
    bool add_credits(uint32_t sub_id, int64_t credits);

    // Find all subscriptions matching a state path that have credits.
    // Consumes one credit from each matched subscription.
    std::vector<MatchResult> match_and_consume(const StatePath& path);

    // Find all matching subscriptions without consuming credits (read-only).
    std::vector<MatchResult> match(const StatePath& path) const;

    // Get subscription by ID
    Subscription* get(uint32_t sub_id);
    const Subscription* get(uint32_t sub_id) const;

    size_t count() const { return subs_.size(); }
    size_t count_for_connection(uint16_t conn_id) const;

    // Iterate all subscriptions
    template<typename Fn>
    void for_each(Fn fn) const {
        for (const auto& [id, sub] : subs_) fn(sub);
    }

private:
    uint32_t next_id_ = 1;
    std::unordered_map<uint32_t, Subscription> subs_;
};

// Wire format for STATE_SUBSCRIBE frame payload:
//   uint32_t sub_id
//   int64_t  initial_credits (as int32 on wire for compactness; -1 = unlimited)
//   uint32_t freshness_us
//   uint8_t  resolution_tier
//   uint16_t pattern_length
//   [pattern bytes]

struct SubscribeFramePayload {
    uint32_t sub_id;
    int32_t  initial_credits;
    uint32_t freshness_us;
    ResolutionTier tier = ResolutionTier::FULL;
    std::string pattern;

    bool encode(uint8_t* buf, size_t buf_len, size_t& written) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    size_t wire_size() const { return 15 + pattern.size(); }
};

struct UnsubscribeFramePayload {
    uint32_t sub_id;

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    static constexpr size_t WIRE_SIZE = 4;
};

struct CreditFramePayload {
    uint32_t sub_id;
    int32_t  credits;

    bool encode(uint8_t* buf, size_t buf_len) const;
    bool decode(const uint8_t* buf, size_t buf_len);
    static constexpr size_t WIRE_SIZE = 8;
};

} // namespace protocoll
