#include <gtest/gtest.h>
#include "protocoll/connection/connection.h"
#include "protocoll/connection/connection_id.h"

using namespace protocoll;

TEST(Connection, InitialState) {
    Connection conn;
    EXPECT_EQ(conn.state(), ConnectionState::IDLE);
    EXPECT_EQ(conn.local_conn_id(), 0);
    EXPECT_EQ(conn.remote_conn_id(), 0);
}

TEST(Connection, ClientInitiate) {
    Connection conn;
    Endpoint remote{"127.0.0.1", 9000};
    ASSERT_TRUE(conn.initiate(1, remote));
    EXPECT_EQ(conn.state(), ConnectionState::CONNECTING);
    EXPECT_EQ(conn.local_conn_id(), 1);
    EXPECT_EQ(conn.remote_endpoint().port, 9000);
}

TEST(Connection, ClientInitiateFromWrongState) {
    Connection conn;
    Endpoint remote{"127.0.0.1", 9000};
    conn.initiate(1, remote);
    // Can't initiate again from CONNECTING
    EXPECT_FALSE(conn.initiate(2, remote));
}

TEST(Connection, ClientReceiveAccept) {
    Connection conn;
    Endpoint remote{"127.0.0.1", 9000};
    conn.initiate(1, remote);
    ASSERT_TRUE(conn.on_accept(42, 1000));
    EXPECT_EQ(conn.state(), ConnectionState::CONNECTED);
    EXPECT_EQ(conn.remote_conn_id(), 42);
}

TEST(Connection, ServerAccept) {
    Connection conn;
    Endpoint remote{"127.0.0.1", 8000};
    conn.on_connect(1, 1400);
    EXPECT_EQ(conn.state(), ConnectionState::ACCEPTING);
    ASSERT_TRUE(conn.accept(10, 5, remote));
    EXPECT_EQ(conn.state(), ConnectionState::CONNECTED);
    EXPECT_EQ(conn.local_conn_id(), 10);
    EXPECT_EQ(conn.remote_conn_id(), 5);
}

TEST(Connection, CloseFromConnected) {
    Connection conn;
    Endpoint remote{"127.0.0.1", 9000};
    conn.initiate(1, remote);
    conn.on_accept(42, 1000);
    ASSERT_TRUE(conn.close(CloseReason::NORMAL));
    EXPECT_EQ(conn.state(), ConnectionState::CLOSING);
}

TEST(Connection, ReceiveClose) {
    Connection conn;
    Endpoint remote{"127.0.0.1", 9000};
    conn.initiate(1, remote);
    conn.on_accept(42, 1000);
    ASSERT_TRUE(conn.on_close(CloseReason::GOING_AWAY));
    EXPECT_EQ(conn.state(), ConnectionState::CLOSED);
}

TEST(Connection, PacketNumberSequence) {
    Connection conn;
    EXPECT_EQ(conn.next_send_packet_number(), 0u);
    EXPECT_EQ(conn.next_send_packet_number(), 1u);
    EXPECT_EQ(conn.next_send_packet_number(), 2u);
}

TEST(Connection, RecvPacketNumberTracking) {
    Connection conn;
    conn.update_recv_packet_number(5);
    EXPECT_EQ(conn.last_recv_packet_number(), 5u);
    conn.update_recv_packet_number(3); // Out-of-order, should not regress
    EXPECT_EQ(conn.last_recv_packet_number(), 5u);
    conn.update_recv_packet_number(10);
    EXPECT_EQ(conn.last_recv_packet_number(), 10u);
}

TEST(Connection, RttEstimate) {
    Connection conn;
    conn.update_rtt(1000); // First sample = 1ms
    EXPECT_EQ(conn.smoothed_rtt_us(), 1000u);

    conn.update_rtt(2000); // EWMA: (1000*7 + 2000) / 8 = 1125
    EXPECT_EQ(conn.smoothed_rtt_us(), 1125u);
}

TEST(Connection, ElapsedTimestamp) {
    Connection conn;
    uint32_t t = conn.elapsed_us();
    // Should be very small (just created)
    EXPECT_LT(t, 1000000u); // Less than 1 second
}

TEST(ConnectionIdAllocator, Unique) {
    ConnectionIdAllocator alloc;
    uint16_t id1 = alloc.next();
    uint16_t id2 = alloc.next();
    uint16_t id3 = alloc.next();
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, 0); // 0 is reserved
}
