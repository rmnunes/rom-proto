#pragma once

// DpdkTransport: kernel-bypass transport using DPDK (Data Plane Development Kit).
//
// Linux only, requires DPDK libraries and hugepages.
// Constructs Ethernet/IP/UDP headers manually (no kernel stack).
// Uses poll-mode driver with rte_eth_rx_burst / rte_eth_tx_burst.
//
// Gated by PROTOCOLL_ENABLE_DPDK=OFF (default).
// When disabled, provides a stub that returns errors.

#include "protocoll/transport/transport.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <atomic>
#include <string>

namespace protocoll {

struct DpdkConfig {
    // DPDK port ID (NIC index)
    uint16_t port_id = 0;

    // Number of RX/TX descriptors
    uint16_t nb_rx_desc = 1024;
    uint16_t nb_tx_desc = 1024;

    // Mbuf pool size
    uint32_t mbuf_pool_size = 8192;

    // Mbuf cache size
    uint32_t mbuf_cache_size = 256;

    // Burst size for rx/tx operations
    uint16_t burst_size = 32;

    // Source MAC address (6 bytes, hex string "aa:bb:cc:dd:ee:ff")
    std::string src_mac;

    // Source IP (for manual IP header construction)
    std::string src_ip;
};

class DpdkTransport : public Transport {
public:
    explicit DpdkTransport(DpdkConfig config = {});
    ~DpdkTransport() override;

    // Transport interface
    bool bind(const Endpoint& local) override;
    void close() override;
    int send_to(const uint8_t* data, size_t len, const Endpoint& remote) override;
    int recv_from(uint8_t* buf, size_t buf_len, Endpoint& from, int timeout_ms = -1) override;

    // --- DPDK-specific ---

    // Initialize DPDK EAL (must be called before bind)
    // argc/argv are EAL arguments (e.g., "-l 0-1 --huge-dir /mnt/huge")
    static bool init_eal(int argc, char** argv);

    // Check if EAL has been initialized
    static bool eal_initialized();

    // Get port statistics
    uint64_t packets_sent() const { return packets_sent_.load(); }
    uint64_t packets_received() const { return packets_received_.load(); }
    uint64_t bytes_sent() const { return bytes_sent_.load(); }
    uint64_t bytes_received() const { return bytes_received_.load(); }
    uint64_t rx_drops() const { return rx_drops_.load(); }
    uint64_t tx_drops() const { return tx_drops_.load(); }

    const DpdkConfig& config() const { return config_; }

private:
    DpdkConfig config_;
    bool bound_ = false;

    std::atomic<uint64_t> packets_sent_{0};
    std::atomic<uint64_t> packets_received_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> bytes_received_{0};
    std::atomic<uint64_t> rx_drops_{0};
    std::atomic<uint64_t> tx_drops_{0};

    // Platform-specific implementation (DPDK internals hidden)
    struct Impl;
    std::unique_ptr<Impl> impl_;

    Endpoint local_ep_;
};

} // namespace protocoll
