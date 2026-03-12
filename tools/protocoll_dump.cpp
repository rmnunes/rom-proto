// protocoll-dump: decode and display protocoll packets from stdin or file.
//
// Reads raw packet bytes (prefixed with 2-byte big-endian length) and
// decodes them using the protocoll wire format. Useful for debugging
// packet captures and verifying wire encoding.
//
// Usage:
//   protocoll-dump < capture.bin
//   protocoll-dump capture.bin
//   protocoll-dump --hex "1800010000000001..."

#include "protocoll/wire/codec.h"
#include "protocoll/wire/packet.h"
#include "protocoll/wire/frame.h"
#include "protocoll/wire/frame_types.h"
#include "protocoll/util/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>

using namespace protocoll;

// --- String helpers ---

static const char* packet_type_str(PacketType t) {
    switch (t) {
        case PacketType::DATA:      return "DATA";
        case PacketType::CONTROL:   return "CONTROL";
        case PacketType::HANDSHAKE: return "HANDSHAKE";
        default:                    return "UNKNOWN";
    }
}

static const char* frame_type_str(FrameType t) {
    switch (t) {
        case FrameType::STATE_DECLARE:     return "STATE_DECLARE";
        case FrameType::STATE_SNAPSHOT:    return "STATE_SNAPSHOT";
        case FrameType::STATE_DELTA:       return "STATE_DELTA";
        case FrameType::STATE_SUBSCRIBE:   return "STATE_SUBSCRIBE";
        case FrameType::STATE_UNSUBSCRIBE: return "STATE_UNSUBSCRIBE";
        case FrameType::CREDIT:            return "CREDIT";
        case FrameType::ACK:               return "ACK";
        case FrameType::NACK:              return "NACK";
        case FrameType::CAPABILITY_GRANT:  return "CAPABILITY_GRANT";
        case FrameType::CAPABILITY_REVOKE: return "CAPABILITY_REVOKE";
        case FrameType::CLOCK_SYNC:        return "CLOCK_SYNC";
        case FrameType::PING:              return "PING";
        case FrameType::PONG:              return "PONG";
        case FrameType::CONNECT:           return "CONNECT";
        case FrameType::ACCEPT:            return "ACCEPT";
        case FrameType::CLOSE:             return "CLOSE";
        default:                           return "UNKNOWN";
    }
}

static const char* crdt_type_str(CrdtType t) {
    switch (t) {
        case CrdtType::NONE:         return "NONE";
        case CrdtType::LWW_REGISTER: return "LWW_REGISTER";
        case CrdtType::G_COUNTER:    return "G_COUNTER";
        case CrdtType::PN_COUNTER:   return "PN_COUNTER";
        case CrdtType::OR_SET:       return "OR_SET";
        case CrdtType::MV_REGISTER:  return "MV_REGISTER";
        case CrdtType::RGA:          return "RGA";
        default:                     return "UNKNOWN";
    }
}

static const char* reliability_str(Reliability r) {
    switch (r) {
        case Reliability::UNRELIABLE:  return "UNRELIABLE";
        case Reliability::BEST_EFFORT: return "BEST_EFFORT";
        case Reliability::RELIABLE:    return "RELIABLE";
        default:                       return "UNKNOWN";
    }
}

static const char* close_reason_str(CloseReason r) {
    switch (r) {
        case CloseReason::NORMAL:         return "NORMAL";
        case CloseReason::TIMEOUT:        return "TIMEOUT";
        case CloseReason::PROTOCOL_ERROR: return "PROTOCOL_ERROR";
        case CloseReason::AUTH_FAILURE:   return "AUTH_FAILURE";
        case CloseReason::GOING_AWAY:     return "GOING_AWAY";
        default:                          return "UNKNOWN";
    }
}

static std::string flags_str(uint8_t flags) {
    std::string s;
    if (flags & FLAG_CHECKSUM)   s += "CHECKSUM ";
    if (flags & FLAG_COMPRESSED) s += "COMPRESSED ";
    if (flags & FLAG_ENCRYPTED)  s += "ENCRYPTED ";
    if (flags & FLAG_RESERVED)   s += "RESERVED ";
    if (s.empty()) s = "NONE";
    else s.pop_back(); // remove trailing space
    return s;
}

static std::string hex_bytes(const uint8_t* data, size_t len, size_t max_show = 32) {
    std::ostringstream oss;
    size_t show = (len > max_show) ? max_show : len;
    for (size_t i = 0; i < show; i++) {
        if (i > 0) oss << ' ';
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<unsigned>(data[i]);
    }
    if (len > max_show) {
        oss << " ... (" << std::dec << len << " bytes total)";
    }
    return oss.str();
}

// --- Frame detail printers ---

