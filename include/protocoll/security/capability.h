#pragma once

// Capability: unforgeable, attenuatable access tokens.
//
// Design inspired by Cap'n Proto / E Language capability model:
//   - No ambient authority: you can only access what you have a token for
//   - Attenuation: narrow a capability (e.g., read+write → read-only)
//   - Delegation: pass your capability to a third party
//   - Expiry: capabilities have a deadline
//
// Each capability token is:
//   - Bound to a state path pattern (e.g., /app/users/alice/*)
//   - Limited to specific operations (READ, WRITE, SUBSCRIBE)
//   - Time-limited (expiry timestamp)
//   - Signed by the issuer's Ed25519 key
//   - Verifiable by anyone who has the issuer's public key

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>

#include "protocoll/security/crypto.h"
#include "protocoll/state/state_path.h"

namespace protocoll {

// Capability permissions (bitfield)
enum CapabilityPermission : uint8_t {
    CAP_NONE      = 0x00,
    CAP_READ      = 0x01,  // Can receive state updates
    CAP_WRITE     = 0x02,  // Can send state mutations
    CAP_SUBSCRIBE = 0x04,  // Can subscribe to path patterns
    CAP_GRANT     = 0x08,  // Can delegate (sub)capabilities to others
    CAP_ALL       = 0x0F,
};

// A capability token: signed proof of access
struct CapabilityToken {
    uint32_t    token_id;        // Unique identifier
    uint32_t    issuer_id;       // Node ID of issuer (maps to public key)
    uint8_t     permissions;     // Bitfield of CapabilityPermission
    std::string path_pattern;    // State path pattern (supports wildcards)
    uint32_t    expiry_us;       // Expiry timestamp (0 = no expiry)
    uint32_t    parent_token_id; // 0 if root capability, else derived from parent
    Signature   signature;       // Ed25519 signature over all fields above

    // Encode the signable portion (everything except signature)
    std::vector<uint8_t> signable_bytes() const;

    // Wire encode/decode (full token including signature)
    std::vector<uint8_t> encode() const;
    static bool decode(const uint8_t* data, size_t len, CapabilityToken& out);

    // Check if this token grants a specific permission
    bool has_permission(CapabilityPermission perm) const {
        return (permissions & perm) != 0;
    }

    // Check if this token covers a given state path
    bool covers(const StatePath& path) const;

    // Check if token has expired
    bool is_expired(uint32_t now_us) const {
        if (expiry_us == 0) return false;
        return now_us > expiry_us;
    }
};

// Manages capability tokens for a node
class CapabilityStore {
public:
    explicit CapabilityStore(const KeyPair& node_keys, uint32_t node_id);

    // Issue a new root capability (only for server/authority nodes)
    CapabilityToken issue(const std::string& path_pattern,
                          uint8_t permissions,
                          uint32_t expiry_us = 0);

    // Attenuate an existing capability: create a narrower sub-capability.
    // The new capability must be a subset of the parent's permissions and path.
    // Returns empty optional-like (token_id=0) if attenuation is invalid.
    CapabilityToken attenuate(const CapabilityToken& parent,
                              const std::string& narrower_path,
                              uint8_t reduced_permissions,
                              uint32_t expiry_us = 0);

    // Verify a token's signature against a known public key
    bool verify_token(const CapabilityToken& token, const PublicKey& issuer_pk) const;

    // Store a received capability (from a CAPABILITY_GRANT frame)
    void store(CapabilityToken token);

    // Revoke a capability by ID
    bool revoke(uint32_t token_id);

    // Check if we have a valid capability for a path + permission
    const CapabilityToken* find_capability(const StatePath& path,
                                           CapabilityPermission perm,
                                           uint32_t now_us = 0) const;

    // Get all stored capabilities
    size_t count() const { return tokens_.size(); }

    // Remove expired tokens
    size_t gc(uint32_t now_us);

    // Register a peer's public key (for signature verification)
    void register_peer_key(uint32_t node_id, const PublicKey& pk);

    // Look up a peer's public key
    const PublicKey* peer_key(uint32_t node_id) const;

private:
    KeyPair keys_;
    uint32_t node_id_;
    uint32_t next_token_id_ = 1;

    // token_id → token
    std::unordered_map<uint32_t, CapabilityToken> tokens_;

    // node_id → public key (for verifying tokens from other nodes)
    std::unordered_map<uint32_t, PublicKey> peer_keys_;
};

} // namespace protocoll
