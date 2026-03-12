#include <gtest/gtest.h>
#include "protocoll/reliability/fec.h"
#include <cstring>

using namespace protocoll;

// --- FecHeader ---

TEST(FecHeader, RoundTrip) {
    FecHeader hdr{42, 4, 2};
    uint8_t buf[FecHeader::WIRE_SIZE];
    ASSERT_TRUE(hdr.encode(buf, sizeof(buf)));

    FecHeader decoded;
    ASSERT_TRUE(decoded.decode(buf, sizeof(buf)));
    EXPECT_EQ(decoded.group_id, 42);
    EXPECT_EQ(decoded.group_size, 4);
    EXPECT_EQ(decoded.index, 2);
}

// --- FecEncoder ---

TEST(FecEncoder, ProducesParityAfterGroupSize) {
    FecEncoder enc(4);
    uint8_t data[] = {0xAA};

    EXPECT_FALSE(enc.add_packet(data, 1).has_value());
    EXPECT_FALSE(enc.add_packet(data, 1).has_value());
    EXPECT_FALSE(enc.add_packet(data, 1).has_value());

    auto parity = enc.add_packet(data, 1);
    ASSERT_TRUE(parity.has_value());

    // Parity should start with FecHeader
    EXPECT_GE(parity->size(), FecHeader::WIRE_SIZE + 1);
}

TEST(FecEncoder, ParityIsXorOfData) {
    FecEncoder enc(2);

    uint8_t a[] = {0b10101010};
    uint8_t b[] = {0b11001100};

    enc.add_packet(a, 1);
    auto parity = enc.add_packet(b, 1);
    ASSERT_TRUE(parity.has_value());

    // XOR of a ^ b = 0b01100110
    EXPECT_EQ((*parity)[FecHeader::WIRE_SIZE], 0b10101010 ^ 0b11001100);
}

TEST(FecEncoder, FlushIncompleteGroup) {
    FecEncoder enc(4);
    uint8_t data[] = {0x42};
    enc.add_packet(data, 1);
    enc.add_packet(data, 1);

    auto parity = enc.flush();
    ASSERT_TRUE(parity.has_value());

    // Header should indicate group_size = 2 (actual count)
    FecHeader hdr;
    hdr.decode(parity->data(), parity->size());
    EXPECT_EQ(hdr.group_size, 2);
    EXPECT_EQ(hdr.index, 2); // parity
}

TEST(FecEncoder, FlushEmptyReturnsNothing) {
    FecEncoder enc(4);
    EXPECT_FALSE(enc.flush().has_value());
}

TEST(FecEncoder, GroupIdIncrements) {
    FecEncoder enc(2);
    uint8_t data[] = {1};

    EXPECT_EQ(enc.current_group_id(), 0);
    enc.add_packet(data, 1);
    enc.add_packet(data, 1);
    EXPECT_EQ(enc.current_group_id(), 1);
    enc.add_packet(data, 1);
    enc.add_packet(data, 1);
    EXPECT_EQ(enc.current_group_id(), 2);
}

TEST(FecEncoder, Reset) {
    FecEncoder enc(2);
    uint8_t data[] = {1};
    enc.add_packet(data, 1);
    enc.reset();
    EXPECT_EQ(enc.current_group_id(), 0);
}

// --- FecDecoder ---

TEST(FecDecoder, AllDataReceivedNoRecovery) {
    FecDecoder dec(2);

    FecHeader h0{0, 2, 0};
    FecHeader h1{0, 2, 1};
    uint8_t d0[] = {0xAA};
    uint8_t d1[] = {0xBB};

    EXPECT_FALSE(dec.add_packet(h0, d0, 1).has_value());
    EXPECT_FALSE(dec.add_packet(h1, d1, 1).has_value());
}

TEST(FecDecoder, RecoversMissingPacket) {
    // Group of 2: packets [0xAA] and [0xBB]
    // Parity = 0xAA ^ 0xBB = 0x11
    // Lose packet 1, recover it from packet 0 + parity

    FecDecoder dec(2);

    FecHeader h0{0, 2, 0};
    uint8_t d0[] = {0xAA};
    EXPECT_FALSE(dec.add_packet(h0, d0, 1).has_value());

    // Parity packet (index = group_size = 2)
    FecHeader hp{0, 2, 2};
    uint8_t parity[] = {0xAA ^ 0xBB};

    auto recovered = dec.add_packet(hp, parity, 1);
    ASSERT_TRUE(recovered.has_value());
    ASSERT_EQ(recovered->size(), 1u);
    EXPECT_EQ((*recovered)[0], 0xBB);
}

