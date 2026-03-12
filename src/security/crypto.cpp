#include "protocoll/security/crypto.h"

extern "C" {
#include "monocypher.h"
#include "monocypher-ed25519.h"
}

#include <stdexcept>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <fstream>
#endif

namespace protocoll {

void random_bytes(uint8_t* buf, size_t len) {
#ifdef _WIN32
    NTSTATUS status = BCryptGenRandom(NULL, buf, static_cast<ULONG>(len),
                                       BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) {
        throw std::runtime_error("BCryptGenRandom failed");
    }
#else
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (!urandom || !urandom.read(reinterpret_cast<char*>(buf), len)) {
        throw std::runtime_error("Failed to read /dev/urandom");
    }
#endif
}

KeyPair KeyPair::generate() {
    Seed seed;
    random_bytes(seed.data(), seed.size());
    return from_seed(seed);
}

KeyPair KeyPair::from_seed(const Seed& seed) {
    KeyPair kp;

    // Monocypher mutates the seed buffer during key derivation, so copy it
    Seed seed_copy = seed;
    crypto_ed25519_key_pair(kp.secret_key.data(), kp.public_key.data(), seed_copy.data());

    return kp;
}

Signature sign(const SecretKey& sk, const uint8_t* msg, size_t msg_len) {
    Signature sig;
    // Monocypher ed25519 sign: uses secret_key (64 bytes = seed+pk)
    crypto_ed25519_sign(sig.data(), sk.data(), msg, msg_len);
    return sig;
}

bool verify(const PublicKey& pk, const Signature& sig,
            const uint8_t* msg, size_t msg_len) {
    return crypto_ed25519_check(sig.data(), pk.data(), msg, msg_len) == 0;
}

} // namespace protocoll
