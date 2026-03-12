#include <gtest/gtest.h>
#include "protocoll/connection/connection_manager.h"

using namespace protocoll;

TEST(ConnectionManager, AddAndGet) {
    ConnectionManager mgr;
    auto* mc = mgr.add(1);
    ASSERT_NE(mc, nullptr);
    EXPECT_EQ(mc->remote_node_id, 1);
    EXPECT_EQ(mgr.count(), 1u);

    auto* mc2 = mgr.get(1);
    EXPECT_EQ(mc, mc2);
}

TEST(ConnectionManager, AddDuplicateReturnsNull) {
    ConnectionManager mgr;
    mgr.add(1);
    EXPECT_EQ(mgr.add(1), nullptr);
    EXPECT_EQ(mgr.count(), 1u);
}

TEST(ConnectionManager, Remove) {
    ConnectionManager mgr;
    mgr.add(1);
    mgr.add(2);
    EXPECT_TRUE(mgr.remove(1));
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.get(1), nullptr);
    EXPECT_NE(mgr.get(2), nullptr);
}

TEST(ConnectionManager, RemoveNonexistent) {
    ConnectionManager mgr;
    EXPECT_FALSE(mgr.remove(99));
}

TEST(ConnectionManager, Primary) {
    ConnectionManager mgr;
    EXPECT_EQ(mgr.primary(), nullptr);

    mgr.add(5);
    auto* p = mgr.primary();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->remote_node_id, 5);
}

TEST(ConnectionManager, HasConnections) {
    ConnectionManager mgr;
    EXPECT_FALSE(mgr.has_connections());

    auto* mc = mgr.add(1);
    ASSERT_NE(mc, nullptr);
    // Not connected yet (IDLE state)
    EXPECT_FALSE(mgr.has_connections());
}

TEST(ConnectionManager, ConnectedNodes) {
    ConnectionManager mgr;
    mgr.add(1);
    mgr.add(2);
    // None connected (IDLE state)
    EXPECT_TRUE(mgr.connected_nodes().empty());
}

TEST(ConnectionManager, ForEach) {
    ConnectionManager mgr;
    mgr.add(10);
    mgr.add(20);
    mgr.add(30);

    int count = 0;
    mgr.for_each([&](ManagedConnection& mc) {
        count++;
    });
    EXPECT_EQ(count, 3);
}

TEST(ConnectionManager, NextConnId) {
    ConnectionManager mgr;
    uint16_t id1 = mgr.next_conn_id();
    uint16_t id2 = mgr.next_conn_id();
    EXPECT_NE(id1, id2);
    EXPECT_NE(id1, 0); // 0 is reserved
    EXPECT_NE(id2, 0);
}

TEST(ConnectionManager, GetByConnId) {
    ConnectionManager mgr;
    auto* mc = mgr.add(1);
    ASSERT_NE(mc, nullptr);

    // After initiate, we can find by assigned conn_id
    uint16_t cid = mgr.next_conn_id();
    Endpoint ep{"test", 100};
    mc->conn.initiate(cid, ep);
    auto* found = mgr.get_by_conn_id(cid);
    EXPECT_EQ(found, mc);
}
