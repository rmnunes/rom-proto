#include <gtest/gtest.h>
#include "protocoll/wire/codec.h"
#include "protocoll/wire/packet.h"
#include "protocoll/wire/frame.h"
#include "protocoll/wire/frame_types.h"

using namespace protocoll;

// These tests verify the decode pipeline used by the CLI tools:
// raw bytes -> PacketDecoder -> Frame iteration -> frame-specific decode.

// --- Helper: build a valid packet with frames ---

static std::vector<uint8_t> build_packet(PacketType type, uint16_t conn_id,
                                          uint32_t pkt_num,
                                          const std::vector<std::pair<FrameType, std::vector<uint8_t>>>& frames) {
    PacketEncoder enc;
    enc.set_packet_type(type);
    enc.set_connection_id(conn_id);
    enc.set_packet_number(pkt_num);
    enc.set_timestamp(12345);

    for (const auto& [ft, payload] : frames) {
        enc.add_frame(ft, payload.data(), static_cast<uint16_t>(payload.size()));
    }

    return enc.finalize();
}

// --- Tests ---

TEST(CliDecode, EmptyPacket) {
    auto pkt = build_packet(PacketType::DATA, 1, 0, {});
    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));
    EXPECT_EQ(dec.header().packet_type, PacketType::DATA);
    EXPECT_EQ(dec.header().connection_id, 1);
    EXPECT_EQ(dec.header().payload_length, 0);
    EXPECT_TRUE(dec.verify_checksum());

    Frame frame;
    EXPECT_FALSE(dec.next_frame(frame));
}

TEST(CliDecode, ConnectFrameDecode) {
    ConnectFrame cf{CONNECT_MAGIC, PROTOCOL_VERSION, 1400};
    uint8_t buf[ConnectFrame::WIRE_SIZE];
    ASSERT_TRUE(cf.encode(buf, sizeof(buf)));

    auto pkt = build_packet(PacketType::HANDSHAKE, 0, 0,
        {{FrameType::CONNECT, std::vector<uint8_t>(buf, buf + ConnectFrame::WIRE_SIZE)}});

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));
    ASSERT_TRUE(dec.verify_checksum());

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_EQ(frame.header.type, FrameType::CONNECT);
    EXPECT_EQ(frame.header.length, ConnectFrame::WIRE_SIZE);

    ConnectFrame decoded;
    ASSERT_TRUE(decoded.decode(frame.payload, frame.header.length));
    EXPECT_EQ(decoded.magic, CONNECT_MAGIC);
    EXPECT_EQ(decoded.version, PROTOCOL_VERSION);
    EXPECT_EQ(decoded.max_frame_size, 1400);
}

TEST(CliDecode, AcceptFrameDecode) {
    AcceptFrame af{42, 999000};
    uint8_t buf[AcceptFrame::WIRE_SIZE];
    ASSERT_TRUE(af.encode(buf, sizeof(buf)));

    auto pkt = build_packet(PacketType::HANDSHAKE, 0, 0,
        {{FrameType::ACCEPT, std::vector<uint8_t>(buf, buf + AcceptFrame::WIRE_SIZE)}});

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_EQ(frame.header.type, FrameType::ACCEPT);

    AcceptFrame decoded;
    ASSERT_TRUE(decoded.decode(frame.payload, frame.header.length));
    EXPECT_EQ(decoded.assigned_conn_id, 42);
    EXPECT_EQ(decoded.server_timestamp_us, 999000u);
}

TEST(CliDecode, CloseFrameDecode) {
    CloseFrame clf{CloseReason::TIMEOUT};
    uint8_t buf[CloseFrame::WIRE_SIZE];
    ASSERT_TRUE(clf.encode(buf, sizeof(buf)));

    auto pkt = build_packet(PacketType::CONTROL, 5, 10,
        {{FrameType::CLOSE, std::vector<uint8_t>(buf, buf + CloseFrame::WIRE_SIZE)}});

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));

    CloseFrame decoded;
    ASSERT_TRUE(decoded.decode(frame.payload, frame.header.length));
    EXPECT_EQ(decoded.reason, CloseReason::TIMEOUT);
}

TEST(CliDecode, PingPongDecode) {
    PingFrame pf{7, 500000};
    uint8_t buf[PingFrame::WIRE_SIZE];
    ASSERT_TRUE(pf.encode(buf, sizeof(buf)));

    auto pkt = build_packet(PacketType::CONTROL, 1, 0,
        {{FrameType::PING, std::vector<uint8_t>(buf, buf + PingFrame::WIRE_SIZE)},
         {FrameType::PONG, std::vector<uint8_t>(buf, buf + PingFrame::WIRE_SIZE)}});

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_EQ(frame.header.type, FrameType::PING);

    PingFrame decoded;
    ASSERT_TRUE(decoded.decode(frame.payload, frame.header.length));
    EXPECT_EQ(decoded.ping_id, 7u);
    EXPECT_EQ(decoded.timestamp_us, 500000u);

    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_EQ(frame.header.type, FrameType::PONG);
}

