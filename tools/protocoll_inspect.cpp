// protocoll-inspect: connect to a peer and inspect state tree.
//
// Connects to a running protocoll peer, subscribes to all state paths,
// and displays the state tree with real-time updates.
//
// Usage:
//   protocoll-inspect <host> <port> [--watch]
//   protocoll-inspect --local    (inspect via loopback for testing)

#include "protocoll/transport/transport.h"
#include "protocoll/transport/udp_transport.h"
#include "protocoll/wire/codec.h"
#include "protocoll/wire/frame.h"
#include "protocoll/wire/frame_types.h"
#include "protocoll/util/platform.h"
#include "protocoll/security/crypto.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <csignal>
#include <iostream>

using namespace protocoll;

static volatile bool g_running = true;

static void signal_handler(int) {
    g_running = false;
}

// --- String helpers (duplicated from dump for standalone binary) ---

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

// --- Tracked state ---

struct TrackedState {
    std::string path;
    uint32_t path_hash;
    CrdtType crdt_type;
    Reliability reliability;
    uint32_t delta_count = 0;
    uint32_t snapshot_count = 0;
    uint64_t total_bytes = 0;
    std::chrono::steady_clock::time_point last_update;
};

static std::unordered_map<uint32_t, TrackedState> g_states;
static uint32_t g_total_packets = 0;
static uint32_t g_total_frames = 0;

// --- Packet processor ---

static void process_packet(const uint8_t* data, size_t len) {
    PacketDecoder dec;
    if (!dec.parse(data, len)) return;

    g_total_packets++;

    Frame frame;
    while (dec.next_frame(frame)) {
        g_total_frames++;

        switch (frame.header.type) {
            case FrameType::STATE_DECLARE: {
                StateDeclareFrame sd;
                if (sd.decode(frame.payload, frame.header.length)) {
                    TrackedState ts;
                    ts.path_hash = sd.path_hash;
                    ts.crdt_type = sd.crdt_type;
                    ts.reliability = sd.reliability;
                    if (frame.header.length > StateDeclareFrame::BASE_WIRE_SIZE) {
                        size_t str_len = frame.header.length - StateDeclareFrame::BASE_WIRE_SIZE;
                        ts.path.assign(
                            reinterpret_cast<const char*>(frame.payload + StateDeclareFrame::BASE_WIRE_SIZE),
                            str_len);
                    }
                    ts.last_update = std::chrono::steady_clock::now();
                    g_states[sd.path_hash] = ts;
                    printf("[DECLARE] %s (0x%08X) crdt=%s reliability=%s\n",
                           ts.path.c_str(), sd.path_hash,
                           crdt_type_str(sd.crdt_type),
                           reliability_str(sd.reliability));
                }
                break;
            }
            case FrameType::STATE_DELTA: {
                StateDeltaFrame sdf;
                if (frame.header.length >= StateDeltaFrame::BASE_WIRE_SIZE &&
                    sdf.decode(frame.payload, frame.header.length)) {
                    auto it = g_states.find(sdf.path_hash);
                    if (it != g_states.end()) {
                        it->second.delta_count++;
                        it->second.total_bytes += frame.header.length;
                        it->second.last_update = std::chrono::steady_clock::now();
                    }
                    printf("[DELTA] 0x%08X author=%u %u bytes\n",
                           sdf.path_hash, sdf.author_node_id, frame.header.length);
                }
                break;
            }
            case FrameType::STATE_SNAPSHOT: {
                StateSnapshotFrame ssf;
                if (frame.header.length >= StateSnapshotFrame::BASE_WIRE_SIZE &&
                    ssf.decode(frame.payload, frame.header.length)) {
                    auto it = g_states.find(ssf.path_hash);
                    if (it != g_states.end()) {
                        it->second.snapshot_count++;
                        it->second.total_bytes += frame.header.length;
                        it->second.last_update = std::chrono::steady_clock::now();
                    }
                    printf("[SNAPSHOT] 0x%08X author=%u %u bytes\n",
                           ssf.path_hash, ssf.author_node_id, frame.header.length);
                }
                break;
            }
            default:
                break;
        }
    }
}

