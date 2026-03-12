#include "protocoll/wire/packet.h"
#include "protocoll/util/platform.h"
#include "protocoll/util/hash.h"
#include <cstring>

namespace protocoll {

bool PacketHeader::encode(uint8_t* buf, size_t buf_len) const {
    if (buf_len < PACKET_HEADER_SIZE) return false;

    buf[0] = make_version_flags(version, flags);
    buf[1] = static_cast<uint8_t>(packet_type);
    write_u16(buf + 2, connection_id);
    write_u32(buf + 4, packet_number);
    write_u32(buf + 8, timestamp_us);
    write_u16(buf + 12, payload_length);
    write_u16(buf + 14, checksum);

    return true;
}

bool PacketHeader::decode(const uint8_t* buf, size_t buf_len) {
    if (buf_len < PACKET_HEADER_SIZE) return false;

    uint8_t vf = buf[0];
    version = (vf >> 4) & 0x0F;
    flags   = vf & 0x0F;
    packet_type = static_cast<PacketType>(buf[1]);
    connection_id = read_u16(buf + 2);
    packet_number = read_u32(buf + 4);
    timestamp_us  = read_u32(buf + 8);
    payload_length = read_u16(buf + 12);
    checksum = read_u16(buf + 14);

    return true;
}

uint16_t PacketHeader::compute_checksum(const uint8_t* packet_buf, size_t total_len) {
    if (total_len < PACKET_HEADER_SIZE) return 0;

    // Copy header, zero the checksum field (bytes 14-15), hash everything
    uint8_t tmp[PACKET_HEADER_SIZE];
    std::memcpy(tmp, packet_buf, PACKET_HEADER_SIZE);
    tmp[14] = 0;
    tmp[15] = 0;

    // Hash header + payload in two steps via xxHash streaming would be ideal,
    // but for simplicity we'll hash header then XOR with payload hash
    uint16_t h1 = xxhash16(tmp, PACKET_HEADER_SIZE);
    if (total_len > PACKET_HEADER_SIZE) {
        uint16_t h2 = xxhash16(packet_buf + PACKET_HEADER_SIZE,
                                total_len - PACKET_HEADER_SIZE);
        return h1 ^ h2;
    }
    return h1;
}

} // namespace protocoll