TEST(CliDecode, AckFrameDecode) {
    AckFrame af{100, 2000, 0};
    uint8_t buf[AckFrame::BASE_WIRE_SIZE];
    ASSERT_TRUE(af.encode(buf, sizeof(buf)));

    auto pkt = build_packet(PacketType::CONTROL, 3, 5,
        {{FrameType::ACK, std::vector<uint8_t>(buf, buf + AckFrame::BASE_WIRE_SIZE)}});

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));

    AckFrame decoded;
    ASSERT_TRUE(decoded.decode(frame.payload, frame.header.length));
    EXPECT_EQ(decoded.largest_acked, 100u);
    EXPECT_EQ(decoded.ack_delay_us, 2000u);
    EXPECT_EQ(decoded.sack_range_count, 0);
}

TEST(CliDecode, StateDeclareFrameDecode) {
    StateDeclareFrame sd{0xCAFEBABE, CrdtType::LWW_REGISTER, Reliability::RELIABLE};
    uint8_t base_buf[StateDeclareFrame::BASE_WIRE_SIZE];
    ASSERT_TRUE(sd.encode(base_buf, sizeof(base_buf)));

    // Build payload: base + path string
    std::string path = "/game/player/1/pos";
    std::vector<uint8_t> payload(base_buf, base_buf + StateDeclareFrame::BASE_WIRE_SIZE);
    payload.insert(payload.end(), path.begin(), path.end());

    auto pkt = build_packet(PacketType::DATA, 1, 0,
        {{FrameType::STATE_DECLARE, payload}});

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_EQ(frame.header.type, FrameType::STATE_DECLARE);

    StateDeclareFrame decoded;
    ASSERT_TRUE(decoded.decode(frame.payload, frame.header.length));
    EXPECT_EQ(decoded.path_hash, 0xCAFEBABE);
    EXPECT_EQ(decoded.crdt_type, CrdtType::LWW_REGISTER);
    EXPECT_EQ(decoded.reliability, Reliability::RELIABLE);

    // Extract path string
    size_t str_len = frame.header.length - StateDeclareFrame::BASE_WIRE_SIZE;
    std::string decoded_path(
        reinterpret_cast<const char*>(frame.payload + StateDeclareFrame::BASE_WIRE_SIZE),
        str_len);
    EXPECT_EQ(decoded_path, "/game/player/1/pos");
}

TEST(CliDecode, StateDeltaFrameDecode) {
    StateDeltaFrame sdf{0xDEADBEEF, CrdtType::G_COUNTER, Reliability::BEST_EFFORT, 42};
    uint8_t base_buf[StateDeltaFrame::BASE_WIRE_SIZE];
    ASSERT_TRUE(sdf.encode(base_buf, sizeof(base_buf)));

    // Build payload: base + delta + fake signature
    std::vector<uint8_t> delta_data = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> payload(base_buf, base_buf + StateDeltaFrame::BASE_WIRE_SIZE);
    payload.insert(payload.end(), delta_data.begin(), delta_data.end());
    // Fake 64-byte signature
    payload.insert(payload.end(), 64, 0xAA);

    auto pkt = build_packet(PacketType::DATA, 1, 1, {{FrameType::STATE_DELTA, payload}});

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_EQ(frame.header.type, FrameType::STATE_DELTA);

    StateDeltaFrame decoded;
    ASSERT_TRUE(decoded.decode(frame.payload, frame.header.length));
    EXPECT_EQ(decoded.path_hash, 0xDEADBEEF);
    EXPECT_EQ(decoded.crdt_type, CrdtType::G_COUNTER);
    EXPECT_EQ(decoded.reliability, Reliability::BEST_EFFORT);
    EXPECT_EQ(decoded.author_node_id, 42);

    // Verify we can extract delta data (between header and signature)
    ASSERT_GE(frame.header.length, StateDeltaFrame::MIN_WIRE_SIZE);
    size_t delta_len = frame.header.length - StateDeltaFrame::BASE_WIRE_SIZE - StateDeltaFrame::SIGNATURE_SIZE;
    EXPECT_EQ(delta_len, 4u);
    EXPECT_EQ(frame.payload[StateDeltaFrame::BASE_WIRE_SIZE], 0x01);
    EXPECT_EQ(frame.payload[StateDeltaFrame::BASE_WIRE_SIZE + 1], 0x02);
}

