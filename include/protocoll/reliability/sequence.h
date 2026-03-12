#pragma once

/*
 * Sequence number utilities for packet tracking.
 * Handles wrap-around comparison for uint32_t sequence numbers.
 */

#include <cstdint>

namespace protocoll {

// Serial number arithmetic (RFC 1982 style)
// Returns true if a is "before" b (accounting for wrap-around)
inline bool seq_lt(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) < 0;
}

inline bool seq_le(uint32_t a, uint32_t b) {
    return a == b || seq_lt(a, b);
}

inline bool seq_gt(uint32_t a, uint32_t b) {
    return seq_lt(b, a);
}

inline bool seq_ge(uint32_t a, uint32_t b) {
    return a == b || seq_gt(a, b);
}

} // namespace protocoll
