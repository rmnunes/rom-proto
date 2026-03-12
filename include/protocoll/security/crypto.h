#pragma once

// Crypto: Ed25519 signing and verification.
//
// "Trust the data, not the channel." — Every STATE_DELTA and STATE_SNAPSHOT
// is signed by its author. Verification is independent of transport path.
// Data can be cached, relayed, or gossipped without losing trust.
//
// Uses Monocypher (audited single-file C library) for Ed25519 (RFC 8032).

#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>

namespace protocoll {

// Ed25519 constants
constexpr size_t ED25519_PUBLIC_KEY_SIZE = 32;
constexpr size_t ED25519_SECRET_KEY_SIZE = 64; // seed(32) + public(32)
constexpr size_t ED25519_SEED_SIZE       = 32;
constexpr size_t ED25519_SIGNATURE_SIZE  = 64;

using PublicKey = std::array<uint8_t, ED25519_PUBLIC_KEY_SIZE>;
using SecretKey = std::array<uint8_t, ED25519_SECRET_KEY_SIZE>;
using Seed      = std::array<uint8_t, ED25519_SEED_SIZE>;
using Signature = std::array<uint8_t, ED25519_SIGNATURE_SIZE>;

struct KeyPair {
    PublicKey public_key;
    SecretKey secret_key; // Contains seed + public_key

    // Generate a new random key pair
    static KeyPair generate();

    // Derive key pair from a known seed (deterministic)
    static KeyPair from_seed(const Seed& seed);
};

// Sign a message with the secret key. Returns 64-byte Ed25519 signature.
Signature sign(const SecretKey& sk, const uint8_t* msg, size_t msg_len);

// Verify a signature against a public key and message.
bool verify(const PublicKey& pk, const Signature& sig,
            const uint8_t* msg, size_t msg_len);

// Fill buffer with cryptographically secure random bytes.
// Uses BCryptGenRandom (Windows) or /dev/urandom (Linux/macOS).
void random_bytes(uint8_t* buf, size_t len);

} // namespace protocoll
