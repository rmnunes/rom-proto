#pragma once

// DtlsTransport: DTLS-encrypted wrapper over any Transport.
//
// Decorator pattern: wraps an inner Transport (typically UdpTransport)
// and provides encrypted send/recv using DTLS 1.2 via mbedTLS.
//
// DTLS encrypts the channel. Ed25519 per-frame signatures remain.
// "Trust the data, not the channel" — DTLS prevents eavesdropping,
// signatures prevent forgery.
//
// Usage:
//   UdpTransport udp;
//   DtlsConfig config;
//   config.is_server = true;
//   config.cert_pem = "...";
//   config.key_pem = "...";
//   DtlsTransport dtls(udp, config);
//   dtls.bind(ep);
//   dtls.handshake(remote_ep, 5000);  // DTLS handshake
//   dtls.send_to(data, len, remote);  // encrypted
//   dtls.recv_from(buf, len, from);   // decrypted

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>

#include "protocoll/transport/transport.h"
#include "protocoll/wire/frame_types.h"

namespace protocoll {

// DTLS configuration
struct DtlsConfig {
    bool is_server = false;

    // PEM-encoded certificate and private key
    std::string cert_pem;
    std::string key_pem;

    // PEM-encoded CA certificate for peer verification (optional)
    std::string ca_pem;

    // If true, require peer certificate verification
    bool verify_peer = false;

    // DTLS handshake timeout in milliseconds
    uint32_t handshake_timeout_ms = 5000;

    // Connection timeout (retransmission) in milliseconds
    uint32_t min_retransmit_ms = 1000;
    uint32_t max_retransmit_ms = 60000;
};

// DTLS connection state
enum class DtlsState : uint8_t {
    IDLE,
    HANDSHAKING,
    CONNECTED,
    CLOSED,
    ERROR,
};

class DtlsTransport : public Transport {
public:
    DtlsTransport(Transport& inner, const DtlsConfig& config);
    ~DtlsTransport() override;

    // Non-copyable, non-movable
    DtlsTransport(const DtlsTransport&) = delete;
    DtlsTransport& operator=(const DtlsTransport&) = delete;

    // Transport interface
    bool bind(const Endpoint& local) override;
    void close() override;
    int send_to(const uint8_t* data, size_t len, const Endpoint& remote) override;
    int recv_from(uint8_t* buf, size_t buf_len, Endpoint& from, int timeout_ms = -1) override;

    // Perform DTLS handshake with remote peer.
    // Must be called after bind() and before send_to/recv_from.
    // Returns true on successful handshake.
    bool handshake(const Endpoint& remote, int timeout_ms = 5000);

    // Current DTLS state
    DtlsState state() const { return state_; }

    // Get the negotiated DTLS version string (e.g., "DTLSv1.2")
    std::string negotiated_version() const;

    // Get the negotiated cipher suite name
    std::string cipher_suite() const;

    // Statistics
    uint64_t bytes_encrypted() const { return bytes_encrypted_; }
    uint64_t bytes_decrypted() const { return bytes_decrypted_; }

private:
    Transport& inner_;
    DtlsConfig config_;
    DtlsState state_ = DtlsState::IDLE;
    Endpoint remote_ep_;

    uint64_t bytes_encrypted_ = 0;
    uint64_t bytes_decrypted_ = 0;

    // Opaque pointer to mbedTLS context (avoids header dependency in public API)
    struct DtlsContext;
    std::unique_ptr<DtlsContext> ctx_;

    bool setup_context();
    void teardown_context();

    // mbedTLS BIO callbacks — route through inner_ transport
    static int bio_send(void* ctx, const unsigned char* buf, size_t len);
    static int bio_recv(void* ctx, unsigned char* buf, size_t len);
    static int bio_recv_timeout(void* ctx, unsigned char* buf, size_t len, uint32_t timeout);
};

} // namespace protocoll
