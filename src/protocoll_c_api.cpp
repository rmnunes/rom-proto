#include "protocoll/protocoll.h"
#include "protocoll/peer.h"
#include "protocoll/security/crypto.h"
#include "protocoll/transport/transport.h"
#include "protocoll/transport/loopback_transport.h"
#include "protocoll/transport/external_transport.h"
#ifndef __EMSCRIPTEN__
#include "protocoll/transport/udp_transport.h"
#include "protocoll/transport/dtls_transport.h"
#endif
#include "protocoll/state/crdt/lww_register.h"
#include "protocoll/state/crdt/g_counter.h"
#include "protocoll/state/state_path.h"
#include "protocoll/state/subscription.h"

#include <cstring>
#include <unordered_map>
#include <memory>
#include <mutex>

using namespace protocoll;

// --- Internal wrappers ---

struct PcolTransport {
    std::unique_ptr<Transport> impl;
    // For loopback: keep bus alive
    std::shared_ptr<LoopbackBus> bus;
};

struct PcolPeer {
    std::unique_ptr<Peer> impl;
    // Store C callbacks + user_data
    PcolUpdateCallback update_cb = nullptr;
    void* update_ud = nullptr;
    PcolSigFailureCallback sig_cb = nullptr;
    void* sig_ud = nullptr;
};

// Loopback bus registry (keyed by bus_id)
static std::mutex s_bus_mutex;
static std::unordered_map<uint32_t, std::weak_ptr<LoopbackBus>> s_buses;

static Endpoint to_ep(PcolEndpoint ep) {
    return Endpoint{ep.address ? ep.address : "", ep.port};
}

static CrdtType to_crdt(PcolCrdtType t) {
    switch (t) {
    case PCOL_LWW_REGISTER: return CrdtType::LWW_REGISTER;
    case PCOL_G_COUNTER:    return CrdtType::G_COUNTER;
    case PCOL_PN_COUNTER:   return CrdtType::PN_COUNTER;
    case PCOL_OR_SET:       return CrdtType::OR_SET;
    default:                return CrdtType::LWW_REGISTER;
    }
}

static Reliability to_rel(PcolReliability r) {
    return r == PCOL_BEST_EFFORT ? Reliability::BEST_EFFORT : Reliability::RELIABLE;
}

static KeyPair to_kp(const PcolKeyPair* k) {
    KeyPair kp;
    std::memcpy(kp.public_key.data(), k->public_key, 32);
    std::memcpy(kp.secret_key.data(), k->secret_key, 64);
    return kp;
}

// --- Key generation ---

void pcol_generate_keypair(PcolKeyPair* out) {
    if (!out) return;
    auto kp = KeyPair::generate();
    std::memcpy(out->public_key, kp.public_key.data(), 32);
    std::memcpy(out->secret_key, kp.secret_key.data(), 64);
}

// --- Transport ---

PcolTransport* pcol_transport_loopback_create(uint32_t bus_id) {
    auto* t = new PcolTransport();
    {
        std::lock_guard<std::mutex> lock(s_bus_mutex);
        auto it = s_buses.find(bus_id);
        if (it != s_buses.end()) {
            t->bus = it->second.lock();
        }
        if (!t->bus) {
            t->bus = std::make_shared<LoopbackBus>();
            s_buses[bus_id] = t->bus;
        }
    }
    t->impl = std::make_unique<LoopbackTransport>(t->bus);
    return t;
}

#ifndef __EMSCRIPTEN__
PcolTransport* pcol_transport_udp_create(void) {
    auto* t = new PcolTransport();
    t->impl = std::make_unique<UdpTransport>();
    return t;
}
#else
PcolTransport* pcol_transport_udp_create(void) {
    return nullptr; // UDP not available in WASM
}
#endif

PcolError pcol_transport_bind(PcolTransport* t, PcolEndpoint ep) {
    if (!t || !t->impl) return PCOL_ERR_INVALID;
    return t->impl->bind(to_ep(ep)) ? PCOL_OK : PCOL_ERR_INVALID;
}

void pcol_transport_destroy(PcolTransport* t) {
    delete t;
}

// --- External Transport ---

PcolTransport* pcol_transport_external_create(void) {
    auto* t = new PcolTransport();
    t->impl = std::make_unique<ExternalTransport>();
    return t;
}

PcolError pcol_transport_external_push_recv(PcolTransport* t, const uint8_t* data,
                                              size_t len, const char* from_addr,
                                              uint16_t from_port) {
    if (!t || !t->impl || !data || len == 0) return PCOL_ERR_INVALID;
    auto* ext = dynamic_cast<ExternalTransport*>(t->impl.get());
    if (!ext) return PCOL_ERR_INVALID;
    Endpoint from{from_addr ? from_addr : "", from_port};
    ext->push_recv(data, len, from);
    return PCOL_OK;
}

