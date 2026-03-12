#include <gtest/gtest.h>
#include "protocoll/wire/packet.h"
#include "protocoll/wire/frame_types.h"
#include <cstring>
#include <array>

using namespace protocoll;

TEST(PacketHeader, EncodeDecodeRoundTrip) {
    PacketHeader hdr{};
    hdr.version = PROTOCOL_VERSION;
    hdr.flags = FLAG_CHECKSUM;
    hdr.packet_type = PacketType::DATA;
    hdr.connection_id = 0x1234;
    hdr.packet_number = 42;
    hdr.timestamp_us = 1000000;
    hdr.payload_length = 128;
    hdr.checksum = 0xABCD;

    std::array<uint8_t, PACKET_HEADER_SIZE> buf{};
    ASSERT_TRUE(hdr.encode(buf.data(), buf.size()));

    PacketHeader decoded{};
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));

    EXPECT_EQ(decoded.version, PROTOCOL_VERSION);
    EXPECT_EQ(decoded.flags, FLAG_CHECKSUM);
    EXPECT_EQ(decoded.packet_type, PacketType::DATA);
    EXPECT_EQ(decoded.connection_id, 0x1234);
    EXPECT_EQ(decoded.packet_number, 42u);
    EXPECT_EQ(decoded.timestamp_us, 1000000u);
    EXPECT_EQ(decoded.payload_length, 128);
    EXPECT_EQ(decoded.checksum, 0xABCD);
}

TEST(PacketHeader, VersionFlagsPacking) {
    uint8_t vf = PacketHeader::make_version_flags(1, 0x0F);
    EXPECT_EQ(vf, 0x1F);
    EXPECT_EQ((vf >> 4) & 0x0F, 1);
    EXPECT_EQ(vf & 0x0F, 0x0F);
}

TEST(PacketHeader, DecodeBufferTooSmall) {
    PacketHeader hdr{};
    uint8_t buf[8] = {};
    EXPECT_FALSE(hdr.decode(buf, sizeof(buf)));
}

TEST(PacketHeader, EncodeBufferTooSmall) {
    PacketHeader hdr{};
    uint8_t buf[8] = {};
    EXPECT_FALSE(hdr.encode(buf, sizeof(buf)));
}

TEST(PacketHeader, ChecksumDeterministic) {
    uint8_t pkt[32] = {};
    pkt[0] = 0x18; // version=1, flags=CHECKSUM
    pkt[1] = 0x01; // DATA
    // Fill some payload bytes
    for (int i = PACKET_HEADER_SIZE; i < 32; i++) pkt[i] = static_cast<uint8_t>(i);

    uint16_t c1 = PacketHeader::compute_checksum(pkt, 32);
    uint16_t c2 = PacketHeader::compute_checksum(pkt, 32);
    EXPECT_EQ(c1, c2);
}

TEST(PacketHeader, ChecksumChangesWithData) {
    uint8_t pkt1[32] = {};
    uint8_t pkt2[32] = {};
    std::memset(pkt1 + PACKET_HEADER_SIZE, 0xAA, 16);
    std::memset(pkt2 + PACKET_HEADER_SIZE, 0xBB, 16);

    uint16_t c1 = PacketHeader::compute_checksum(pkt1, 32);
    uint16_t c2 = PacketHeader::compute_checksum(pkt2, 32);
    EXPECT_NE(c1, c2);
}

TEST(PacketHeader, BigEndianWireOrder) {
    PacketHeader hdr{};
    hdr.version = 1;
    hdr.flags = 0;
    hdr.packet_type = PacketType::DATA;
    hdr.connection_id = 0x0102;
    hdr.packet_number = 0x01020304;
    hdr.timestamp_us = 0;
    hdr.payload_length = 0;
    hdr.checksum = 0;

    std::array<uint8_t, PACKET_HEADER_SIZE> buf{};
    hdr.encode(buf.data(), buf.size());

    // connection_id at offset 2-3 should be big-endian: 0x01, 0x02
    EXPECT_EQ(buf[2], 0x01);
    EXPECT_EQ(buf[3], 0x02);

    // packet_number at offset 4-7 should be big-endian
    EXPECT_EQ(buf[4], 0x01);
    EXPECT_EQ(buf[5], 0x02);
    EXPECT_EQ(buf[6], 0x03);
    EXPECT_EQ(buf[7], 0x04);
}
