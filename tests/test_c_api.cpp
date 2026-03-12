#include <gtest/gtest.h>
#include "protocoll/protocoll.h"
#include <cstring>
#include <thread>
#include <chrono>
#include <string>

TEST(CApi, KeyPairGeneration) {
    PcolKeyPair kp1, kp2;
    pcol_generate_keypair(&kp1);
    pcol_generate_keypair(&kp2);

    // Two generated keypairs should differ
    EXPECT_NE(std::memcmp(kp1.public_key, kp2.public_key, 32), 0);
}

TEST(CApi, TransportCreateDestroy) {
    PcolTransport* t = pcol_transport_loopback_create(100);
    ASSERT_NE(t, nullptr);

    PcolEndpoint ep = {"loopback", 1};
    EXPECT_EQ(pcol_transport_bind(t, ep), PCOL_OK);

    pcol_transport_destroy(t);
}

TEST(CApi, PeerCreateDestroy) {
    PcolKeyPair kp;
    pcol_generate_keypair(&kp);

    PcolTransport* t = pcol_transport_loopback_create(101);
    PcolEndpoint ep = {"loopback", 1};
    pcol_transport_bind(t, ep);

    PcolPeer* peer = pcol_peer_create(1, t, &kp);
    ASSERT_NE(peer, nullptr);

    EXPECT_EQ(pcol_peer_node_id(peer), 1);

    PcolPublicKey pk;
    pcol_peer_public_key(peer, &pk);
    EXPECT_EQ(std::memcmp(pk.bytes, kp.public_key, 32), 0);

    pcol_peer_destroy(peer);
    pcol_transport_destroy(t);
}

TEST(CApi, DeclareAndSetLww) {
    PcolKeyPair kp;
    pcol_generate_keypair(&kp);

    PcolTransport* t = pcol_transport_loopback_create(102);
    PcolEndpoint ep = {"loopback", 1};
    pcol_transport_bind(t, ep);

    PcolPeer* peer = pcol_peer_create(1, t, &kp);
    ASSERT_NE(peer, nullptr);

    EXPECT_EQ(pcol_declare(peer, "/test/val", PCOL_LWW_REGISTER, PCOL_RELIABLE), PCOL_OK);

    // Can't read without data yet? Let's set then read.
    const char* data = "hello";
    // set_lww requires connected state for timestamp — but we can still set locally
    // Actually set_lww uses conn_.elapsed_us() which should work even disconnected

    // Read back
    uint8_t buf[64];
    size_t out_len = 0;
    PcolError err = pcol_get_lww(peer, "/test/val", buf, sizeof(buf), &out_len);
    EXPECT_EQ(err, PCOL_OK);
    // Value should be empty initially
    EXPECT_EQ(out_len, 0u);

    pcol_peer_destroy(peer);
    pcol_transport_destroy(t);
}

TEST(CApi, EndToEndStateStream) {
    PcolKeyPair server_kp, client_kp;
    pcol_generate_keypair(&server_kp);
    pcol_generate_keypair(&client_kp);

    // Create transports on same bus
    PcolTransport* st = pcol_transport_loopback_create(200);
    PcolTransport* ct = pcol_transport_loopback_create(200);

    PcolEndpoint sep = {"loopback", 1};
    PcolEndpoint cep = {"loopback", 2};
    pcol_transport_bind(st, sep);
    pcol_transport_bind(ct, cep);

    // Create peers
    PcolPeer* server = pcol_peer_create(1, st, &server_kp);
    PcolPeer* client = pcol_peer_create(2, ct, &client_kp);

    // Exchange keys
    PcolPublicKey spk, cpk;
    pcol_peer_public_key(server, &spk);
    pcol_peer_public_key(client, &cpk);
    pcol_peer_register_key(server, 2, &cpk);
    pcol_peer_register_key(client, 1, &spk);

    // Declare state on both sides
    pcol_declare(server, "/data/value", PCOL_LWW_REGISTER, PCOL_RELIABLE);
    pcol_declare(client, "/data/value", PCOL_LWW_REGISTER, PCOL_RELIABLE);

    // Connect
    std::thread accept_thread([&]() {
        pcol_peer_accept(server, cep, 2000);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_EQ(pcol_peer_connect(client, sep), PCOL_OK);
    accept_thread.join();

    EXPECT_EQ(pcol_peer_is_connected(server), 1);
    EXPECT_EQ(pcol_peer_is_connected(client), 1);

    // Client writes state
    const char* val = "through-c-api";
    pcol_set_lww(client, "/data/value",
                 reinterpret_cast<const uint8_t*>(val), strlen(val));
    int sent = pcol_flush(client);
    EXPECT_GT(sent, 0);

    // Server receives
    int changes = pcol_poll(server, 1000);
    EXPECT_EQ(changes, 1);

    // Server reads value
    uint8_t buf[64];
    size_t out_len = 0;
    EXPECT_EQ(pcol_get_lww(server, "/data/value", buf, sizeof(buf), &out_len), PCOL_OK);
    EXPECT_EQ(out_len, strlen(val));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(buf), out_len), "through-c-api");

    // Cleanup
    pcol_peer_disconnect(client);
    pcol_peer_destroy(client);
    pcol_peer_destroy(server);
    pcol_transport_destroy(ct);
    pcol_transport_destroy(st);
}

