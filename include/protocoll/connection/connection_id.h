#pragma once

#include <cstdint>
#include <atomic>

namespace protocoll {

// Thread-safe connection ID allocator
class ConnectionIdAllocator {
public:
    uint16_t next() {
        uint16_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
        // Skip 0 (reserved for unassigned/handshake)
        if (id == 0) id = next_id_.fetch_add(1, std::memory_order_relaxed);
        return id;
    }

private:
    std::atomic<uint16_t> next_id_{1};
};

} // namespace protocoll
