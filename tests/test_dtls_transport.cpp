#include <gtest/gtest.h>
#include <memory>
#include "protocoll/transport/dtls_transport.h"
#include "protocoll/transport/loopback_transport.h"

using namespace protocoll;

// --- Stub tests (always run, DTLS enabled or not) ---

TEST(DtlsTransport, DefaultConfigIsClient) {
    DtlsConfig cfg;
    EXPECT_FALSE(cfg.is_server);
    EXPECT_FALSE(cfg.verify_peer);
    EXPECT_EQ(cfg.handshake_timeout_ms, 5000u);
}

TEST(DtlsTransport, InitialStateIsIdle) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport lt(bus);
    DtlsConfig cfg;
    DtlsTransport dtls(lt, cfg);
    EXPECT_EQ(dtls.state(), DtlsState::IDLE);
}

TEST(DtlsTransport, BindDelegatesToInner) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport lt(bus);
    DtlsConfig cfg;
    DtlsTransport dtls(lt, cfg);
    Endpoint ep{"loopback", 9000};
    EXPECT_TRUE(dtls.bind(ep));
}

TEST(DtlsTransport, StatsInitiallyZero) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport lt(bus);
    DtlsConfig cfg;
    DtlsTransport dtls(lt, cfg);
    EXPECT_EQ(dtls.bytes_encrypted(), 0u);
    EXPECT_EQ(dtls.bytes_decrypted(), 0u);
}

TEST(DtlsTransport, NegotiatedVersionEmptyBeforeHandshake) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport lt(bus);
    DtlsConfig cfg;
    DtlsTransport dtls(lt, cfg);
    EXPECT_TRUE(dtls.negotiated_version().empty());
    EXPECT_TRUE(dtls.cipher_suite().empty());
}

TEST(DtlsTransport, CloseTransitionsState) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport lt(bus);
    DtlsConfig cfg;
    DtlsTransport dtls(lt, cfg);
    dtls.close();
    EXPECT_EQ(dtls.state(), DtlsState::CLOSED);
}

TEST(DtlsTransport, SendBeforeHandshakeFails) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport lt(bus);
    DtlsConfig cfg;
    DtlsTransport dtls(lt, cfg);
    Endpoint remote{"loopback", 9001};
    uint8_t data[] = {1, 2, 3};
    EXPECT_EQ(dtls.send_to(data, 3, remote), -1);
}

TEST(DtlsTransport, RecvBeforeHandshakeFails) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport lt(bus);
    DtlsConfig cfg;
    DtlsTransport dtls(lt, cfg);
    uint8_t buf[64];
    Endpoint from;
    EXPECT_EQ(dtls.recv_from(buf, sizeof(buf), from, 0), -1);
}

#ifndef PROTOCOLL_ENABLE_DTLS

TEST(DtlsTransport, HandshakeFailsWhenDisabled) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport lt(bus);
    DtlsConfig cfg;
    DtlsTransport dtls(lt, cfg);
    Endpoint ep{"loopback", 9000};
    dtls.bind(ep);

    Endpoint remote{"loopback", 9001};
    EXPECT_FALSE(dtls.handshake(remote, 1000));
    EXPECT_EQ(dtls.state(), DtlsState::ERROR);
}

#endif

#ifdef PROTOCOLL_ENABLE_DTLS

// Full DTLS tests using self-signed certificates
// These only compile when mbedTLS is available

#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_csr.h>