PcolError pcol_transport_external_pop_send(PcolTransport* t, uint8_t* buf,
                                             size_t buf_len, size_t* out_len,
                                             char* to_addr_buf, size_t to_addr_buf_len,
                                             uint16_t* to_port) {
    if (!t || !t->impl || !buf || !out_len) return PCOL_ERR_INVALID;
    auto* ext = dynamic_cast<ExternalTransport*>(t->impl.get());
    if (!ext) return PCOL_ERR_INVALID;

    ExternalTransport::Packet pkt;
    if (!ext->pop_send(pkt)) return PCOL_ERR_NOT_FOUND;

    size_t copy_len = pkt.data.size() < buf_len ? pkt.data.size() : buf_len;
    std::memcpy(buf, pkt.data.data(), copy_len);
    *out_len = pkt.data.size();

    if (to_addr_buf && to_addr_buf_len > 0) {
        size_t addr_len = pkt.endpoint.address.size();
        size_t addr_copy = addr_len < (to_addr_buf_len - 1) ? addr_len : (to_addr_buf_len - 1);
        std::memcpy(to_addr_buf, pkt.endpoint.address.c_str(), addr_copy);
        to_addr_buf[addr_copy] = '\0';
    }
    if (to_port) *to_port = pkt.endpoint.port;

    return PCOL_OK;
}

size_t pcol_transport_external_send_queue_size(PcolTransport* t) {
    if (!t || !t->impl) return 0;
    auto* ext = dynamic_cast<ExternalTransport*>(t->impl.get());
    if (!ext) return 0;
    return ext->send_queue_size();
}

// --- DTLS Transport ---

#ifndef __EMSCRIPTEN__
PcolTransport* pcol_transport_dtls_create(PcolTransport* inner,
                                            const PcolDtlsConfig* config) {
    if (!inner || !inner->impl || !config) return nullptr;

    DtlsConfig cfg;
    cfg.is_server = config->is_server != 0;
    if (config->cert_pem) cfg.cert_pem = config->cert_pem;
    if (config->key_pem) cfg.key_pem = config->key_pem;
    if (config->ca_pem) cfg.ca_pem = config->ca_pem;
    cfg.verify_peer = config->verify_peer != 0;
    cfg.handshake_timeout_ms = config->handshake_timeout_ms > 0
        ? config->handshake_timeout_ms : 5000;

    auto* t = new PcolTransport();
    t->impl = std::make_unique<DtlsTransport>(*inner->impl, cfg);
    return t;
}

PcolError pcol_transport_dtls_handshake(PcolTransport* t, PcolEndpoint remote,
                                          int timeout_ms) {
    if (!t || !t->impl) return PCOL_ERR_INVALID;

    auto* dtls = dynamic_cast<DtlsTransport*>(t->impl.get());
    if (!dtls) return PCOL_ERR_INVALID;

    return dtls->handshake(to_ep(remote), timeout_ms) ? PCOL_OK : PCOL_ERR_TIMEOUT;
}
#else
PcolTransport* pcol_transport_dtls_create(PcolTransport*, const PcolDtlsConfig*) {
    return nullptr; // DTLS not available in WASM
}

PcolError pcol_transport_dtls_handshake(PcolTransport*, PcolEndpoint, int) {
    return PCOL_ERR_INVALID; // DTLS not available in WASM
}
#endif

// --- Peer ---

PcolPeer* pcol_peer_create(uint16_t node_id, PcolTransport* transport,
                            const PcolKeyPair* keys) {
    if (!transport || !transport->impl || !keys) return nullptr;
    auto kp = to_kp(keys);
    auto* p = new PcolPeer();
    p->impl = std::make_unique<Peer>(node_id, *transport->impl, kp);
    return p;
}

void pcol_peer_destroy(PcolPeer* peer) {
    if (peer && peer->impl && peer->impl->is_connected()) {
        peer->impl->disconnect();
    }
    delete peer;
}

uint16_t pcol_peer_node_id(const PcolPeer* peer) {
    return peer ? peer->impl->node_id() : 0;
}

void pcol_peer_public_key(const PcolPeer* peer, PcolPublicKey* out) {
    if (!peer || !out) return;
    std::memcpy(out->bytes, peer->impl->public_key().data(), 32);
}

void pcol_peer_register_key(PcolPeer* peer, uint16_t remote_node_id,
                             const PcolPublicKey* pk) {
    if (!peer || !pk) return;
    PublicKey key;
    std::memcpy(key.data(), pk->bytes, 32);
    peer->impl->register_peer_key(remote_node_id, key);
}

