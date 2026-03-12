#include <gtest/gtest.h>
#include "protocoll/wire/codec.h"
#include "protocoll/wire/frame.h"
#include "protocoll/wire/frame_types.h"
#include <cstring>

using namespace protocoll;

TEST(PacketEncoder, EmptyPacket) {
    PacketEncoder enc;
    enc.set_packet_type(PacketType::CONTROL);
    enc.set_connection_id(1);
    enc.set_packet_number(0);

    auto pkt = enc.finalize();
    ASSERT_EQ(pkt.size(), PACKET_HEADER_SIZE); // No frames = header only

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));
    EXPECT_EQ(dec.header().packet_type, PacketType::CONTROL);
    EXPECT_EQ(dec.header().connection_id, 1);
    EXPECT_EQ(dec.header().payload_length, 0);
    EXPECT_TRUE(dec.verify_checksum());
}

TEST(PacketCodec, SingleFrameRoundTrip) {
    // Encode a CONNECT frame inside a packet
    ConnectFrame cf{CONNECT_MAGIC, PROTOCOL_VERSION, 1400};
    uint8_t frame_buf[ConnectFrame::WIRE_SIZE];
    ASSERT_TRUE(cf.encode(frame_buf, sizeof(frame_buf)));

    PacketEncoder enc;
    enc.set_packet_type(PacketType::HANDSHAKE);
    enc.set_connection_id(0);
    enc.set_packet_number(1);
    enc.set_timestamp(5000);
    ASSERT_TRUE(enc.add_frame(FrameType::CONNECT, frame_buf, ConnectFrame::WIRE_SIZE));

    auto pkt = enc.finalize();
    ASSERT_EQ(pkt.size(), PACKET_HEADER_SIZE + FRAME_HEADER_SIZE + ConnectFrame::WIRE_SIZE);

    // Decode
    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));
    EXPECT_TRUE(dec.verify_checksum());
    EXPECT_EQ(dec.header().packet_type, PacketType::HANDSHAKE);
    EXPECT_EQ(dec.header().packet_number, 1u);
    EXPECT_EQ(dec.header().timestamp_us, 5000u);

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_EQ(frame.header.type, FrameType::CONNECT);
    EXPECT_EQ(frame.header.length, ConnectFrame::WIRE_SIZE);

    ConnectFrame decoded_cf{};
    ASSERT_TRUE(decoded_cf.decode(frame.payload, frame.header.length));
    EXPECT_EQ(decoded_cf.magic, CONNECT_MAGIC);
    EXPECT_EQ(decoded_cf.version, PROTOCOL_VERSION);
    EXPECT_EQ(decoded_cf.max_frame_size, 1400);

    // No more frames
    EXPECT_FALSE(dec.next_frame(frame));
}

TEST(PacketCodec, MultipleFramesRoundTrip) {
    PacketEncoder enc;
    enc.set_packet_type(PacketType::DATA);
    enc.set_connection_id(42);
    enc.set_packet_number(100);

    // Add a StateDelta frame
    StateDeltaFrame delta{0xAABBCCDD, CrdtType::LWW_REGISTER, Reliability::RELIABLE, 1};
    uint8_t delta_buf[StateDeltaFrame::BASE_WIRE_SIZE];
    ASSERT_TRUE(delta.encode(delta_buf, sizeof(delta_buf)));
    ASSERT_TRUE(enc.add_frame(FrameType::STATE_DELTA, delta_buf, StateDeltaFrame::BASE_WIRE_SIZE));

    // Add a Ping frame
    PingFrame ping{7, 99999};
    uint8_t ping_buf[PingFrame::WIRE_SIZE];
    ASSERT_TRUE(ping.encode(ping_buf, sizeof(ping_buf)));
    ASSERT_TRUE(enc.add_frame(FrameType::PING, ping_buf, PingFrame::WIRE_SIZE));

    auto pkt = enc.finalize();

    // Decode
    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));
    EXPECT_TRUE(dec.verify_checksum());

    // First frame: STATE_DELTA
    Frame f1;
    ASSERT_TRUE(dec.next_frame(f1));
    EXPECT_EQ(f1.header.type, FrameType::STATE_DELTA);

    StateDeltaFrame decoded_delta{};
    ASSERT_TRUE(decoded_delta.decode(f1.payload, f1.header.length));
    EXPECT_EQ(decoded_delta.path_hash, 0xAABBCCDD);

    // Second frame: PING
    Frame f2;
    ASSERT_TRUE(dec.next_frame(f2));
    EXPECT_EQ(f2.header.type, FrameType::PING);

    PingFrame decoded_ping{};
    ASSERT_TRUE(decoded_ping.decode(f2.payload, f2.header.length));
    EXPECT_EQ(decoded_ping.ping_id, 7u);
    EXPECT_EQ(decoded_ping.timestamp_us, 99999u);

    // No more frames
    Frame f3;
    EXPECT_FALSE(dec.next_frame(f3));
}

TEST(PacketCodec, AddTypedFrame) {
    PacketEncoder enc;
    enc.set_packet_type(PacketType::HANDSHAKE);
    enc.set_connection_id(0);
    enc.set_packet_number(0);

    ConnectFrame cf{CONNECT_MAGIC, PROTOCOL_VERSION, 1400};
    ASSERT_TRUE(enc.add_typed_frame(FrameType::CONNECT, cf));

    auto pkt = enc.finalize();

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));
    EXPECT_TRUE(dec.verify_checksum());

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_EQ(frame.header.type, FrameType::CONNECT);
}

TEST(PacketCodec, ChecksumDetectsCorruption) {
    PacketEncoder enc;
    enc.set_packet_type(PacketType::DATA);
    enc.set_connection_id(1);
    enc.set_packet_number(1);

    PingFrame ping{1, 1000};
    ASSERT_TRUE(enc.add_typed_frame(FrameType::PING, ping));

    auto pkt = enc.finalize();

    // Verify original checksum
    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));
    EXPECT_TRUE(dec.verify_checksum());

    // Corrupt a payload byte
    pkt[PACKET_HEADER_SIZE + FRAME_HEADER_SIZE + 2] ^= 0xFF;

    PacketDecoder dec2;
    ASSERT_TRUE(dec2.parse(pkt.data(), pkt.size()));
    EXPECT_FALSE(dec2.verify_checksum());
}

TEST(PacketDecoder, ResetFrameCursor) {
    PacketEncoder enc;
    enc.set_packet_type(PacketType::DATA);
    enc.set_connection_id(1);
    enc.set_packet_number(1);

    PingFrame ping{1, 1000};
    ASSERT_TRUE(enc.add_typed_frame(FrameType::PING, ping));

    auto pkt = enc.finalize();

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_FALSE(dec.next_frame(frame)); // exhausted

    dec.reset_frame_cursor();
    ASSERT_TRUE(dec.next_frame(frame)); // can iterate again
    EXPECT_EQ(frame.header.type, FrameType::PING);
}

TEST(PacketEncoder, PacketOverflow) {
    PacketEncoder enc;
    enc.set_packet_type(PacketType::DATA);
    enc.set_connection_id(1);
    enc.set_packet_number(1);

    // Try to add a frame larger than remaining space
    uint8_t big_payload[MAX_PACKET_SIZE] = {};
    EXPECT_FALSE(enc.add_frame(FrameType::STATE_SNAPSHOT, big_payload,
                               static_cast<uint16_t>(MAX_PAYLOAD_SIZE)));
}