TEST(CliDecode, MultipleFramesInOnePacket) {
    PingFrame pf{1, 100};
    uint8_t ping_buf[PingFrame::WIRE_SIZE];
    pf.encode(ping_buf, sizeof(ping_buf));

    AckFrame af{50, 1000, 0};
    uint8_t ack_buf[AckFrame::BASE_WIRE_SIZE];
    af.encode(ack_buf, sizeof(ack_buf));

    CloseFrame clf{CloseReason::NORMAL};
    uint8_t close_buf[CloseFrame::WIRE_SIZE];
    clf.encode(close_buf, sizeof(close_buf));

    auto pkt = build_packet(PacketType::CONTROL, 2, 3, {
        {FrameType::PING, std::vector<uint8_t>(ping_buf, ping_buf + PingFrame::WIRE_SIZE)},
        {FrameType::ACK, std::vector<uint8_t>(ack_buf, ack_buf + AckFrame::BASE_WIRE_SIZE)},
        {FrameType::CLOSE, std::vector<uint8_t>(close_buf, close_buf + CloseFrame::WIRE_SIZE)},
    });

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));
    ASSERT_TRUE(dec.verify_checksum());

    Frame frame;
    int count = 0;
    FrameType types[3];
    while (dec.next_frame(frame)) {
        ASSERT_LT(count, 3);
        types[count++] = frame.header.type;
    }

    EXPECT_EQ(count, 3);
    EXPECT_EQ(types[0], FrameType::PING);
    EXPECT_EQ(types[1], FrameType::ACK);
    EXPECT_EQ(types[2], FrameType::CLOSE);
}

TEST(CliDecode, InvalidPacketTooShort) {
    uint8_t buf[4] = {0x10, 0x01, 0x00, 0x00};
    PacketDecoder dec;
    EXPECT_FALSE(dec.parse(buf, 4));
}

TEST(CliDecode, ChecksumVerification) {
    auto pkt = build_packet(PacketType::DATA, 1, 0, {});
    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));
    EXPECT_TRUE(dec.verify_checksum());

    // Corrupt a byte
    pkt[5] ^= 0xFF;
    PacketDecoder dec2;
    ASSERT_TRUE(dec2.parse(pkt.data(), pkt.size()));
    EXPECT_FALSE(dec2.verify_checksum());
}

TEST(CliDecode, FrameCursorReset) {
    PingFrame pf{1, 100};
    uint8_t buf[PingFrame::WIRE_SIZE];
    pf.encode(buf, sizeof(buf));

    auto pkt = build_packet(PacketType::CONTROL, 1, 0,
        {{FrameType::PING, std::vector<uint8_t>(buf, buf + PingFrame::WIRE_SIZE)}});

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));

    // First pass
    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_FALSE(dec.next_frame(frame));

    // Reset and iterate again
    dec.reset_frame_cursor();
    ASSERT_TRUE(dec.next_frame(frame));
    EXPECT_EQ(frame.header.type, FrameType::PING);
}

TEST(CliDecode, CapabilityRevokeFrameDecode) {
    CapabilityRevokeFrame cr{99, 7};
    uint8_t buf[CapabilityRevokeFrame::WIRE_SIZE];
    ASSERT_TRUE(cr.encode(buf, sizeof(buf)));

    auto pkt = build_packet(PacketType::DATA, 1, 0,
        {{FrameType::CAPABILITY_REVOKE, std::vector<uint8_t>(buf, buf + CapabilityRevokeFrame::WIRE_SIZE)}});

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));

    CapabilityRevokeFrame decoded;
    ASSERT_TRUE(decoded.decode(frame.payload, frame.header.length));
    EXPECT_EQ(decoded.token_id, 99u);
    EXPECT_EQ(decoded.issuer_node_id, 7);
}

TEST(CliDecode, StateSnapshotFrameDecode) {
    StateSnapshotFrame ssf{0x12345678, CrdtType::OR_SET, 10};
    uint8_t base_buf[StateSnapshotFrame::BASE_WIRE_SIZE];
    ASSERT_TRUE(ssf.encode(base_buf, sizeof(base_buf)));

    // Build: base + snapshot data + fake signature
    std::vector<uint8_t> payload(base_buf, base_buf + StateSnapshotFrame::BASE_WIRE_SIZE);
    payload.push_back(0xFF); // 1 byte snapshot data
    payload.insert(payload.end(), 64, 0xBB); // fake signature

    auto pkt = build_packet(PacketType::DATA, 1, 0, {{FrameType::STATE_SNAPSHOT, payload}});

    PacketDecoder dec;
    ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));

    Frame frame;
    ASSERT_TRUE(dec.next_frame(frame));

    StateSnapshotFrame decoded;
    ASSERT_TRUE(decoded.decode(frame.payload, frame.header.length));
    EXPECT_EQ(decoded.path_hash, 0x12345678);
    EXPECT_EQ(decoded.crdt_type, CrdtType::OR_SET);
    EXPECT_EQ(decoded.author_node_id, 10);
}

TEST(CliDecode, AllPacketTypes) {
    for (auto pt : {PacketType::DATA, PacketType::CONTROL, PacketType::HANDSHAKE}) {
        auto pkt = build_packet(pt, 1, 0, {});
        PacketDecoder dec;
        ASSERT_TRUE(dec.parse(pkt.data(), pkt.size()));
        EXPECT_EQ(dec.header().packet_type, pt);
    }
}