// --- Connection ---

PcolError pcol_peer_connect(PcolPeer* peer, PcolEndpoint remote) {
    if (!peer) return PCOL_ERR_INVALID;
    return peer->impl->connect(to_ep(remote)) ? PCOL_OK : PCOL_ERR_TIMEOUT;
}

PcolError pcol_peer_accept(PcolPeer* peer, PcolEndpoint expected_from, int timeout_ms) {
    if (!peer) return PCOL_ERR_INVALID;
    return peer->impl->accept_from(to_ep(expected_from), timeout_ms) ? PCOL_OK : PCOL_ERR_TIMEOUT;
}

int pcol_peer_is_connected(const PcolPeer* peer) {
    return (peer && peer->impl->is_connected()) ? 1 : 0;
}

void pcol_peer_disconnect(PcolPeer* peer) {
    if (peer) peer->impl->disconnect();
}

void pcol_peer_set_local_endpoint(PcolPeer* peer, PcolEndpoint ep) {
    if (peer) peer->impl->set_local_endpoint(to_ep(ep));
}

// --- Non-blocking connection ---

PcolError pcol_peer_connect_start(PcolPeer* peer, PcolEndpoint remote) {
    if (!peer) return PCOL_ERR_INVALID;
    return peer->impl->connect_start(to_ep(remote)) ? PCOL_OK : PCOL_ERR_INVALID;
}

PcolError pcol_peer_connect_poll(PcolPeer* peer) {
    if (!peer) return PCOL_ERR_INVALID;
    return peer->impl->connect_poll() ? PCOL_OK : PCOL_ERR_TIMEOUT;
}

PcolError pcol_peer_accept_start(PcolPeer* peer) {
    if (!peer) return PCOL_ERR_INVALID;
    return peer->impl->accept_start() ? PCOL_OK : PCOL_ERR_INVALID;
}

PcolError pcol_peer_accept_poll(PcolPeer* peer) {
    if (!peer) return PCOL_ERR_INVALID;
    return peer->impl->accept_poll() ? PCOL_OK : PCOL_ERR_TIMEOUT;
}

// --- State declaration ---

PcolError pcol_declare(PcolPeer* peer, const char* path, PcolCrdtType crdt_type,
                        PcolReliability reliability) {
    if (!peer || !path) return PCOL_ERR_INVALID;
    StatePath sp(path);
    return peer->impl->declare(sp, to_crdt(crdt_type), to_rel(reliability))
               ? PCOL_OK : PCOL_ERR_INVALID;
}

// --- State mutation ---

PcolError pcol_set_lww(PcolPeer* peer, const char* path,
                        const uint8_t* data, size_t len) {
    if (!peer || !path) return PCOL_ERR_INVALID;
    StatePath sp(path);
    return peer->impl->set_lww(sp, data, len) ? PCOL_OK : PCOL_ERR_NOT_FOUND;
}

PcolError pcol_increment_counter(PcolPeer* peer, const char* path, uint64_t amount) {
    if (!peer || !path) return PCOL_ERR_INVALID;
    StatePath sp(path);
    return peer->impl->increment_counter(sp, amount) ? PCOL_OK : PCOL_ERR_NOT_FOUND;
}

// --- State reading ---

PcolError pcol_get_lww(const PcolPeer* peer, const char* path,
                        uint8_t* buf, size_t buf_len, size_t* out_len) {
    if (!peer || !path) return PCOL_ERR_INVALID;
    StatePath sp(path);
    auto* region = peer->impl->state().get(sp);
    if (!region || region->crdt_type != CrdtType::LWW_REGISTER) return PCOL_ERR_NOT_FOUND;

    auto* lww = static_cast<LwwRegister*>(region->crdt.get());
    auto& val = lww->value();
    if (out_len) *out_len = val.size();
    if (buf && buf_len > 0) {
        size_t copy = val.size() < buf_len ? val.size() : buf_len;
        std::memcpy(buf, val.data(), copy);
    }
    return PCOL_OK;
}

PcolError pcol_get_counter(const PcolPeer* peer, const char* path, uint64_t* out_value) {
    if (!peer || !path || !out_value) return PCOL_ERR_INVALID;
    StatePath sp(path);
    auto* region = peer->impl->state().get(sp);
    if (!region || region->crdt_type != CrdtType::G_COUNTER) return PCOL_ERR_NOT_FOUND;

    auto* gc = static_cast<GCounter*>(region->crdt.get());
    *out_value = gc->value();
    return PCOL_OK;
}

// --- Network I/O ---

int pcol_flush(PcolPeer* peer) {
    if (!peer) return PCOL_ERR_INVALID;
    return peer->impl->flush();
}

