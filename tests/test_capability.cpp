#include <gtest/gtest.h>
#include "protocoll/security/capability.h"

using namespace protocoll;

class CapabilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_keys_ = KeyPair::generate();
        client_keys_ = KeyPair::generate();
    }
    KeyPair server_keys_;
    KeyPair client_keys_;
};

TEST_F(CapabilityTest, IssueAndVerify) {
    CapabilityStore store(server_keys_, 1);
    auto token = store.issue("/app/users/*", CAP_READ | CAP_SUBSCRIBE);

    EXPECT_NE(token.token_id, 0u);
    EXPECT_EQ(token.issuer_id, 1u);
    EXPECT_EQ(token.permissions, CAP_READ | CAP_SUBSCRIBE);
    EXPECT_EQ(token.path_pattern, "/app/users/*");
    EXPECT_EQ(token.parent_token_id, 0u);

    // Verify signature
    EXPECT_TRUE(store.verify_token(token, server_keys_.public_key));
}

TEST_F(CapabilityTest, VerifyFailsWithWrongKey) {
    CapabilityStore store(server_keys_, 1);
    auto token = store.issue("/data", CAP_ALL);

    // Verify with client's key should fail
    EXPECT_FALSE(store.verify_token(token, client_keys_.public_key));
}

TEST_F(CapabilityTest, TokenCoversPath) {
    CapabilityStore store(server_keys_, 1);
    auto token = store.issue("/game/player/*/pos", CAP_READ);

    EXPECT_TRUE(token.covers(StatePath("/game/player/7/pos")));
    EXPECT_TRUE(token.covers(StatePath("/game/player/alice/pos")));
    EXPECT_FALSE(token.covers(StatePath("/game/player/7/health")));
    EXPECT_FALSE(token.covers(StatePath("/other/path")));
}

TEST_F(CapabilityTest, TokenExactPathMatch) {
    CapabilityStore store(server_keys_, 1);
    auto token = store.issue("/game/score", CAP_READ | CAP_WRITE);

    EXPECT_TRUE(token.covers(StatePath("/game/score")));
    EXPECT_FALSE(token.covers(StatePath("/game/score/sub")));
}

TEST_F(CapabilityTest, TokenExpiry) {
    CapabilityStore store(server_keys_, 1);
    auto token = store.issue("/data", CAP_READ, 5000);

    EXPECT_FALSE(token.is_expired(4999));
    EXPECT_FALSE(token.is_expired(5000));
    EXPECT_TRUE(token.is_expired(5001));
}

TEST_F(CapabilityTest, NoExpiryNeverExpires) {
    CapabilityStore store(server_keys_, 1);
    auto token = store.issue("/data", CAP_READ, 0);

    EXPECT_FALSE(token.is_expired(UINT32_MAX));
}

TEST_F(CapabilityTest, Attenuate) {
    CapabilityStore store(server_keys_, 1);
    auto parent = store.issue("/app/users/*", CAP_READ | CAP_WRITE | CAP_SUBSCRIBE);

    // Attenuate: narrow to read-only for a specific user
    auto child = store.attenuate(parent, "/app/users/alice", CAP_READ, 10000);

    EXPECT_NE(child.token_id, 0u);
    EXPECT_EQ(child.permissions, CAP_READ);
    EXPECT_EQ(child.path_pattern, "/app/users/alice");
    EXPECT_EQ(child.parent_token_id, parent.token_id);

    // Child signature should be valid
    EXPECT_TRUE(store.verify_token(child, server_keys_.public_key));
}

TEST_F(CapabilityTest, AttenuateCannotEscalate) {
    CapabilityStore store(server_keys_, 1);
    auto parent = store.issue("/data", CAP_READ);

    // Trying to add WRITE permission should fail
    auto child = store.attenuate(parent, "/data", CAP_READ | CAP_WRITE);
    EXPECT_EQ(child.token_id, 0u); // Invalid token
}

TEST_F(CapabilityTest, AttenuateExpiryConstraint) {
    CapabilityStore store(server_keys_, 1);
    auto parent = store.issue("/data", CAP_ALL, 5000);

    // Child cannot outlive parent
    auto child = store.attenuate(parent, "/data", CAP_READ, 6000);
    EXPECT_EQ(child.token_id, 0u); // Invalid

    // Child with shorter expiry is ok
    auto child2 = store.attenuate(parent, "/data", CAP_READ, 3000);
    EXPECT_NE(child2.token_id, 0u);
}

