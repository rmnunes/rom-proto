#include "protocoll/util/hash.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

namespace protocoll {

uint16_t xxhash16(const uint8_t* data, size_t len) {
    uint32_t h = XXH32(data, len, 0);
    return static_cast<uint16_t>(h & 0xFFFF);
}

uint32_t xxhash32(const uint8_t* data, size_t len) {
    return XXH32(data, len, 0);
}

uint32_t xxhash32(std::string_view s) {
    return XXH32(s.data(), s.size(), 0);
}

} // namespace protocoll