int pcol_poll(PcolPeer* peer, int timeout_ms) {
    if (!peer) return PCOL_ERR_INVALID;
    return peer->impl->poll(timeout_ms);
}

// --- Callbacks ---

void pcol_on_update(PcolPeer* peer, PcolUpdateCallback cb, void* user_data) {
    if (!peer) return;
    peer->update_cb = cb;
    peer->update_ud = user_data;
    if (cb) {
        peer->impl->state().set_update_callback(
            [peer](const StatePath& path, const uint8_t* data, size_t len) {
                if (peer->update_cb) {
                    peer->update_cb(path.str().c_str(), data, len, peer->update_ud);
                }
            });
    } else {
        peer->impl->state().set_update_callback(nullptr);
    }
}

void pcol_on_signature_failure(PcolPeer* peer, PcolSigFailureCallback cb,
                                void* user_data) {
    if (!peer) return;
    peer->sig_cb = cb;
    peer->sig_ud = user_data;
    if (cb) {
        peer->impl->on_signature_failure(
            [peer](uint16_t author_id, uint32_t path_hash) {
                if (peer->sig_cb) {
                    peer->sig_cb(author_id, path_hash, peer->sig_ud);
                }
            });
    } else {
        peer->impl->on_signature_failure(nullptr);
    }
}

// --- Access control ---

void pcol_set_access_control(PcolPeer* peer, int enabled) {
    if (peer) peer->impl->set_access_control(enabled != 0);
}

// --- Multi-connection ---

PcolError pcol_peer_connect_to(PcolPeer* peer, uint16_t remote_node_id,
                                 PcolEndpoint remote) {
    if (!peer) return PCOL_ERR_INVALID;
    return peer->impl->connect_to(remote_node_id, to_ep(remote))
               ? PCOL_OK : PCOL_ERR_TIMEOUT;
}

PcolError pcol_peer_accept_node(PcolPeer* peer, uint16_t remote_node_id,
                                  PcolEndpoint expected_from, int timeout_ms) {
    if (!peer) return PCOL_ERR_INVALID;
    return peer->impl->accept_node(remote_node_id, to_ep(expected_from), timeout_ms)
               ? PCOL_OK : PCOL_ERR_TIMEOUT;
}

void pcol_peer_disconnect_node(PcolPeer* peer, uint16_t remote_node_id) {
    if (peer) peer->impl->disconnect_node(remote_node_id);
}

int pcol_peer_is_connected_to(const PcolPeer* peer, uint16_t remote_node_id) {
    return (peer && peer->impl->is_connected_to(remote_node_id)) ? 1 : 0;
}

// --- Resolution tiers ---

static ResolutionTier to_resolution(PcolResolutionTier t) {
    switch (t) {
    case PCOL_RESOLUTION_FULL:     return ResolutionTier::FULL;
    case PCOL_RESOLUTION_NORMAL:   return ResolutionTier::NORMAL;
    case PCOL_RESOLUTION_COARSE:   return ResolutionTier::COARSE;
    case PCOL_RESOLUTION_METADATA: return ResolutionTier::METADATA;
    default:                       return ResolutionTier::FULL;
    }
}

void pcol_peer_set_resolution(PcolPeer* peer, uint16_t remote_node_id,
                                PcolResolutionTier tier) {
    if (peer) peer->impl->set_connection_resolution(remote_node_id, to_resolution(tier));
}

// --- Routing ---

void pcol_peer_announce_route(PcolPeer* peer, const char* path) {
    if (!peer || !path) return;
    StatePath sp(path);
    peer->impl->router().announce_path(sp.hash());
}

void pcol_peer_learn_route(PcolPeer* peer, uint32_t path_hash, uint16_t via_node) {
    if (peer) peer->impl->router().learn_route(path_hash, via_node);
}

int pcol_peer_has_route(const PcolPeer* peer, uint32_t path_hash) {
    return (peer && peer->impl->router().has_route(path_hash)) ? 1 : 0;
}

// --- Subscriptions with resolution ---

int32_t pcol_subscribe_with_resolution(PcolPeer* peer, const char* pattern,
                                         PcolResolutionTier tier,
                                         int32_t initial_credits,
                                         uint32_t freshness_us) {
    if (!peer || !pattern) return PCOL_ERR_INVALID;
    StatePath sp(pattern);
    uint32_t sub_id = peer->impl->subscribe(sp, to_resolution(tier),
                                              initial_credits, freshness_us);
    return static_cast<int32_t>(sub_id);
}

// --- API versioning ---

uint32_t pcol_api_version(void) {
    // Version 0.1.0 = (0 << 16) | (1 << 8) | 0
    return 0x000100u;
}