TEST(FecDecoder, RecoversFirstPacket) {
    // Lose packet 0, have packet 1 + parity
    FecDecoder dec(2);

    FecHeader h1{0, 2, 1};
    uint8_t d1[] = {0xBB};
    EXPECT_FALSE(dec.add_packet(h1, d1, 1).has_value());

    FecHeader hp{0, 2, 2};
    uint8_t parity[] = {0xAA ^ 0xBB};

    auto recovered = dec.add_packet(hp, parity, 1);
    ASSERT_TRUE(recovered.has_value());
    EXPECT_EQ((*recovered)[0], 0xAA);
}

TEST(FecDecoder, MultiByteRecovery) {
    // 3-byte packets, group of 2
    uint8_t d0[] = {0x01, 0x02, 0x03};
    uint8_t d1[] = {0x10, 0x20, 0x30};
    uint8_t parity[] = {0x01 ^ 0x10, 0x02 ^ 0x20, 0x03 ^ 0x30};

    FecDecoder dec(2);

    FecHeader h0{0, 2, 0};
    dec.add_packet(h0, d0, 3);

    FecHeader hp{0, 2, 2};
    auto recovered = dec.add_packet(hp, parity, 3);

    ASSERT_TRUE(recovered.has_value());
    ASSERT_EQ(recovered->size(), 3u);
    EXPECT_EQ((*recovered)[0], 0x10);
    EXPECT_EQ((*recovered)[1], 0x20);
    EXPECT_EQ((*recovered)[2], 0x30);
}

TEST(FecDecoder, CannotRecoverTwoMissing) {
    // Group of 3, only have packet 0 + parity — missing 2 packets, can't recover
    FecDecoder dec(3);

    FecHeader h0{0, 3, 0};
    uint8_t d0[] = {0xAA};
    EXPECT_FALSE(dec.add_packet(h0, d0, 1).has_value());

    FecHeader hp{0, 3, 3};
    uint8_t parity[] = {0xFF};
    EXPECT_FALSE(dec.add_packet(hp, parity, 1).has_value());
}

TEST(FecDecoder, FullRoundTrip) {
    // Encode 4 packets, decode with one missing
    FecEncoder enc(4);
    uint8_t packets[4][4] = {
        {0x11, 0x22, 0x33, 0x44},
        {0xAA, 0xBB, 0xCC, 0xDD},
        {0x01, 0x02, 0x03, 0x04},
        {0xFF, 0xFE, 0xFD, 0xFC},
    };

    std::vector<uint8_t> parity_pkt;
    for (int i = 0; i < 4; i++) {
        auto result = enc.add_packet(packets[i], 4);
        if (result) parity_pkt = *result;
    }
    ASSERT_FALSE(parity_pkt.empty());

    // Decode: lose packet 2
    FecDecoder dec(4);
    FecHeader h;
    h.group_id = 0;
    h.group_size = 4;

    h.index = 0; dec.add_packet(h, packets[0], 4);
    h.index = 1; dec.add_packet(h, packets[1], 4);
    // Skip packet 2
    h.index = 3; dec.add_packet(h, packets[3], 4);

    // Add parity
    FecHeader ph;
    ph.decode(parity_pkt.data(), parity_pkt.size());
    auto recovered = dec.add_packet(ph,
        parity_pkt.data() + FecHeader::WIRE_SIZE,
        parity_pkt.size() - FecHeader::WIRE_SIZE);

    ASSERT_TRUE(recovered.has_value());
    ASSERT_EQ(recovered->size(), 4u);
    EXPECT_EQ((*recovered)[0], 0x01);
    EXPECT_EQ((*recovered)[1], 0x02);
    EXPECT_EQ((*recovered)[2], 0x03);
    EXPECT_EQ((*recovered)[3], 0x04);
}
