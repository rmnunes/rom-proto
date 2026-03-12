#include "protocoll/transport/dpdk_transport.h"

// DPDK is Linux-only and requires PROTOCOLL_ENABLE_DPDK
// When not available, provide stub implementation

#if defined(PROTOCOLL_ENABLE_DPDK) && defined(__linux__)

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_ether.h>

#include <cstring>
#include <arpa/inet.h>

namespace protocoll {

static bool g_eal_initialized = false;

struct DpdkTransport::Impl {
    rte_mempool* mbuf_pool = nullptr;
    uint16_t port_id = 0;
    rte_ether_addr src_mac{};
    uint32_t src_ip = 0;
    uint16_t src_port = 0;

    // Parse MAC from "aa:bb:cc:dd:ee:ff"
    bool parse_mac(const std::string& mac_str, rte_ether_addr& addr) {
        if (mac_str.empty()) {
            std::memset(&addr, 0, sizeof(addr));
            return true;
        }
        unsigned int bytes[6];
        if (sscanf(mac_str.c_str(), "%x:%x:%x:%x:%x:%x",
                   &bytes[0], &bytes[1], &bytes[2],
                   &bytes[3], &bytes[4], &bytes[5]) != 6) {
            return false;
        }
        for (int i = 0; i < 6; i++) {
            addr.addr_bytes[i] = static_cast<uint8_t>(bytes[i]);
        }
        return true;
    }
};

DpdkTransport::DpdkTransport(DpdkConfig config)
    : config_(config), impl_(std::make_unique<Impl>()) {}

DpdkTransport::~DpdkTransport() {
    close();
}

bool DpdkTransport::init_eal(int argc, char** argv) {
    if (g_eal_initialized) return true;
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) return false;
    g_eal_initialized = true;
    return true;
}

bool DpdkTransport::eal_initialized() {
    return g_eal_initialized;
}

bool DpdkTransport::bind(const Endpoint& local) {
    if (!g_eal_initialized) return false;
    if (bound_) return false;

    impl_->port_id = config_.port_id;

    // Parse source MAC
    if (!impl_->parse_mac(config_.src_mac, impl_->src_mac)) {
        return false;
    }

    // Parse source IP
    const std::string& ip_str = config_.src_ip.empty() ? local.address : config_.src_ip;
    if (!ip_str.empty()) {
        inet_pton(AF_INET, ip_str.c_str(), &impl_->src_ip);
    }
    impl_->src_port = local.port;

    // Create mbuf pool
    char pool_name[32];
    snprintf(pool_name, sizeof(pool_name), "pcol_pool_%u", config_.port_id);
    impl_->mbuf_pool = rte_pktmbuf_pool_create(
        pool_name, config_.mbuf_pool_size, config_.mbuf_cache_size,
        0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!impl_->mbuf_pool) return false;

    // Configure port
    rte_eth_conf port_conf{};
    int ret = rte_eth_dev_configure(impl_->port_id, 1, 1, &port_conf);
    if (ret < 0) return false;

    // Setup RX queue
    ret = rte_eth_rx_queue_setup(impl_->port_id, 0, config_.nb_rx_desc,
                                  rte_eth_dev_socket_id(impl_->port_id),
                                  nullptr, impl_->mbuf_pool);
    if (ret < 0) return false;

    // Setup TX queue
    ret = rte_eth_tx_queue_setup(impl_->port_id, 0, config_.nb_tx_desc,
                                  rte_eth_dev_socket_id(impl_->port_id),
                                  nullptr);
    if (ret < 0) return false;

    // Start port
    ret = rte_eth_dev_start(impl_->port_id);
    if (ret < 0) return false;

    // Enable promiscuous mode
    rte_eth_promiscuous_enable(impl_->port_id);

    local_ep_ = local;
    bound_ = true;
    return true;
}

void DpdkTransport::close() {
    if (!bound_) return;
    rte_eth_dev_stop(impl_->port_id);
    rte_eth_dev_close(impl_->port_id);
    bound_ = false;
}

