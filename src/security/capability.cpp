#include "protocoll/security/capability.h"
#include "protocoll/util/platform.h"
#include <cstring>
#include <algorithm>

namespace protocoll {

// --- CapabilityToken ---

std::vector<uint8_t> CapabilityToken::signable_bytes() const {
    // Layout: token_id(4) + issuer_id(4) + permissions(1) + expiry_us(4)
    //       + parent_token_id(4) + pattern_len(2) + pattern_bytes
    size_t size = 4 + 4 + 1 + 4 + 4 + 2 + path_pattern.size();
    std::vector<uint8_t> buf(size);
    size_t off = 0;

    write_u32(buf.data() + off, token_id);       off += 4;
    write_u32(buf.data() + off, issuer_id);       off += 4;
    buf[off] = permissions;                       off += 1;
    write_u32(buf.data() + off, expiry_us);       off += 4;
    write_u32(buf.data() + off, parent_token_id); off += 4;
    write_u16(buf.data() + off, static_cast<uint16_t>(path_pattern.size())); off += 2;
    if (!path_pattern.empty()) {
        std::memcpy(buf.data() + off, path_pattern.data(), path_pattern.size());
    }

    return buf;
}

std::vector<uint8_t> CapabilityToken::encode() const {
    auto signable = signable_bytes();
    // Full wire: signable_bytes + signature(64)
    std::vector<uint8_t> buf(signable.size() + ED25519_SIGNATURE_SIZE);
    std::memcpy(buf.data(), signable.data(), signable.size());
    std::memcpy(buf.data() + signable.size(), signature.data(), ED25519_SIGNATURE_SIZE);
    return buf;
}

bool CapabilityToken::decode(const uint8_t* data, size_t len, CapabilityToken& out) {
    // Minimum: 4+4+1+4+4+2 + 0(pattern) + 64(sig) = 83 bytes
    if (len < 83) return false;

    size_t off = 0;
    out.token_id       = read_u32(data + off); off += 4;
    out.issuer_id      = read_u32(data + off); off += 4;
    out.permissions    = data[off];            off += 1;
    out.expiry_us      = read_u32(data + off); off += 4;
    out.parent_token_id = read_u32(data + off); off += 4;

    uint16_t pat_len = read_u16(data + off); off += 2;
    if (off + pat_len + ED25519_SIGNATURE_SIZE > len) return false;

    out.path_pattern.assign(reinterpret_cast<const char*>(data + off), pat_len);
    off += pat_len;

    std::memcpy(out.signature.data(), data + off, ED25519_SIGNATURE_SIZE);
    return true;
}

bool CapabilityToken::covers(const StatePath& path) const {
    StatePath pattern_path(path_pattern);
    return path.matches(pattern_path);
}

// --- CapabilityStore ---

CapabilityStore::CapabilityStore(const KeyPair& node_keys, uint32_t node_id)
    : keys_(node_keys), node_id_(node_id)
{
    // Register our own public key
    peer_keys_[node_id] = keys_.public_key;
}

CapabilityToken CapabilityStore::issue(const std::string& path_pattern,
                                        uint8_t permissions,
                                        uint32_t expiry_us) {
    CapabilityToken token;
    token.token_id = next_token_id_++;
    token.issuer_id = node_id_;
    token.permissions = permissions;
    token.path_pattern = path_pattern;
    token.expiry_us = expiry_us;
    token.parent_token_id = 0; // Root capability

    auto signable = token.signable_bytes();
    token.signature = sign(keys_.secret_key, signable.data(), signable.size());

    tokens_[token.token_id] = token;
    return token;
}

CapabilityToken CapabilityStore::attenuate(const CapabilityToken& parent,
                                            const std::string& narrower_path,
                                            uint8_t reduced_permissions,
                                            uint32_t expiry_us) {
    CapabilityToken invalid{};
    invalid.token_id = 0;

    // Reduced permissions must be a subset of parent's
    if ((reduced_permissions & ~parent.permissions) != 0) return invalid;

    // If parent has expiry, child must expire at or before parent
    if (parent.expiry_us != 0) {
        if (expiry_us == 0 || expiry_us > parent.expiry_us) return invalid;
    }

    // The narrower path must be covered by the parent pattern
    // (We check that the narrower pattern is a specialization)
    StatePath parent_pattern(parent.path_pattern);
    StatePath child_pattern(narrower_path);
    // Simple check: child must match against parent pattern
    if (!child_pattern.matches(parent_pattern) &&
        narrower_path != parent.path_pattern) {
        // Allow if child path is longer (more specific) or same
        // For wildcards, the parent pattern must cover the child pattern segments
        if (parent_pattern.segments().size() != child_pattern.segments().size()) {
            return invalid;
        }
    }

    CapabilityToken token;
    token.token_id = next_token_id_++;
    token.issuer_id = node_id_;
    token.permissions = reduced_permissions;
    token.path_pattern = narrower_path;
    token.expiry_us = expiry_us;
    token.parent_token_id = parent.token_id;

    auto signable = token.signable_bytes();
    token.signature = sign(keys_.secret_key, signable.data(), signable.size());

    tokens_[token.token_id] = token;
    return token;
}

bool CapabilityStore::verify_token(const CapabilityToken& token, const PublicKey& issuer_pk) const {
    auto signable = token.signable_bytes();
    return verify(issuer_pk, token.signature, signable.data(), signable.size());
}

void CapabilityStore::store(CapabilityToken token) {
    tokens_[token.token_id] = std::move(token);
}

bool CapabilityStore::revoke(uint32_t token_id) {
    return tokens_.erase(token_id) > 0;
}

const CapabilityToken* CapabilityStore::find_capability(const StatePath& path,
                                                         CapabilityPermission perm,
                                                         uint32_t now_us) const {
    for (const auto& [id, token] : tokens_) {
        if (token.is_expired(now_us)) continue;
        if (!token.has_permission(perm)) continue;
        if (token.covers(path)) return &token;
    }
    return nullptr;
}

size_t CapabilityStore::gc(uint32_t now_us) {
    size_t removed = 0;
    for (auto it = tokens_.begin(); it != tokens_.end();) {
        if (it->second.is_expired(now_us)) {
            it = tokens_.erase(it);
            removed++;
        } else {
            ++it;
        }
    }
    return removed;
}

void CapabilityStore::register_peer_key(uint32_t node_id, const PublicKey& pk) {
    peer_keys_[node_id] = pk;
}

const PublicKey* CapabilityStore::peer_key(uint32_t node_id) const {
    auto it = peer_keys_.find(node_id);
    return it != peer_keys_.end() ? &it->second : nullptr;
}

} // namespace protocoll
