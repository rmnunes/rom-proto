#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace protocoll {

// xxHash32-based hashing, truncated to 16-bit for checksums
uint16_t xxhash16(const uint8_t* data, size_t len);

// Full xxHash32 for state path hashing
uint32_t xxhash32(const uint8_t* data, size_t len);
uint32_t xxhash32(std::string_view s);

} // namespace protocoll