static void print_frame_detail(const Frame& frame) {
    const uint8_t* p = frame.payload;
    uint16_t len = frame.header.length;

    switch (frame.header.type) {
        case FrameType::CONNECT: {
            ConnectFrame cf;
            if (cf.decode(p, len)) {
                printf("      magic=0x%08X version=%u max_frame_size=%u\n",
                       cf.magic, cf.version, cf.max_frame_size);
            }
            break;
        }
        case FrameType::ACCEPT: {
            AcceptFrame af;
            if (af.decode(p, len)) {
                printf("      assigned_conn_id=%u server_timestamp=%u us\n",
                       af.assigned_conn_id, af.server_timestamp_us);
            }
            break;
        }
        case FrameType::CLOSE: {
            CloseFrame clf;
            if (clf.decode(p, len)) {
                printf("      reason=%s\n", close_reason_str(clf.reason));
            }
            break;
        }
        case FrameType::PING:
        case FrameType::PONG: {
            PingFrame pf;
            if (pf.decode(p, len)) {
                printf("      ping_id=%u timestamp=%u us\n",
                       pf.ping_id, pf.timestamp_us);
            }
            break;
        }
        case FrameType::ACK: {
            AckFrame ack;
            if (ack.decode(p, len)) {
                printf("      largest_acked=%u ack_delay=%u us sack_ranges=%u\n",
                       ack.largest_acked, ack.ack_delay_us, ack.sack_range_count);
            }
            break;
        }
        case FrameType::STATE_DECLARE: {
            StateDeclareFrame sd;
            if (sd.decode(p, len)) {
                printf("      path_hash=0x%08X crdt=%s reliability=%s\n",
                       sd.path_hash, crdt_type_str(sd.crdt_type),
                       reliability_str(sd.reliability));
                if (len > StateDeclareFrame::BASE_WIRE_SIZE) {
                    size_t str_len = len - StateDeclareFrame::BASE_WIRE_SIZE;
                    std::string path(reinterpret_cast<const char*>(p + StateDeclareFrame::BASE_WIRE_SIZE), str_len);
                    printf("      path=\"%s\"\n", path.c_str());
                }
            }
            break;
        }
        case FrameType::STATE_DELTA: {
            StateDeltaFrame sdf;
            if (len >= StateDeltaFrame::BASE_WIRE_SIZE && sdf.decode(p, len)) {
                size_t delta_len = 0;
                bool has_sig = (len >= StateDeltaFrame::MIN_WIRE_SIZE);
                if (has_sig) {
                    delta_len = len - StateDeltaFrame::BASE_WIRE_SIZE - StateDeltaFrame::SIGNATURE_SIZE;
                } else {
                    delta_len = len - StateDeltaFrame::BASE_WIRE_SIZE;
                }
                printf("      path_hash=0x%08X crdt=%s reliability=%s author=%u\n",
                       sdf.path_hash, crdt_type_str(sdf.crdt_type),
                       reliability_str(sdf.reliability), sdf.author_node_id);
                printf("      delta_len=%zu signed=%s\n", delta_len, has_sig ? "yes" : "no");
                if (delta_len > 0) {
                    printf("      delta: %s\n",
                           hex_bytes(p + StateDeltaFrame::BASE_WIRE_SIZE, delta_len).c_str());
                }
            }
            break;
        }
        case FrameType::STATE_SNAPSHOT: {
            StateSnapshotFrame ssf;
            if (len >= StateSnapshotFrame::BASE_WIRE_SIZE && ssf.decode(p, len)) {
                size_t snap_len = 0;
                bool has_sig = (len >= StateSnapshotFrame::MIN_WIRE_SIZE);
                if (has_sig) {
                    snap_len = len - StateSnapshotFrame::BASE_WIRE_SIZE - StateSnapshotFrame::SIGNATURE_SIZE;
                } else {
                    snap_len = len - StateSnapshotFrame::BASE_WIRE_SIZE;
                }
                printf("      path_hash=0x%08X crdt=%s author=%u\n",
                       ssf.path_hash, crdt_type_str(ssf.crdt_type), ssf.author_node_id);
                printf("      snapshot_len=%zu signed=%s\n", snap_len, has_sig ? "yes" : "no");
            }
            break;
        }
        case FrameType::CAPABILITY_REVOKE: {
            CapabilityRevokeFrame cr;
            if (cr.decode(p, len)) {
                printf("      token_id=%u issuer=%u\n", cr.token_id, cr.issuer_node_id);
            }
            break;
        }
        default:
            if (len > 0) {
                printf("      payload: %s\n", hex_bytes(p, len).c_str());
            }
            break;
    }
}

// --- Packet printer ---

