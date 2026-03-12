#include "protocoll/connection/handshake.h"
#include "protocoll/util/clock.h"

namespace protocoll {
namespace handshake {

std::vector<uint8_t> build_connect_packet(Connection& conn) {
    PacketEncoder enc;
    enc.set_packet_type(PacketType::HANDSHAKE);
    enc.set_connection_id(0); // No assigned ID yet
    enc.set_packet_number(conn.next_send_packet_number());
    enc.set_timestamp(conn.elapsed_us());

    ConnectFrame cf{};
    cf.magic = CONNECT_MAGIC;
    cf.version = PROTOCOL_VERSION;
    cf.max_frame_size = conn.config().max_frame_size;
    enc.add_typed_frame(FrameType::CONNECT, cf);

    return enc.finalize();
}

std::vector<uint8_t> build_accept_packet(Connection& conn) {
    PacketEncoder enc;
    enc.set_packet_type(PacketType::HANDSHAKE);
    enc.set_connection_id(conn.remote_conn_id());
    enc.set_packet_number(conn.next_send_packet_number());
    enc.set_timestamp(conn.elapsed_us());

    AcceptFrame af{};
    af.assigned_conn_id = conn.local_conn_id();
    af.server_timestamp_us = conn.elapsed_us();
    enc.add_typed_frame(FrameType::ACCEPT, af);

    return enc.finalize();
}

std::vector<uint8_t> build_close_packet(Connection& conn, CloseReason reason) {
    PacketEncoder enc;
    enc.set_packet_type(PacketType::HANDSHAKE);
    enc.set_connection_id(conn.remote_conn_id());
    enc.set_packet_number(conn.next_send_packet_number());
    enc.set_timestamp(conn.elapsed_us());

    CloseFrame cf{};
    cf.reason = reason;
    enc.add_typed_frame(FrameType::CLOSE, cf);

    return enc.finalize();
}

std::vector<uint8_t> build_ping_packet(Connection& conn, uint32_t ping_id) {
    PacketEncoder enc;
    enc.set_packet_type(PacketType::CONTROL);
    enc.set_connection_id(conn.remote_conn_id());
    enc.set_packet_number(conn.next_send_packet_number());
    enc.set_timestamp(conn.elapsed_us());

    PingFrame pf{};
    pf.ping_id = ping_id;
    pf.timestamp_us = conn.elapsed_us();
    enc.add_typed_frame(FrameType::PING, pf);

    return enc.finalize();
}

std::vector<uint8_t> build_pong_packet(Connection& conn, uint32_t ping_id, uint32_t echo_timestamp) {
    PacketEncoder enc;
    enc.set_packet_type(PacketType::CONTROL);
    enc.set_connection_id(conn.remote_conn_id());
    enc.set_packet_number(conn.next_send_packet_number());
    enc.set_timestamp(conn.elapsed_us());

    PongFrame pf{};
    pf.ping_id = ping_id;
    pf.timestamp_us = echo_timestamp;
    enc.add_typed_frame(FrameType::PONG, pf);

    return enc.finalize();
}

HandshakeEvent process_packet(Connection& conn, const uint8_t* data, size_t len) {
    HandshakeEvent event{};
    event.result = HandshakeResult::NOT_HANDSHAKE;

    PacketDecoder dec;
    if (!dec.parse(data, len)) {
        event.result = HandshakeResult::INVALID;
        return event;
    }

    if (!dec.verify_checksum()) {
        event.result = HandshakeResult::INVALID;
        return event;
    }

    conn.update_recv_packet_number(dec.header().packet_number);

    Frame frame;
    while (dec.next_frame(frame)) {
        switch (frame.header.type) {
            case FrameType::CONNECT: {
                ConnectFrame cf{};
                if (!cf.decode(frame.payload, frame.header.length)) {
                    event.result = HandshakeResult::INVALID;
                    return event;
                }
                if (cf.magic != CONNECT_MAGIC) {
                    event.result = HandshakeResult::INVALID;
                    return event;
                }
                conn.on_connect(cf.version, cf.max_frame_size);
                event.result = HandshakeResult::CONNECT_RECEIVED;
                event.connect = cf;
                return event;
            }
            case FrameType::ACCEPT: {
                AcceptFrame af{};
                if (!af.decode(frame.payload, frame.header.length)) {
                    event.result = HandshakeResult::INVALID;
                    return event;
                }
                conn.on_accept(af.assigned_conn_id, af.server_timestamp_us);
                event.result = HandshakeResult::ACCEPT_RECEIVED;
                event.accept = af;
                return event;
            }
            case FrameType::CLOSE: {
                CloseFrame cf{};
                if (!cf.decode(frame.payload, frame.header.length)) {
                    event.result = HandshakeResult::INVALID;
                    return event;
                }
                conn.on_close(cf.reason);
                event.result = HandshakeResult::CLOSE_RECEIVED;
                event.close = cf;
                return event;
            }
            case FrameType::PING: {
                PingFrame pf{};
                if (!pf.decode(frame.payload, frame.header.length)) {
                    event.result = HandshakeResult::INVALID;
                    return event;
                }
                event.result = HandshakeResult::PING_RECEIVED;
                event.ping = pf;
                return event;
            }
            case FrameType::PONG: {
                PongFrame pf{};
                if (!pf.decode(frame.payload, frame.header.length)) {
                    event.result = HandshakeResult::INVALID;
                    return event;
                }
                event.result = HandshakeResult::PONG_RECEIVED;
                event.ping = pf; // PongFrame == PingFrame
                return event;
            }
            default:
                break;
        }
    }

    return event;
}

} // namespace handshake
} // namespace protocoll
