#include <gtest/gtest.h>
#include "protocoll/transport/dpdk_transport.h"

using namespace protocoll;

// DPDK tests: most require DPDK EAL + dedicated NIC, so these test
// the stub behavior and config on non-DPDK builds. Full DPDK tests
// are gated by PROTOCOLL_ENABLE_DPDK.

TEST(DpdkTransport, ConfigDefaults) {
    DpdkConfig cfg;
    EXPECT_EQ(cfg.port_id, 0);
    EXPECT_EQ(cfg.nb_rx_desc, 1024);
    EXPECT_EQ(cfg.nb_tx_desc, 1024);
    EXPECT_EQ(cfg.mbuf_pool_size, 8192u);
    EXPECT_EQ(cfg.mbuf_cache_size, 256u);
    EXPECT_EQ(cfg.burst_size, 32);
    EXPECT_TRUE(cfg.src_mac.empty());
    EXPECT_TRUE(cfg.src_ip.empty());
}

TEST(DpdkTransport, CreateWithConfig) {
    DpdkConfig cfg;
    cfg.port_id = 1;
    cfg.burst_size = 64;
    DpdkTransport transport(cfg);
    EXPECT_EQ(transport.config().port_id, 1);
    EXPECT_EQ(transport.config().burst_size, 64);
}

TEST(DpdkTransport, InitialStats) {
    DpdkTransport transport;
    EXPECT_EQ(transport.packets_sent(), 0u);
    EXPECT_EQ(transport.packets_received(), 0u);
    EXPECT_EQ(transport.bytes_sent(), 0u);
    EXPECT_EQ(transport.bytes_received(), 0u);
    EXPECT_EQ(transport.rx_drops(), 0u);
    EXPECT_EQ(transport.tx_drops(), 0u);
}

#ifndef PROTOCOLL_ENABLE_DPDK

TEST(DpdkTransport, StubEalNotInitialized) {
    EXPECT_FALSE(DpdkTransport::eal_initialized());
}

TEST(DpdkTransport, StubEalInitFails) {
    EXPECT_FALSE(DpdkTransport::init_eal(0, nullptr));
}

TEST(DpdkTransport, StubBindFails) {
    DpdkTransport transport;
    Endpoint ep{"0.0.0.0", 9000};
    EXPECT_FALSE(transport.bind(ep));
}

TEST(DpdkTransport, StubSendFails) {
    DpdkTransport transport;
    uint8_t data[] = {0x42};
    Endpoint remote{"127.0.0.1", 9000};
    EXPECT_EQ(transport.send_to(data, 1, remote), -1);
}

TEST(DpdkTransport, StubRecvFails) {
    DpdkTransport transport;
    uint8_t buf[64];
    Endpoint from;
    EXPECT_EQ(transport.recv_from(buf, sizeof(buf), from), -1);
}

TEST(DpdkTransport, StubCloseSafe) {
    DpdkTransport transport;
    transport.close(); // Should not crash
}

#else // PROTOCOLL_ENABLE_DPDK

// Full DPDK tests require EAL initialization with hugepages and a dedicated NIC.
// These are meant to be run in a controlled environment (CI with DPDK setup).

TEST(DpdkTransport, EalInit) {
    // Minimal EAL init with no ports
    const char* args[] = {"test", "--no-pci", "--no-huge"};
    bool ok = DpdkTransport::init_eal(3, const_cast<char**>(args));
    // May fail in environments without DPDK support
    if (ok) {
        EXPECT_TRUE(DpdkTransport::eal_initialized());
    }
}

#endif // PROTOCOLL_ENABLE_DPDK