static int decode_and_print(const uint8_t* data, size_t len, int pkt_index) {
    PacketDecoder dec;
    if (!dec.parse(data, len)) {
        fprintf(stderr, "  [%d] PARSE ERROR: cannot decode packet header (%zu bytes)\n",
                pkt_index, len);
        printf("  raw: %s\n", hex_bytes(data, len, 64).c_str());
        return 1;
    }

    const PacketHeader& hdr = dec.header();
    bool cksum_ok = dec.verify_checksum();

    printf("[%d] %s v%u conn=%u pkt#%u ts=%u us payload=%u bytes flags=[%s] checksum=%s\n",
           pkt_index,
           packet_type_str(hdr.packet_type),
           hdr.version,
           hdr.connection_id,
           hdr.packet_number,
           hdr.timestamp_us,
           hdr.payload_length,
           flags_str(hdr.flags).c_str(),
           cksum_ok ? "OK" : "FAIL");

    Frame frame;
    int frame_idx = 0;
    while (dec.next_frame(frame)) {
        printf("  [%d.%d] %s (0x%02X) len=%u\n",
               pkt_index, frame_idx,
               frame_type_str(frame.header.type),
               static_cast<unsigned>(frame.header.type),
               frame.header.length);
        print_frame_detail(frame);
        frame_idx++;
    }

    if (frame_idx == 0 && hdr.payload_length > 0) {
        printf("  (no decodable frames in %u payload bytes)\n", hdr.payload_length);
    }

    return 0;
}

// --- Hex string parser ---

static std::vector<uint8_t> parse_hex_string(const std::string& hex) {
    std::vector<uint8_t> bytes;
    std::string clean;
    for (char c : hex) {
        if (std::isxdigit(c)) clean += c;
    }
    if (clean.size() % 2 != 0) {
        fprintf(stderr, "Error: hex string has odd length\n");
        return {};
    }
    for (size_t i = 0; i < clean.size(); i += 2) {
        uint8_t byte = static_cast<uint8_t>(
            std::stoul(clean.substr(i, 2), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

// --- Length-prefixed binary reader ---
// Format: [uint16_t BE length][packet bytes] repeated

static int read_length_prefixed(std::istream& in) {
    int pkt_index = 0;
    int errors = 0;

    while (in.good()) {
        uint8_t len_buf[2];
        in.read(reinterpret_cast<char*>(len_buf), 2);
        if (in.gcount() < 2) break;

        uint16_t pkt_len = static_cast<uint16_t>((len_buf[0] << 8) | len_buf[1]);
        if (pkt_len == 0 || pkt_len > MAX_PACKET_SIZE) {
            fprintf(stderr, "  [%d] Invalid packet length: %u\n", pkt_index, pkt_len);
            errors++;
            break;
        }

        std::vector<uint8_t> buf(pkt_len);
        in.read(reinterpret_cast<char*>(buf.data()), pkt_len);
        if (static_cast<size_t>(in.gcount()) < pkt_len) {
            fprintf(stderr, "  [%d] Truncated packet: expected %u bytes, got %lld\n",
                    pkt_index, pkt_len, static_cast<long long>(in.gcount()));
            errors++;
            break;
        }

        errors += decode_and_print(buf.data(), buf.size(), pkt_index);
        pkt_index++;
    }

    printf("\n--- %d packet(s) decoded, %d error(s) ---\n", pkt_index, errors);
    return errors > 0 ? 1 : 0;
}

// --- Raw binary reader (try to decode as single packet) ---

static int read_raw(std::istream& in) {
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (buf.empty()) {
        fprintf(stderr, "Error: empty input\n");
        return 1;
    }

    // If it starts with a valid packet header, try single packet decode
    if (buf.size() >= PACKET_HEADER_SIZE) {
        return decode_and_print(buf.data(), buf.size(), 0);
    }

    fprintf(stderr, "Error: input too short for a packet (%zu bytes, need >= %zu)\n",
            buf.size(), PACKET_HEADER_SIZE);
    return 1;
}

// --- Main ---

static void print_usage(const char* prog) {
    printf("Usage:\n");
    printf("  %s [options] [file]\n\n", prog);
    printf("Options:\n");
    printf("  --hex <hexstring>   Decode a hex-encoded packet\n");
    printf("  --lp                Input is length-prefixed (2-byte BE length + packet)\n");
    printf("  --raw               Input is a single raw packet (default)\n");
    printf("  -h, --help          Show this help\n\n");
    printf("If no file is given, reads from stdin.\n");
}

int main(int argc, char** argv) {
    bool use_hex = false;
    bool length_prefixed = false;
    std::string hex_data;
    std::string filename;

    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--hex" && i + 1 < argc) {
            use_hex = true;
            hex_data = argv[++i];
        } else if (arg == "--lp") {
            length_prefixed = true;
        } else if (arg == "--raw") {
            length_prefixed = false;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            filename = arg;
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    if (use_hex) {
        auto bytes = parse_hex_string(hex_data);
        if (bytes.empty()) return 1;
        return decode_and_print(bytes.data(), bytes.size(), 0);
    }

    if (!filename.empty()) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            fprintf(stderr, "Error: cannot open '%s'\n", filename.c_str());
            return 1;
        }
        if (length_prefixed) return read_length_prefixed(file);
        return read_raw(file);
    }

    // Read from stdin
    std::ios::sync_with_stdio(false);
    if (length_prefixed) return read_length_prefixed(std::cin);
    return read_raw(std::cin);
}
