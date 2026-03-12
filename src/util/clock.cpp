#include "protocoll/util/clock.h"
#include <chrono>

namespace protocoll {

uint32_t now_us() {
    auto now = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()
    ).count();
    return static_cast<uint32_t>(us & 0xFFFFFFFF);
}

} // namespace protocoll
