#include "protocoll/transport/dtls_transport.h"

#ifdef PROTOCOLL_ENABLE_DTLS

#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <mbedtls/timing.h>
#include <mbedtls/debug.h>

#include <cstring>
#include <chrono>

namespace protocoll {

struct DtlsTransport::DtlsContext {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt cert;
    mbedtls_x509_crt ca_cert;
    mbedtls_pk_context pk;
    mbedtls_ssl_cookie_ctx cookie;
    mbedtls_timing_delay_context timer;

    DtlsContext() {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_x509_crt_init(&cert);
        mbedtls_x509_crt_init(&ca_cert);
        mbedtls_pk_init(&pk);
        mbedtls_ssl_cookie_init(&cookie);
    }

    ~DtlsContext() {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_x509_crt_free(&cert);
        mbedtls_x509_crt_free(&ca_cert);
        mbedtls_pk_free(&pk);
        mbedtls_ssl_cookie_free(&cookie);
    }
};

DtlsTransport::DtlsTransport(Transport& inner, const DtlsConfig& config)
    : inner_(inner), config_(config) {}

DtlsTransport::~DtlsTransport() {
    close();
}

bool DtlsTransport::setup_context() {
    ctx_ = std::make_unique<DtlsContext>();

    // Seed the RNG
    const char* pers = "protocoll_dtls";
    int ret = mbedtls_ctr_drbg_seed(&ctx_->ctr_drbg, mbedtls_entropy_func,
                                      &ctx_->entropy,
                                      reinterpret_cast<const unsigned char*>(pers),
                                      strlen(pers));
    if (ret != 0) return false;

    // Configure SSL
    int endpoint = config_.is_server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT;
    ret = mbedtls_ssl_config_defaults(&ctx_->conf, endpoint,
                                       MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) return false;

    mbedtls_ssl_conf_rng(&ctx_->conf, mbedtls_ctr_drbg_random, &ctx_->ctr_drbg);

    // Parse certificate
    if (!config_.cert_pem.empty()) {
        ret = mbedtls_x509_crt_parse(&ctx_->cert,
            reinterpret_cast<const unsigned char*>(config_.cert_pem.c_str()),
            config_.cert_pem.size() + 1);
        if (ret != 0) return false;
    }

    // Parse private key
    if (!config_.key_pem.empty()) {
        ret = mbedtls_pk_parse_key(&ctx_->pk,
            reinterpret_cast<const unsigned char*>(config_.key_pem.c_str()),
            config_.key_pem.size() + 1,
            nullptr, 0,
            mbedtls_ctr_drbg_random, &ctx_->ctr_drbg);
        if (ret != 0) return false;
    }

    // Configure own cert + key
    if (!config_.cert_pem.empty() && !config_.key_pem.empty()) {
        ret = mbedtls_ssl_conf_own_cert(&ctx_->conf, &ctx_->cert, &ctx_->pk);
        if (ret != 0) return false;
    }

    // Parse CA cert for verification
    if (!config_.ca_pem.empty()) {
        ret = mbedtls_x509_crt_parse(&ctx_->ca_cert,
            reinterpret_cast<const unsigned char*>(config_.ca_pem.c_str()),
            config_.ca_pem.size() + 1);
        if (ret != 0) return false;
        mbedtls_ssl_conf_ca_chain(&ctx_->conf, &ctx_->ca_cert, nullptr);
    }

    // Peer verification
    if (config_.verify_peer) {
        mbedtls_ssl_conf_authmode(&ctx_->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        mbedtls_ssl_conf_authmode(&ctx_->conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    // DTLS cookies (server-side anti-DoS)
    if (config_.is_server) {
        ret = mbedtls_ssl_cookie_setup(&ctx_->cookie,
            mbedtls_ctr_drbg_random, &ctx_->ctr_drbg);
        if (ret != 0) return false;
        mbedtls_ssl_conf_dtls_cookies(&ctx_->conf,
            mbedtls_ssl_cookie_write, mbedtls_ssl_cookie_check, &ctx_->cookie);
    }

    // Setup SSL context
    ret = mbedtls_ssl_setup(&ctx_->ssl, &ctx_->conf);
    if (ret != 0) return false;

    // Set timer callbacks for DTLS retransmission
    mbedtls_ssl_set_timer_cb(&ctx_->ssl, &ctx_->timer,
        mbedtls_timing_set_delay, mbedtls_timing_get_delay);

    // Set BIO callbacks — route through inner transport
    mbedtls_ssl_set_bio(&ctx_->ssl, this, bio_send, bio_recv, bio_recv_timeout);

    return true;
}

void DtlsTransport::teardown_context() {
    if (ctx_) {
        mbedtls_ssl_close_notify(&ctx_->ssl);
        ctx_.reset();
    }
}

bool DtlsTransport::bind(const Endpoint& local) {
    return inner_.bind(local);
}

void DtlsTransport::close() {
    if (state_ == DtlsState::CONNECTED || state_ == DtlsState::HANDSHAKING) {
        teardown_context();
    }
    state_ = DtlsState::CLOSED;
    inner_.close();
}

bool DtlsTransport::handshake(const Endpoint& remote, int timeout_ms) {
    remote_ep_ = remote;

    if (!setup_context()) {
        state_ = DtlsState::ERROR;
        return false;
    }

    state_ = DtlsState::HANDSHAKING;

    auto start = std::chrono::steady_clock::now();
    int ret;
    do {
        ret = mbedtls_ssl_handshake(&ctx_->ssl);

        if (ret == 0) {
            state_ = DtlsState::CONNECTED;
            return true;
        }

        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            state_ = DtlsState::ERROR;
            teardown_context();
            return false;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) {
            state_ = DtlsState::ERROR;
            teardown_context();
            return false;
        }
    } while (true);
}

int DtlsTransport::send_to(const uint8_t* data, size_t len, const Endpoint& remote) {
    if (state_ != DtlsState::CONNECTED || !ctx_) return -1;

    // DTLS is connection-oriented — verify destination matches
    // (for multi-peer, would need session management)
    int ret = mbedtls_ssl_write(&ctx_->ssl, data, len);
    if (ret > 0) {
        bytes_encrypted_ += static_cast<uint64_t>(ret);
    }
    return ret;
}

int DtlsTransport::recv_from(uint8_t* buf, size_t buf_len, Endpoint& from, int timeout_ms) {
    if (state_ != DtlsState::CONNECTED || !ctx_) return -1;

    int ret = mbedtls_ssl_read(&ctx_->ssl, buf, buf_len);
    if (ret > 0) {
        bytes_decrypted_ += static_cast<uint64_t>(ret);
        from = remote_ep_;
    }
    return ret;
}

std::string DtlsTransport::negotiated_version() const {
    if (!ctx_ || state_ != DtlsState::CONNECTED) return "";
    return mbedtls_ssl_get_version(&ctx_->ssl);
}

std::string DtlsTransport::cipher_suite() const {
    if (!ctx_ || state_ != DtlsState::CONNECTED) return "";
    return mbedtls_ssl_get_ciphersuite(&ctx_->ssl);
}

// --- BIO callbacks ---

int DtlsTransport::bio_send(void* ctx, const unsigned char* buf, size_t len) {
    auto* self = static_cast<DtlsTransport*>(ctx);
    int ret = self->inner_.send_to(buf, len, self->remote_ep_);
    if (ret < 0) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return ret;
}

int DtlsTransport::bio_recv(void* ctx, unsigned char* buf, size_t len) {
    auto* self = static_cast<DtlsTransport*>(ctx);
    Endpoint from;
    int ret = self->inner_.recv_from(buf, len, from, 0); // non-blocking
    if (ret < 0) return MBEDTLS_ERR_SSL_WANT_READ;
    if (ret == 0) return MBEDTLS_ERR_SSL_WANT_READ;
    return ret;
}

int DtlsTransport::bio_recv_timeout(void* ctx, unsigned char* buf, size_t len, uint32_t timeout) {
    auto* self = static_cast<DtlsTransport*>(ctx);
    Endpoint from;
    int ret = self->inner_.recv_from(buf, len, from, static_cast<int>(timeout));
    if (ret < 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    if (ret == 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    return ret;
}

} // namespace protocoll

#else // !PROTOCOLL_ENABLE_DTLS

// Stub implementation when DTLS is not enabled
namespace protocoll {

struct DtlsTransport::DtlsContext {};

DtlsTransport::DtlsTransport(Transport& inner, const DtlsConfig& config)
    : inner_(inner), config_(config) {}

DtlsTransport::~DtlsTransport() {}

bool DtlsTransport::setup_context() { return false; }
void DtlsTransport::teardown_context() {}

bool DtlsTransport::bind(const Endpoint& local) { return inner_.bind(local); }
void DtlsTransport::close() {
    state_ = DtlsState::CLOSED;
    inner_.close();
}

bool DtlsTransport::handshake(const Endpoint&, int) {
    state_ = DtlsState::ERROR;
    return false;
}

int DtlsTransport::send_to(const uint8_t*, size_t, const Endpoint&) {
    return -1;
}

int DtlsTransport::recv_from(uint8_t*, size_t, Endpoint&, int) {
    return -1;
}

std::string DtlsTransport::negotiated_version() const { return ""; }
std::string DtlsTransport::cipher_suite() const { return ""; }

int DtlsTransport::bio_send(void*, const unsigned char*, size_t) { return -1; }
int DtlsTransport::bio_recv(void*, unsigned char*, size_t) { return -1; }
int DtlsTransport::bio_recv_timeout(void*, unsigned char*, size_t, uint32_t) { return -1; }

} // namespace protocoll

#endif // PROTOCOLL_ENABLE_DTLS
