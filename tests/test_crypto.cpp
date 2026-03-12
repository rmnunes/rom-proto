#include <gtest/gtest.h>
#include "protocoll/security/crypto.h"
#include <cstring>

using namespace protocoll;

TEST(KeyPair, Generate) {
    auto kp = KeyPair::generate();
    // Public key should not be all zeros
    bool all_zero = true;
    for (auto b : kp.public_key) { if (b != 0) { all_zero = false; break; } }
    EXPECT_FALSE(all_zero);
}

TEST(KeyPair, FromSeedDeterministic) {
    Seed seed{};
    seed[0] = 42;

    auto kp1 = KeyPair::from_seed(seed);
    auto kp2 = KeyPair::from_seed(seed);

    EXPECT_EQ(kp1.public_key, kp2.public_key);
    EXPECT_EQ(kp1.secret_key, kp2.secret_key);
}

TEST(KeyPair, DifferentSeedsDifferentKeys) {
    Seed s1{}, s2{};
    s1[0] = 1;
    s2[0] = 2;

    auto kp1 = KeyPair::from_seed(s1);
    auto kp2 = KeyPair::from_seed(s2);

    EXPECT_NE(kp1.public_key, kp2.public_key);
}

TEST(Ed25519, SignAndVerify) {
    auto kp = KeyPair::generate();
    uint8_t msg[] = "Hello, ProtoCol!";
    size_t msg_len = sizeof(msg) - 1;

    auto sig = sign(kp.secret_key, msg, msg_len);
    EXPECT_TRUE(verify(kp.public_key, sig, msg, msg_len));
}

TEST(Ed25519, VerifyFailsWrongMessage) {
    auto kp = KeyPair::generate();
    uint8_t msg[] = "Original message";
    uint8_t tampered[] = "Tampered message";

    auto sig = sign(kp.secret_key, msg, sizeof(msg) - 1);
    EXPECT_FALSE(verify(kp.public_key, sig, tampered, sizeof(tampered) - 1));
}

TEST(Ed25519, VerifyFailsWrongKey) {
    auto kp1 = KeyPair::generate();
    auto kp2 = KeyPair::generate();
    uint8_t msg[] = "Test message";

    auto sig = sign(kp1.secret_key, msg, sizeof(msg) - 1);
    // Verify with wrong public key should fail
    EXPECT_FALSE(verify(kp2.public_key, sig, msg, sizeof(msg) - 1));
}

TEST(Ed25519, VerifyFailsTamperedSignature) {
    auto kp = KeyPair::generate();
    uint8_t msg[] = "Test message";

    auto sig = sign(kp.secret_key, msg, sizeof(msg) - 1);
    sig[0] ^= 0xFF; // Flip bits in signature
    EXPECT_FALSE(verify(kp.public_key, sig, msg, sizeof(msg) - 1));
}

TEST(Ed25519, SignEmptyMessage) {
    auto kp = KeyPair::generate();
    auto sig = sign(kp.secret_key, nullptr, 0);
    EXPECT_TRUE(verify(kp.public_key, sig, nullptr, 0));
}

TEST(Ed25519, SignLargeMessage) {
    auto kp = KeyPair::generate();
    std::vector<uint8_t> msg(10000, 0xAB);
    auto sig = sign(kp.secret_key, msg.data(), msg.size());
    EXPECT_TRUE(verify(kp.public_key, sig, msg.data(), msg.size()));
}

TEST(RandomBytes, ProducesOutput) {
    uint8_t buf[32] = {};
    random_bytes(buf, sizeof(buf));
    // Extremely unlikely to be all zeros
    bool all_zero = true;
    for (auto b : buf) { if (b != 0) { all_zero = false; break; } }
    EXPECT_FALSE(all_zero);
}

TEST(RandomBytes, ProducesDifferentOutput) {
    uint8_t buf1[32], buf2[32];
    random_bytes(buf1, sizeof(buf1));
    random_bytes(buf2, sizeof(buf2));
    EXPECT_NE(std::memcmp(buf1, buf2, sizeof(buf1)), 0);
}