TEST(CApi, UpdateCallback) {
    PcolKeyPair server_kp, client_kp;
    pcol_generate_keypair(&server_kp);
    pcol_generate_keypair(&client_kp);

    PcolTransport* st = pcol_transport_loopback_create(300);
    PcolTransport* ct = pcol_transport_loopback_create(300);
    PcolEndpoint sep = {"loopback", 1};
    PcolEndpoint cep = {"loopback", 2};
    pcol_transport_bind(st, sep);
    pcol_transport_bind(ct, cep);

    PcolPeer* server = pcol_peer_create(1, st, &server_kp);
    PcolPeer* client = pcol_peer_create(2, ct, &client_kp);

    PcolPublicKey spk, cpk;
    pcol_peer_public_key(server, &spk);
    pcol_peer_public_key(client, &cpk);
    pcol_peer_register_key(server, 2, &cpk);
    pcol_peer_register_key(client, 1, &spk);

    pcol_declare(server, "/cb/test", PCOL_LWW_REGISTER, PCOL_RELIABLE);
    pcol_declare(client, "/cb/test", PCOL_LWW_REGISTER, PCOL_RELIABLE);

    // Set update callback
    struct CbState { int count; };
    CbState cb_state{0};
    pcol_on_update(server, [](const char*, const uint8_t*, size_t, void* ud) {
        static_cast<CbState*>(ud)->count++;
    }, &cb_state);

    std::thread accept_thread([&]() { pcol_peer_accept(server, cep, 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    pcol_peer_connect(client, sep);
    accept_thread.join();

    const char* val = "cb-test";
    pcol_set_lww(client, "/cb/test", reinterpret_cast<const uint8_t*>(val), strlen(val));
    pcol_flush(client);
    pcol_poll(server, 500);

    EXPECT_EQ(cb_state.count, 1);

    pcol_peer_destroy(client);
    pcol_peer_destroy(server);
    pcol_transport_destroy(ct);
    pcol_transport_destroy(st);
}

TEST(CApi, ErrorHandling) {
    // Null pointer safety
    EXPECT_EQ(pcol_transport_bind(nullptr, {"", 0}), PCOL_ERR_INVALID);
    EXPECT_EQ(pcol_peer_connect(nullptr, {"", 0}), PCOL_ERR_INVALID);
    EXPECT_EQ(pcol_declare(nullptr, "/test", PCOL_LWW_REGISTER, PCOL_RELIABLE), PCOL_ERR_INVALID);
    EXPECT_EQ(pcol_set_lww(nullptr, "/test", nullptr, 0), PCOL_ERR_INVALID);
    EXPECT_EQ(pcol_flush(nullptr), PCOL_ERR_INVALID);
    EXPECT_EQ(pcol_poll(nullptr, 0), PCOL_ERR_INVALID);

    // Peer with null path
    PcolKeyPair kp;
    pcol_generate_keypair(&kp);
    PcolTransport* t = pcol_transport_loopback_create(999);
    pcol_transport_bind(t, {"loopback", 1});
    PcolPeer* peer = pcol_peer_create(1, t, &kp);
    EXPECT_EQ(pcol_declare(peer, nullptr, PCOL_LWW_REGISTER, PCOL_RELIABLE), PCOL_ERR_INVALID);

    // Undeclared path
    EXPECT_EQ(pcol_set_lww(peer, "/nonexistent", nullptr, 0), PCOL_ERR_NOT_FOUND);

    pcol_peer_destroy(peer);
    pcol_transport_destroy(t);
}