// --- Print state tree ---

static void print_state_tree() {
    printf("\n=== State Tree (%zu regions) ===\n", g_states.size());
    printf("%-40s %-14s %-12s %8s %8s %10s\n",
           "Path", "CRDT", "Reliability", "Deltas", "Snaps", "Bytes");
    printf("%-40s %-14s %-12s %8s %8s %10s\n",
           "----", "----", "-----------", "------", "-----", "-----");

    for (const auto& [hash, state] : g_states) {
        std::string display = state.path.empty()
            ? ("0x" + ([&]() {
                char buf[16];
                snprintf(buf, sizeof(buf), "%08X", hash);
                return std::string(buf);
            })())
            : state.path;

        printf("%-40s %-14s %-12s %8u %8u %10llu\n",
               display.c_str(),
               crdt_type_str(state.crdt_type),
               reliability_str(state.reliability),
               state.delta_count,
               state.snapshot_count,
               static_cast<unsigned long long>(state.total_bytes));
    }

    printf("\nTotal: %u packets, %u frames\n", g_total_packets, g_total_frames);
}

// --- Listen mode ---

static int listen_mode(const std::string& host, uint16_t port, bool watch) {
    UdpTransport udp;
    Endpoint local;
    local.address = host;
    local.port = port;

    if (!udp.bind(local)) {
        fprintf(stderr, "Error: cannot bind to %s:%u\n", host.c_str(), port);
        return 1;
    }

    printf("Listening on %s:%u ...\n", host.c_str(), port);
    printf("Press Ctrl+C to stop.\n\n");

    std::signal(SIGINT, signal_handler);

    uint8_t buf[MAX_PACKET_SIZE];
    while (g_running) {
        Endpoint from;
        int n = udp.recv_from(buf, sizeof(buf), from, 100);
        if (n > 0) {
            process_packet(buf, static_cast<size_t>(n));
        }
    }

    print_state_tree();
    udp.close();
    return 0;
}

// --- Passive capture mode (read from stdin) ---

static int passive_mode(bool watch) {
    printf("Reading packets from stdin (length-prefixed format)...\n\n");

    uint8_t len_buf[2];
    while (std::cin.read(reinterpret_cast<char*>(len_buf), 2)) {
        uint16_t pkt_len = static_cast<uint16_t>((len_buf[0] << 8) | len_buf[1]);
        if (pkt_len == 0 || pkt_len > MAX_PACKET_SIZE) break;

        std::vector<uint8_t> buf(pkt_len);
        std::cin.read(reinterpret_cast<char*>(buf.data()), pkt_len);
        if (static_cast<size_t>(std::cin.gcount()) < pkt_len) break;

        process_packet(buf.data(), buf.size());
    }

    print_state_tree();
    return 0;
}

// --- Main ---

static void print_usage(const char* prog) {
    printf("Usage:\n");
    printf("  %s <host> <port> [--watch]  Listen on UDP and inspect packets\n", prog);
    printf("  %s --stdin [--watch]         Read length-prefixed packets from stdin\n", prog);
    printf("  %s -h | --help              Show this help\n\n", prog);
    printf("Options:\n");
    printf("  --watch    Show real-time updates (default: exit after Ctrl+C)\n");
}

int main(int argc, char** argv) {
    bool watch = false;
    bool use_stdin = false;
    std::string host;
    uint16_t port = 0;

    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--watch") {
            watch = true;
        } else if (arg == "--stdin") {
            use_stdin = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (host.empty() && arg[0] != '-') {
            host = arg;
        } else if (port == 0 && !host.empty()) {
            port = static_cast<uint16_t>(std::atoi(arg.c_str()));
        } else {
            fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            print_usage(argv[0]);
            return 1;
        }
    }

    if (use_stdin) {
        return passive_mode(watch);
    }

    if (!host.empty() && port > 0) {
        return listen_mode(host, port, watch);
    }

    // Default: stdin mode
    if (host.empty() && port == 0) {
        return passive_mode(watch);
    }

    print_usage(argv[0]);
    return 1;
}