TEST_F(CapabilityTest, FindCapability) {
    CapabilityStore store(server_keys_, 1);
    store.issue("/game/player/*/pos", CAP_READ);
    store.issue("/game/score", CAP_READ | CAP_WRITE);

    auto* cap = store.find_capability(StatePath("/game/player/5/pos"), CAP_READ);
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(cap->path_pattern, "/game/player/*/pos");

    // No write permission for player pos
    EXPECT_EQ(store.find_capability(StatePath("/game/player/5/pos"), CAP_WRITE), nullptr);

    // Score has write permission
    auto* score_cap = store.find_capability(StatePath("/game/score"), CAP_WRITE);
    ASSERT_NE(score_cap, nullptr);
}

TEST_F(CapabilityTest, FindCapabilityRespectsExpiry) {
    CapabilityStore store(server_keys_, 1);
    store.issue("/data", CAP_READ, 5000);

    EXPECT_NE(store.find_capability(StatePath("/data"), CAP_READ, 4000), nullptr);
    EXPECT_EQ(store.find_capability(StatePath("/data"), CAP_READ, 6000), nullptr);
}

TEST_F(CapabilityTest, RevokeCapability) {
    CapabilityStore store(server_keys_, 1);
    auto token = store.issue("/data", CAP_ALL);
    EXPECT_EQ(store.count(), 1u);

    EXPECT_TRUE(store.revoke(token.token_id));
    EXPECT_EQ(store.count(), 0u);
    EXPECT_EQ(store.find_capability(StatePath("/data"), CAP_READ), nullptr);
}

TEST_F(CapabilityTest, GarbageCollectExpired) {
    CapabilityStore store(server_keys_, 1);
    store.issue("/a", CAP_READ, 1000);
    store.issue("/b", CAP_READ, 2000);
    store.issue("/c", CAP_READ, 0); // No expiry

    EXPECT_EQ(store.count(), 3u);
    EXPECT_EQ(store.gc(1500), 1u); // /a expired
    EXPECT_EQ(store.count(), 2u);
    EXPECT_EQ(store.gc(3000), 1u); // /b expired
    EXPECT_EQ(store.count(), 1u); // /c survives
}

TEST_F(CapabilityTest, TokenWireRoundTrip) {
    CapabilityStore store(server_keys_, 1);
    auto token = store.issue("/app/users/*/profile", CAP_READ | CAP_SUBSCRIBE, 60000);

    auto wire = token.encode();
    CapabilityToken decoded;
    ASSERT_TRUE(CapabilityToken::decode(wire.data(), wire.size(), decoded));

    EXPECT_EQ(decoded.token_id, token.token_id);
    EXPECT_EQ(decoded.issuer_id, token.issuer_id);
    EXPECT_EQ(decoded.permissions, token.permissions);
    EXPECT_EQ(decoded.path_pattern, token.path_pattern);
    EXPECT_EQ(decoded.expiry_us, token.expiry_us);
    EXPECT_EQ(decoded.parent_token_id, token.parent_token_id);
    EXPECT_EQ(decoded.signature, token.signature);

    // Decoded token should still verify
    EXPECT_TRUE(store.verify_token(decoded, server_keys_.public_key));
}

TEST_F(CapabilityTest, PeerKeyManagement) {
    CapabilityStore store(server_keys_, 1);

    // Register client's key
    store.register_peer_key(2, client_keys_.public_key);

    auto* pk = store.peer_key(2);
    ASSERT_NE(pk, nullptr);
    EXPECT_EQ(*pk, client_keys_.public_key);

    // Own key is pre-registered
    auto* own = store.peer_key(1);
    ASSERT_NE(own, nullptr);
    EXPECT_EQ(*own, server_keys_.public_key);

    // Unknown peer
    EXPECT_EQ(store.peer_key(99), nullptr);
}

TEST_F(CapabilityTest, PermissionBitfield) {
    CapabilityToken token;
    token.permissions = CAP_READ | CAP_WRITE;

    EXPECT_TRUE(token.has_permission(CAP_READ));
    EXPECT_TRUE(token.has_permission(CAP_WRITE));
    EXPECT_FALSE(token.has_permission(CAP_SUBSCRIBE));
    EXPECT_FALSE(token.has_permission(CAP_GRANT));
}

TEST_F(CapabilityTest, StoreReceivedToken) {
    // Server issues a token
    CapabilityStore server_store(server_keys_, 1);
    auto token = server_store.issue("/shared/data", CAP_READ);

    // Client receives and stores it
    CapabilityStore client_store(client_keys_, 2);
    client_store.register_peer_key(1, server_keys_.public_key);

    // Verify before storing
    EXPECT_TRUE(client_store.verify_token(token, server_keys_.public_key));
    client_store.store(token);

    EXPECT_EQ(client_store.count(), 1u);
    EXPECT_NE(client_store.find_capability(StatePath("/shared/data"), CAP_READ), nullptr);
}
