#include <gtest/gtest.h>
#include "protocoll/wire/frame.h"
#include "protocoll/wire/frame_types.h"
#include <array>

using namespace protocoll;

TEST(FrameHeader, EncodeDecodeRoundTrip) {
    FrameHeader fh{FrameType::STATE_DELTA, 256};

    std::array<uint8_t, FRAME_HEADER_SIZE> buf{};
    ASSERT_TRUE(fh.encode(buf.data(), buf.size()));

    FrameHeader decoded{};
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));

    EXPECT_EQ(decoded.type, FrameType::STATE_DELTA);
    EXPECT_EQ(decoded.length, 256);
}

TEST(ConnectFrame, RoundTrip) {
    ConnectFrame cf{CONNECT_MAGIC, PROTOCOL_VERSION, 1400};

    std::array<uint8_t, ConnectFrame::WIRE_SIZE> buf{};
    ASSERT_TRUE(cf.encode(buf.data(), buf.size()));

    ConnectFrame decoded{};
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));

    EXPECT_EQ(decoded.magic, CONNECT_MAGIC);
    EXPECT_EQ(decoded.version, PROTOCOL_VERSION);
    EXPECT_EQ(decoded.max_frame_size, 1400);
}

TEST(AcceptFrame, RoundTrip) {
    AcceptFrame af{0x4567, 999999};

    std::array<uint8_t, AcceptFrame::WIRE_SIZE> buf{};
    ASSERT_TRUE(af.encode(buf.data(), buf.size()));

    AcceptFrame decoded{};
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));

    EXPECT_EQ(decoded.assigned_conn_id, 0x4567);
    EXPECT_EQ(decoded.server_timestamp_us, 999999u);
}

TEST(CloseFrame, RoundTrip) {
    CloseFrame cf{CloseReason::TIMEOUT};

    std::array<uint8_t, CloseFrame::WIRE_SIZE> buf{};
    ASSERT_TRUE(cf.encode(buf.data(), buf.size()));

    CloseFrame decoded{};
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));

    EXPECT_EQ(decoded.reason, CloseReason::TIMEOUT);
}

TEST(PingFrame, RoundTrip) {
    PingFrame pf{42, 1234567};

    std::array<uint8_t, PingFrame::WIRE_SIZE> buf{};
    ASSERT_TRUE(pf.encode(buf.data(), buf.size()));

    PingFrame decoded{};
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));

    EXPECT_EQ(decoded.ping_id, 42u);
    EXPECT_EQ(decoded.timestamp_us, 1234567u);
}

TEST(AckFrame, RoundTrip) {
    AckFrame af{100, 500, 2};

    std::array<uint8_t, AckFrame::BASE_WIRE_SIZE> buf{};
    ASSERT_TRUE(af.encode(buf.data(), buf.size()));

    AckFrame decoded{};
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));

    EXPECT_EQ(decoded.largest_acked, 100u);
    EXPECT_EQ(decoded.ack_delay_us, 500);
    EXPECT_EQ(decoded.sack_range_count, 2);
}

TEST(StateDeclareFrame, RoundTrip) {
    StateDeclareFrame sdf{0xDEADBEEF, CrdtType::LWW_REGISTER, Reliability::RELIABLE};

    std::array<uint8_t, StateDeclareFrame::BASE_WIRE_SIZE> buf{};
    ASSERT_TRUE(sdf.encode(buf.data(), buf.size()));

    StateDeclareFrame decoded{};
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));

    EXPECT_EQ(decoded.path_hash, 0xDEADBEEF);
    EXPECT_EQ(decoded.crdt_type, CrdtType::LWW_REGISTER);
    EXPECT_EQ(decoded.reliability, Reliability::RELIABLE);
}

TEST(StateDeltaFrame, RoundTrip) {
    StateDeltaFrame sdf{0xCAFEBABE, CrdtType::G_COUNTER, Reliability::BEST_EFFORT, 42};

    std::array<uint8_t, StateDeltaFrame::BASE_WIRE_SIZE> buf{};
    ASSERT_TRUE(sdf.encode(buf.data(), buf.size()));

    StateDeltaFrame decoded{};
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));

    EXPECT_EQ(decoded.path_hash, 0xCAFEBABE);
    EXPECT_EQ(decoded.crdt_type, CrdtType::G_COUNTER);
    EXPECT_EQ(decoded.reliability, Reliability::BEST_EFFORT);
    EXPECT_EQ(decoded.author_node_id, 42);
}

TEST(StateSnapshotFrame, RoundTrip) {
    StateSnapshotFrame ssf{0x12345678, CrdtType::OR_SET, 7};

    std::array<uint8_t, StateSnapshotFrame::BASE_WIRE_SIZE> buf{};
    ASSERT_TRUE(ssf.encode(buf.data(), buf.size()));

    StateSnapshotFrame decoded{};
    ASSERT_TRUE(decoded.decode(buf.data(), buf.size()));

    EXPECT_EQ(decoded.path_hash, 0x12345678);
    EXPECT_EQ(decoded.crdt_type, CrdtType::OR_SET);
    EXPECT_EQ(decoded.author_node_id, 7);
}