// Helper: generate self-signed cert + key PEM for testing
static std::pair<std::string, std::string> generate_test_cert() {
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_mpi serial;

    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_mpi_init(&serial);

    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
        (const unsigned char*)"test", 4);

    mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &ctr_drbg, 2048, 65537);

    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    mbedtls_x509write_crt_set_subject_name(&crt, "CN=test");
    mbedtls_x509write_crt_set_issuer_name(&crt, "CN=test");
    mbedtls_mpi_lset(&serial, 1);
    mbedtls_x509write_crt_set_serial(&crt, &serial);
    mbedtls_x509write_crt_set_validity(&crt, "20200101000000", "20301231235959");
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);

    unsigned char cert_buf[4096];
    mbedtls_x509write_crt_pem(&crt, cert_buf, sizeof(cert_buf),
        mbedtls_ctr_drbg_random, &ctr_drbg);

    unsigned char key_buf[4096];
    mbedtls_pk_write_key_pem(&key, key_buf, sizeof(key_buf));

    std::string cert_pem(reinterpret_cast<char*>(cert_buf));
    std::string key_pem(reinterpret_cast<char*>(key_buf));

    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return {cert_pem, key_pem};
}

TEST(DtlsTransport, HandshakeAndRoundTrip) {
    auto [cert, key] = generate_test_cert();

    // Shared bus so server and client can communicate
    auto bus = std::make_shared<LoopbackBus>();

    // Server
    LoopbackTransport server_lt(bus);
    DtlsConfig server_cfg;
    server_cfg.is_server = true;
    server_cfg.cert_pem = cert;
    server_cfg.key_pem = key;

    // Client
    LoopbackTransport client_lt(bus);
    DtlsConfig client_cfg;
    client_cfg.is_server = false;
    client_cfg.ca_pem = cert; // trust server's self-signed cert
    client_cfg.verify_peer = false; // skip for loopback test

    DtlsTransport server_dtls(server_lt, server_cfg);
    DtlsTransport client_dtls(client_lt, client_cfg);

    Endpoint server_ep{"loopback", 9000};
    Endpoint client_ep{"loopback", 9001};

    server_dtls.bind(server_ep);
    client_dtls.bind(client_ep);

    // Run handshake in threads (server and client need to talk simultaneously)
    std::thread server_thread([&]() {
        server_dtls.handshake(client_ep, 5000);
    });

    bool client_ok = client_dtls.handshake(server_ep, 5000);
    server_thread.join();

    // Verify handshake
    ASSERT_TRUE(client_ok);
    EXPECT_EQ(client_dtls.state(), DtlsState::CONNECTED);
    EXPECT_EQ(server_dtls.state(), DtlsState::CONNECTED);
    EXPECT_FALSE(client_dtls.negotiated_version().empty());
    EXPECT_FALSE(client_dtls.cipher_suite().empty());

    // Send data client -> server
    uint8_t send_buf[] = {0xDE, 0xAD, 0xBE, 0xEF};
    int sent = client_dtls.send_to(send_buf, 4, server_ep);
    EXPECT_EQ(sent, 4);

    uint8_t recv_buf[64];
    Endpoint from;
    int recvd = server_dtls.recv_from(recv_buf, sizeof(recv_buf), from, 1000);
    EXPECT_EQ(recvd, 4);
    EXPECT_EQ(recv_buf[0], 0xDE);
    EXPECT_EQ(recv_buf[3], 0xEF);

    // Stats
    EXPECT_GT(client_dtls.bytes_encrypted(), 0u);
    EXPECT_GT(server_dtls.bytes_decrypted(), 0u);

    client_dtls.close();
    server_dtls.close();
}

TEST(DtlsTransport, HandshakeTimeoutOnNoResponse) {
    auto bus = std::make_shared<LoopbackBus>();
    LoopbackTransport lt(bus);
    DtlsConfig cfg;
    cfg.cert_pem = ""; // empty — no cert
    DtlsTransport dtls(lt, cfg);
    Endpoint ep{"loopback", 9000};
    dtls.bind(ep);

    Endpoint remote{"loopback", 9999}; // no listener
    EXPECT_FALSE(dtls.handshake(remote, 500)); // short timeout
    EXPECT_EQ(dtls.state(), DtlsState::ERROR);
}

#endif // PROTOCOLL_ENABLE_DTLS
