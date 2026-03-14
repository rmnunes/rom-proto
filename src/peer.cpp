#include "protocoll/peer.h"
#include "protocoll/util/clock.h"
#include "protocoll/util/platform.h"
#include <cstring>

namespace protocoll {

Peer::Peer(uint16_t node_id, Transport& transport, const KeyPair& keys)
    : node_id_(node_id)
    , transport_(transport)
    , state_(node_id)
    , router_(node_id)
    , keys_(keys)
    , cap_store_(keys, node_id) {}

// --- Identity ---

void Peer::register_peer_key(uint16_t node_id, const PublicKey& pk) {
    peer_keys_[node_id] = pk;
    cap_store_.register_peer_key(node_id, pk);
}

// --- Connection (backward-compatible single-peer API) ---

bool Peer::connect(const Endpoint& remote) {
    auto* mc = conn_mgr_.add(0);
    if (!mc) {
        mc = conn_mgr_.get(0);
        if (!mc) return false;
    }

    uint16_t cid = conn_mgr_.next_conn_id();
    if (!mc->conn.initiate(cid, remote)) return false;

    auto pkt = handshake::build_connect_packet(mc->conn);
    int sent = transport_.send_to(pkt.data(), pkt.size(), remote);
    if (sent <= 0) return false;

    Endpoint from;
    int n = transport_.recv_from(recv_buf_, sizeof(recv_buf_), from,
                                  static_cast<int>(mc->conn.config().connect_timeout_ms));
    if (n <= 0) return false;

    auto event = handshake::process_packet(mc->conn, recv_buf_, static_cast<size_t>(n));
    return event.result == handshake::HandshakeResult::ACCEPT_RECEIVED;
}

bool Peer::accept_from(const Endpoint& /*expected_from*/, int timeout_ms) {
    auto* mc = conn_mgr_.add(0);
    if (!mc) {
        mc = conn_mgr_.get(0);
        if (!mc) return false;
    }

    Endpoint from;
    int n = transport_.recv_from(recv_buf_, sizeof(recv_buf_), from, timeout_ms);
    if (n <= 0) return false;

    auto event = handshake::process_packet(mc->conn, recv_buf_, static_cast<size_t>(n));
    if (event.result != handshake::HandshakeResult::CONNECT_RECEIVED) return false;

    uint16_t cid = conn_mgr_.next_conn_id();
    mc->conn.accept(cid, 0, from);

    auto pkt = handshake::build_accept_packet(mc->conn);
    transport_.send_to(pkt.data(), pkt.size(), from);

    return mc->conn.state() == ConnectionState::CONNECTED;
}

bool Peer::is_connected() const {
    return conn_mgr_.has_connections();
}

void Peer::disconnect(CloseReason reason) {
    auto* mc = conn_mgr_.primary();
    if (mc && mc->conn.state() == ConnectionState::CONNECTED) {
        mc->conn.close(reason);
        auto pkt = handshake::build_close_packet(mc->conn, reason);
        transport_.send_to(pkt.data(), pkt.size(), mc->conn.remote_endpoint());
    }
}

// --- Non-blocking handshake (for WASM/browser) ---

bool Peer::connect_start(const Endpoint& remote) {
    auto* mc = conn_mgr_.add(0);
    if (!mc) {
        mc = conn_mgr_.get(0);
        if (!mc) return false;
    }

    uint16_t cid = conn_mgr_.next_conn_id();
    if (!mc->conn.initiate(cid, remote)) return false;

    auto pkt = handshake::build_connect_packet(mc->conn);
    int sent = transport_.send_to(pkt.data(), pkt.size(), remote);
    if (sent <= 0) return false;

    hs_state_ = HandshakeState::CONNECTING;
    hs_remote_ = remote;
    return true;
}

bool Peer::connect_poll() {
    if (hs_state_ != HandshakeState::CONNECTING) return false;

    auto* mc = conn_mgr_.get(0);
    if (!mc) return false;

    // Already connected (previous poll succeeded)?
    if (mc->conn.state() == ConnectionState::CONNECTED) {
        hs_state_ = HandshakeState::NONE;
        return true;
    }

    Endpoint from;
    int n = transport_.recv_from(recv_buf_, sizeof(recv_buf_), from, 0);
    if (n <= 0) return false;

    auto event = handshake::process_packet(mc->conn, recv_buf_, static_cast<size_t>(n));
    if (event.result == handshake::HandshakeResult::ACCEPT_RECEIVED) {
        hs_state_ = HandshakeState::NONE;
        return true;
    }
    return false;
}

bool Peer::accept_start() {
    auto* mc = conn_mgr_.add(0);
    if (!mc) {
        mc = conn_mgr_.get(0);
        if (!mc) return false;
    }

    hs_state_ = HandshakeState::ACCEPTING;
    return true;
}

bool Peer::accept_poll() {
    if (hs_state_ != HandshakeState::ACCEPTING) return false;

    auto* mc = conn_mgr_.get(0);
    if (!mc) return false;

    // Already connected?
    if (mc->conn.state() == ConnectionState::CONNECTED) {
        hs_state_ = HandshakeState::NONE;
        return true;
    }

    Endpoint from;
    int n = transport_.recv_from(recv_buf_, sizeof(recv_buf_), from, 0);
    if (n <= 0) return false;

    auto event = handshake::process_packet(mc->conn, recv_buf_, static_cast<size_t>(n));
    if (event.result != handshake::HandshakeResult::CONNECT_RECEIVED) return false;

    uint16_t cid = conn_mgr_.next_conn_id();
    mc->conn.accept(cid, 0, from);

    auto pkt = handshake::build_accept_packet(mc->conn);
    transport_.send_to(pkt.data(), pkt.size(), from);

    if (mc->conn.state() == ConnectionState::CONNECTED) {
        hs_state_ = HandshakeState::NONE;
        return true;
    }
    return false;
}

// --- Multi-connection API ---

bool Peer::connect_to(uint16_t remote_node_id, const Endpoint& remote) {
    auto* mc = conn_mgr_.add(remote_node_id);
    if (!mc) return false;

    uint16_t cid = conn_mgr_.next_conn_id();
    if (!mc->conn.initiate(cid, remote)) {
        conn_mgr_.remove(remote_node_id);
        return false;
    }

    auto pkt = handshake::build_connect_packet(mc->conn);
    int sent = transport_.send_to(pkt.data(), pkt.size(), remote);
    if (sent <= 0) {
        conn_mgr_.remove(remote_node_id);
        return false;
    }

    Endpoint from;
    int n = transport_.recv_from(recv_buf_, sizeof(recv_buf_), from,
                                  static_cast<int>(mc->conn.config().connect_timeout_ms));
    if (n <= 0) {
        conn_mgr_.remove(remote_node_id);
        return false;
    }

    auto event = handshake::process_packet(mc->conn, recv_buf_, static_cast<size_t>(n));
    if (event.result != handshake::HandshakeResult::ACCEPT_RECEIVED) {
        conn_mgr_.remove(remote_node_id);
        return false;
    }

    return true;
}

bool Peer::accept_node(uint16_t remote_node_id, const Endpoint& /*expected_from*/,
                        int timeout_ms) {
    auto* mc = conn_mgr_.add(remote_node_id);
    if (!mc) return false;

    Endpoint from;
    int n = transport_.recv_from(recv_buf_, sizeof(recv_buf_), from, timeout_ms);
    if (n <= 0) {
        conn_mgr_.remove(remote_node_id);
        return false;
    }

    auto event = handshake::process_packet(mc->conn, recv_buf_, static_cast<size_t>(n));
    if (event.result != handshake::HandshakeResult::CONNECT_RECEIVED) {
        conn_mgr_.remove(remote_node_id);
        return false;
    }

    uint16_t cid = conn_mgr_.next_conn_id();
    mc->conn.accept(cid, 0, from);

    auto pkt = handshake::build_accept_packet(mc->conn);
    transport_.send_to(pkt.data(), pkt.size(), from);

    if (mc->conn.state() != ConnectionState::CONNECTED) {
        conn_mgr_.remove(remote_node_id);
        return false;
    }
    return true;
}

void Peer::disconnect_node(uint16_t remote_node_id, CloseReason reason) {
    auto* mc = conn_mgr_.get(remote_node_id);
    if (mc && mc->conn.state() == ConnectionState::CONNECTED) {
        mc->conn.close(reason);
        auto pkt = handshake::build_close_packet(mc->conn, reason);
        transport_.send_to(pkt.data(), pkt.size(), mc->conn.remote_endpoint());
    }
    conn_mgr_.remove(remote_node_id);
    delta_filters_.erase(remote_node_id);
    router_.remove_node(remote_node_id);
}

bool Peer::is_connected_to(uint16_t remote_node_id) const {
    auto* mc = conn_mgr_.get(remote_node_id);
    return mc && mc->conn.state() == ConnectionState::CONNECTED;
}

std::vector<uint16_t> Peer::connected_nodes() const {
    return conn_mgr_.connected_nodes();
}

// --- Resolution ---

void Peer::set_connection_resolution(uint16_t remote_node_id, ResolutionTier tier,
                                      ResolutionConfig config) {
    delta_filters_[remote_node_id] = DeltaFilter(tier, config);
}

// --- Subscriptions ---

uint32_t Peer::subscribe(const StatePath& pattern, ResolutionTier tier,
                          int64_t initial_credits, uint32_t freshness_us) {
    // Use node_id as connection ID for the subscription
    uint32_t sub_id = sub_mgr_.subscribe(node_id_, pattern, initial_credits, freshness_us);
    auto* sub = sub_mgr_.get(sub_id);
    if (sub) sub->tier = tier;
    return sub_id;
}

// --- Backward-compatible connection accessor ---

Connection& Peer::connection() {
    auto* mc = conn_mgr_.primary();
    return mc->conn;
}

const Connection& Peer::connection() const {
    auto* mc = conn_mgr_.primary();
    return mc->conn;
}

// --- State declaration ---

bool Peer::declare(const StatePath& path, CrdtType type, Reliability rel) {
    bool ok = state_.declare(path, type, rel);
    if (ok) {
        router_.announce_path(path.hash());
    }
    return ok;
}

// --- State mutation ---

bool Peer::set_lww(const StatePath& path, const uint8_t* data, size_t len) {
    auto* region = state_.get(path);
    if (!region || region->crdt_type != CrdtType::LWW_REGISTER) return false;

    auto* lww = static_cast<LwwRegister*>(region->crdt.get());
    auto* mc = conn_mgr_.primary();
    uint32_t ts = mc ? mc->conn.elapsed_us() : 0;
    lww->set(data, len, ts);
    region->version.increment(node_id_);
    return true;
}

bool Peer::increment_counter(const StatePath& path, uint64_t amount) {
    auto* region = state_.get(path);
    if (!region || region->crdt_type != CrdtType::G_COUNTER) return false;

    auto* gc = static_cast<GCounter*>(region->crdt.get());
    gc->increment(amount);
    region->version.increment(node_id_);
    return true;
}

// --- Security helpers ---

std::vector<uint8_t> Peer::build_signed_delta(const StateRegistry::PendingDelta& d) {
    size_t content_size = StateDeltaFrame::BASE_WIRE_SIZE + d.data.size();
    std::vector<uint8_t> payload(content_size + StateDeltaFrame::SIGNATURE_SIZE);

    StateDeltaFrame sdf{};
    sdf.path_hash = d.path_hash;
    sdf.crdt_type = d.crdt_type;
    sdf.reliability = d.reliability;
    sdf.author_node_id = node_id_;
    sdf.encode(payload.data(), payload.size());

    if (!d.data.empty()) {
        std::memcpy(payload.data() + StateDeltaFrame::BASE_WIRE_SIZE,
                     d.data.data(), d.data.size());
    }

    auto sig = sign(keys_.secret_key, payload.data(), content_size);
    std::memcpy(payload.data() + content_size, sig.data(), StateDeltaFrame::SIGNATURE_SIZE);

    return payload;
}

bool Peer::verify_frame_signature(const uint8_t* payload, size_t len,
                                     uint16_t author_node_id, size_t base_size) {
    if (len < base_size + StateDeltaFrame::SIGNATURE_SIZE) return false;

    auto it = peer_keys_.find(author_node_id);
    if (it == peer_keys_.end()) return false;

    size_t content_len = len - StateDeltaFrame::SIGNATURE_SIZE;
    const uint8_t* sig_ptr = payload + content_len;

    Signature sig;
    std::memcpy(sig.data(), sig_ptr, StateDeltaFrame::SIGNATURE_SIZE);

    return verify(it->second, sig, payload, content_len);
}

// --- Network I/O ---

int Peer::flush() {
    if (!conn_mgr_.has_connections()) return -1;

    auto deltas = state_.collect_deltas();
    if (deltas.empty()) return 0;

    int total_sent = 0;

    conn_mgr_.for_each([&](ManagedConnection& mc) {
        if (mc.conn.state() != ConnectionState::CONNECTED) return;

        // Apply per-connection resolution filter
        auto filter_it = delta_filters_.find(mc.remote_node_id);
        if (filter_it != delta_filters_.end()) {
            std::vector<StateRegistry::PendingDelta> filtered;
            for (const auto& d : deltas) {
                if (filter_it->second.should_forward(d.path_hash,
                        d.data.data(), d.data.size())) {
                    filtered.push_back(d);
                }
            }
            if (!filtered.empty()) {
                total_sent += send_deltas_to(mc, filtered);
            }
        } else {
            total_sent += send_deltas_to(mc, deltas);
        }
    });

    return total_sent;
}

int Peer::send_deltas_to(ManagedConnection& mc,
                          const std::vector<StateRegistry::PendingDelta>& deltas) {
    int frames_sent = 0;
    PacketEncoder enc;
    enc.set_packet_type(PacketType::DATA);
    enc.set_connection_id(mc.conn.remote_conn_id());

    for (const auto& d : deltas) {
        auto frame_payload = build_signed_delta(d);

        if (!enc.add_frame(FrameType::STATE_DELTA, frame_payload.data(),
                           static_cast<uint16_t>(frame_payload.size()))) {
            uint32_t pkt_num = mc.conn.next_send_packet_number();
            enc.set_packet_number(pkt_num);
            enc.set_timestamp(mc.conn.elapsed_us());
            auto pkt = enc.finalize();
            transport_.send_to(pkt.data(), pkt.size(), mc.conn.remote_endpoint());
            mc.send_tracker.on_packet_sent(pkt_num, mc.conn.elapsed_us(),
                                             static_cast<uint16_t>(pkt.size()));

            enc.set_packet_type(PacketType::DATA);
            enc.set_connection_id(mc.conn.remote_conn_id());
            enc.add_frame(FrameType::STATE_DELTA, frame_payload.data(),
                          static_cast<uint16_t>(frame_payload.size()));
        }
        frames_sent++;
    }

    if (enc.payload_size() > 0) {
        uint32_t pkt_num = mc.conn.next_send_packet_number();
        enc.set_packet_number(pkt_num);
        enc.set_timestamp(mc.conn.elapsed_us());
        auto pkt = enc.finalize();
        transport_.send_to(pkt.data(), pkt.size(), mc.conn.remote_endpoint());
        mc.send_tracker.on_packet_sent(pkt_num, mc.conn.elapsed_us(),
                                         static_cast<uint16_t>(pkt.size()));
    }

    return frames_sent;
}

int Peer::poll(int timeout_ms) {
    if (!conn_mgr_.has_connections()) return -1;

    Endpoint from;
    int n = transport_.recv_from(recv_buf_, sizeof(recv_buf_), from, timeout_ms);
    if (n <= 0) return 0;

    PacketDecoder dec;
    if (!dec.parse(recv_buf_, static_cast<size_t>(n))) return 0;
    if (!dec.verify_checksum()) return 0;

    // Find the connection this packet belongs to by matching endpoint
    ManagedConnection* mc = nullptr;
    conn_mgr_.for_each([&](ManagedConnection& m) {
        if (m.conn.remote_endpoint() == from &&
            m.conn.state() == ConnectionState::CONNECTED) {
            mc = &m;
        }
    });

    // Auto-accept: CONNECT from an unknown endpoint → create new connection
    if (!mc && dec.header().packet_type == PacketType::HANDSHAKE) {
        uint16_t auto_id = next_auto_node_id_++;
        auto* new_mc = conn_mgr_.add(auto_id);
        if (new_mc) {
            auto event = handshake::process_packet(new_mc->conn, recv_buf_, static_cast<size_t>(n));
            if (event.result == handshake::HandshakeResult::CONNECT_RECEIVED) {
                uint16_t cid = conn_mgr_.next_conn_id();
                new_mc->conn.accept(cid, 0, from);
                auto pkt = handshake::build_accept_packet(new_mc->conn);
                transport_.send_to(pkt.data(), pkt.size(), from);
            } else if (event.result == handshake::HandshakeResult::PING_RECEIVED) {
                // Not a real new connection, just a ping — clean up
                conn_mgr_.remove(auto_id);
                next_auto_node_id_--;
            } else {
                conn_mgr_.remove(auto_id);
                next_auto_node_id_--;
            }
        }
        return 0;
    }

    // Fallback: use primary if no match by endpoint
    if (!mc) mc = conn_mgr_.primary();
    if (!mc) return 0;

    mc->conn.update_recv_packet_number(dec.header().packet_number);
    mc->recv_tracker.on_packet_received(dec.header().packet_number);

    if (dec.header().packet_type == PacketType::HANDSHAKE) {
        auto event = handshake::process_packet(mc->conn, recv_buf_, static_cast<size_t>(n));
        if (event.result == handshake::HandshakeResult::PING_RECEIVED) {
            auto pong = handshake::build_pong_packet(mc->conn, event.ping.ping_id,
                                                      event.ping.timestamp_us);
            transport_.send_to(pong.data(), pong.size(), from);
        }
        return 0;
    }

    if (dec.header().packet_type == PacketType::DATA) {
        return process_data_frames(*mc, dec);
    }

    return 0;
}

int Peer::process_data_frames(ManagedConnection& mc, PacketDecoder& dec) {
    int changes = 0;
    Frame frame;
    while (dec.next_frame(frame)) {
        if (frame.header.type == FrameType::STATE_DELTA) {
            const uint8_t* payload = frame.payload;
            size_t payload_len = frame.header.length;

            if (payload_len < StateDeltaFrame::MIN_WIRE_SIZE) {
                if (on_sig_failure_ && payload_len >= 4) {
                    on_sig_failure_(0, read_u32(payload));
                }
                continue;
            }

            StateDeltaFrame sdf{};
            sdf.decode(payload, payload_len);

            if (!verify_frame_signature(payload, payload_len,
                                         sdf.author_node_id, StateDeltaFrame::BASE_WIRE_SIZE)) {
                if (on_sig_failure_) {
                    on_sig_failure_(sdf.author_node_id, sdf.path_hash);
                }
                continue;
            }

            size_t content_len = payload_len - StateDeltaFrame::SIGNATURE_SIZE;
            const uint8_t* delta_data = payload + StateDeltaFrame::BASE_WIRE_SIZE;
            size_t delta_len = content_len - StateDeltaFrame::BASE_WIRE_SIZE;

            if (access_control_) {
                auto* region = state_.get(sdf.path_hash);
                if (region) {
                    auto* cap = cap_store_.find_capability(
                        region->path, CAP_WRITE, mc.conn.elapsed_us());
                    if (!cap) continue;
                }
            }

            if (state_.apply_delta(sdf.path_hash, delta_data, delta_len)) {
                changes++;

                // Router feedback: delivery succeeded via this node
                router_.on_delivery_success(mc.remote_node_id, sdf.path_hash,
                                             mc.conn.smoothed_rtt_us());
            }
        } else if (frame.header.type == FrameType::STATE_SNAPSHOT) {
            const uint8_t* payload = frame.payload;
            size_t payload_len = frame.header.length;

            if (payload_len < StateSnapshotFrame::MIN_WIRE_SIZE) continue;

            StateSnapshotFrame ssf{};
            ssf.decode(payload, payload_len);

            if (!verify_frame_signature(payload, payload_len,
                                         ssf.author_node_id, StateSnapshotFrame::BASE_WIRE_SIZE)) {
                continue;
            }

            size_t content_len = payload_len - StateSnapshotFrame::SIGNATURE_SIZE;
            const uint8_t* snap_data = payload + StateSnapshotFrame::BASE_WIRE_SIZE;
            size_t snap_len = content_len - StateSnapshotFrame::BASE_WIRE_SIZE;

            if (state_.apply_snapshot(ssf.path_hash, snap_data, snap_len)) {
                changes++;
            }
        } else if (frame.header.type == FrameType::CAPABILITY_GRANT) {
            CapabilityToken token;
            if (CapabilityToken::decode(frame.payload, frame.header.length, token)) {
                auto* issuer_pk = cap_store_.peer_key(token.issuer_id);
                if (issuer_pk && cap_store_.verify_token(token, *issuer_pk)) {
                    cap_store_.store(std::move(token));
                }
            }
        } else if (frame.header.type == FrameType::CAPABILITY_REVOKE) {
            CapabilityRevokeFrame crf{};
            if (crf.decode(frame.payload, frame.header.length)) {
                cap_store_.revoke(crf.token_id);
            }
        } else if (frame.header.type == FrameType::ROUTE_ANNOUNCE) {
            if (frame.header.length >= 6) {
                uint32_t path_hash = read_u32(frame.payload);
                uint16_t announcing_node = static_cast<uint16_t>(
                    frame.payload[4] | (frame.payload[5] << 8));
                router_.learn_route(path_hash, announcing_node);
            }
        }
    }
    return changes;
}

void Peer::forward_deltas(uint16_t source_node_id,
                           const std::vector<StateRegistry::PendingDelta>& deltas) {
    for (const auto& d : deltas) {
        auto next_hops = router_.select_next_hops(d.path_hash);
        for (uint16_t hop : next_hops) {
            if (hop == source_node_id) continue;
            auto* mc = conn_mgr_.get(hop);
            if (mc && mc->conn.state() == ConnectionState::CONNECTED) {
                std::vector<StateRegistry::PendingDelta> single = {d};
                send_deltas_to(*mc, single);
            }
        }
    }
}

void Peer::send_ack(ManagedConnection& mc) {
    auto ack_data = mc.recv_tracker.build_ack(0);
    uint8_t buf[AckFrame::BASE_WIRE_SIZE];
    ack_data.frame.encode(buf, sizeof(buf));

    PacketEncoder enc;
    enc.set_packet_type(PacketType::CONTROL);
    enc.set_connection_id(mc.conn.remote_conn_id());
    enc.set_packet_number(mc.conn.next_send_packet_number());
    enc.set_timestamp(mc.conn.elapsed_us());
    enc.add_frame(FrameType::ACK, buf, AckFrame::BASE_WIRE_SIZE);

    auto pkt = enc.finalize();
    transport_.send_to(pkt.data(), pkt.size(), mc.conn.remote_endpoint());
}

} // namespace protocoll