int DpdkTransport::send_to(const uint8_t* data, size_t len, const Endpoint& remote) {
    if (!bound_ || !impl_->mbuf_pool) return -1;

    rte_mbuf* mbuf = rte_pktmbuf_alloc(impl_->mbuf_pool);
    if (!mbuf) {
        tx_drops_.fetch_add(1);
        return -1;
    }

    // Total header sizes
    constexpr size_t ETH_HDR_LEN = sizeof(rte_ether_hdr);
    constexpr size_t IP_HDR_LEN = sizeof(rte_ipv4_hdr);
    constexpr size_t UDP_HDR_LEN = sizeof(rte_udp_hdr);
    size_t total_len = ETH_HDR_LEN + IP_HDR_LEN + UDP_HDR_LEN + len;

    char* pkt = rte_pktmbuf_append(mbuf, static_cast<uint16_t>(total_len));
    if (!pkt) {
        rte_pktmbuf_free(mbuf);
        tx_drops_.fetch_add(1);
        return -1;
    }

    // Ethernet header
    auto* eth_hdr = reinterpret_cast<rte_ether_hdr*>(pkt);
    std::memset(&eth_hdr->dst_addr, 0xFF, 6); // broadcast (simplified)
    rte_ether_addr_copy(&impl_->src_mac, &eth_hdr->src_addr);
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    // IP header
    auto* ip_hdr = reinterpret_cast<rte_ipv4_hdr*>(pkt + ETH_HDR_LEN);
    std::memset(ip_hdr, 0, IP_HDR_LEN);
    ip_hdr->version_ihl = 0x45;
    ip_hdr->total_length = rte_cpu_to_be_16(static_cast<uint16_t>(IP_HDR_LEN + UDP_HDR_LEN + len));
    ip_hdr->time_to_live = 64;
    ip_hdr->next_proto_id = IPPROTO_UDP;
    ip_hdr->src_addr = impl_->src_ip;
    inet_pton(AF_INET, remote.address.c_str(), &ip_hdr->dst_addr);
    ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);

    // UDP header
    auto* udp_hdr = reinterpret_cast<rte_udp_hdr*>(pkt + ETH_HDR_LEN + IP_HDR_LEN);
    udp_hdr->src_port = rte_cpu_to_be_16(impl_->src_port);
    udp_hdr->dst_port = rte_cpu_to_be_16(remote.port);
    udp_hdr->dgram_len = rte_cpu_to_be_16(static_cast<uint16_t>(UDP_HDR_LEN + len));
    udp_hdr->dgram_cksum = 0;

    // Payload
    std::memcpy(pkt + ETH_HDR_LEN + IP_HDR_LEN + UDP_HDR_LEN, data, len);

    // Send
    uint16_t nb_tx = rte_eth_tx_burst(impl_->port_id, 0, &mbuf, 1);
    if (nb_tx == 0) {
        rte_pktmbuf_free(mbuf);
        tx_drops_.fetch_add(1);
        return -1;
    }

    packets_sent_.fetch_add(1);
    bytes_sent_.fetch_add(len);
    return static_cast<int>(len);
}

int DpdkTransport::recv_from(uint8_t* buf, size_t buf_len, Endpoint& from, int) {
    if (!bound_) return -1;

    rte_mbuf* mbufs[1];
    uint16_t nb_rx = rte_eth_rx_burst(impl_->port_id, 0, mbufs, 1);
    if (nb_rx == 0) return -1;

    rte_mbuf* mbuf = mbufs[0];
    char* pkt = rte_pktmbuf_mtod(mbuf, char*);
    size_t pkt_len = rte_pktmbuf_data_len(mbuf);

    constexpr size_t ETH_HDR_LEN = sizeof(rte_ether_hdr);
    constexpr size_t IP_HDR_LEN = sizeof(rte_ipv4_hdr);
    constexpr size_t UDP_HDR_LEN = sizeof(rte_udp_hdr);
    constexpr size_t TOTAL_HDR = ETH_HDR_LEN + IP_HDR_LEN + UDP_HDR_LEN;

    if (pkt_len < TOTAL_HDR) {
        rte_pktmbuf_free(mbuf);
        rx_drops_.fetch_add(1);
        return -1;
    }

    // Parse IP header for source address
    auto* ip_hdr = reinterpret_cast<rte_ipv4_hdr*>(pkt + ETH_HDR_LEN);
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ip_hdr->src_addr, addr_str, sizeof(addr_str));
    from.address = addr_str;

    // Parse UDP header for source port
    auto* udp_hdr = reinterpret_cast<rte_udp_hdr*>(pkt + ETH_HDR_LEN + IP_HDR_LEN);
    from.port = rte_be_to_cpu_16(udp_hdr->src_port);

    // Extract payload
    size_t payload_len = pkt_len - TOTAL_HDR;
    if (payload_len > buf_len) payload_len = buf_len;
    std::memcpy(buf, pkt + TOTAL_HDR, payload_len);

    rte_pktmbuf_free(mbuf);
    packets_received_.fetch_add(1);
    bytes_received_.fetch_add(payload_len);
    return static_cast<int>(payload_len);
}

} // namespace protocoll

#else // Stub implementation when DPDK is not available

namespace protocoll {

struct DpdkTransport::Impl {};

DpdkTransport::DpdkTransport(DpdkConfig config)
    : config_(config), impl_(std::make_unique<Impl>()) {}

DpdkTransport::~DpdkTransport() = default;

bool DpdkTransport::init_eal(int, char**) { return false; }
bool DpdkTransport::eal_initialized() { return false; }

bool DpdkTransport::bind(const Endpoint&) { return false; }
void DpdkTransport::close() {}
int DpdkTransport::send_to(const uint8_t*, size_t, const Endpoint&) { return -1; }
int DpdkTransport::recv_from(uint8_t*, size_t, Endpoint&, int) { return -1; }

} // namespace protocoll

#endif
