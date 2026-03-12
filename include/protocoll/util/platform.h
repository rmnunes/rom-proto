#pragma once

/*
 * Platform abstraction: endianness, packed structs, compiler intrinsics.
 */

#include <cstdint>
#include <cstring>

#ifdef _MSC_VER
    #define PROTOCOLL_PACKED_BEGIN __pragma(pack(push, 1))
    #define PROTOCOLL_PACKED_END   __pragma(pack(pop))
    #include <stdlib.h>
    static inline uint16_t protocoll_bswap16(uint16_t v) { return _byteswap_ushort(v); }
    static inline uint32_t protocoll_bswap32(uint32_t v) { return _byteswap_ulong(v); }
    static inline uint64_t protocoll_bswap64(uint64_t v) { return _byteswap_uint64(v); }
#else
    #define PROTOCOLL_PACKED_BEGIN
    #define PROTOCOLL_PACKED_END
    #define PROTOCOLL_PACKED __attribute__((packed))
    static inline uint16_t protocoll_bswap16(uint16_t v) { return __builtin_bswap16(v); }
    static inline uint32_t protocoll_bswap32(uint32_t v) { return __builtin_bswap32(v); }
    static inline uint64_t protocoll_bswap64(uint64_t v) { return __builtin_bswap64(v); }
#endif

// Network byte order (big-endian) helpers
// x86/x64 is little-endian, so we always swap
namespace protocoll {

inline uint16_t host_to_net16(uint16_t v) { return protocoll_bswap16(v); }
inline uint16_t net_to_host16(uint16_t v) { return protocoll_bswap16(v); }
inline uint32_t host_to_net32(uint32_t v) { return protocoll_bswap32(v); }
inline uint32_t net_to_host32(uint32_t v) { return protocoll_bswap32(v); }
inline uint64_t host_to_net64(uint64_t v) { return protocoll_bswap64(v); }
inline uint64_t net_to_host64(uint64_t v) { return protocoll_bswap64(v); }

// Safe unaligned read/write
inline uint16_t read_u16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return net_to_host16(v);
}

inline uint32_t read_u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return net_to_host32(v);
}

inline void write_u16(uint8_t* p, uint16_t v) {
    v = host_to_net16(v);
    std::memcpy(p, &v, 2);
}

inline void write_u32(uint8_t* p, uint32_t v) {
    v = host_to_net32(v);
    std::memcpy(p, &v, 4);
}

} // namespace protocoll
