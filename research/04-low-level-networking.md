# 04 - Low-Level C/C++ Networking for Maximum Performance

## Overview
Building blocks for implementing a custom protocol with the lowest possible latency.

---

## 1. Raw Socket Programming

### Custom Protocol Header Design
```c
struct __attribute__((packed)) proto_header {
    uint8_t  version;        // Protocol version
    uint8_t  msg_type;       // Message type
    uint16_t flags;          // Control flags
    uint32_t sequence;       // Sequence number
    uint32_t payload_len;    // Payload length
    uint32_t checksum;       // CRC32 or xxHash
    uint64_t timestamp_ns;   // Nanosecond timestamp
};  // 24 bytes, naturally aligned
```

### Design Principles
- Fixed-size headers (avoid parsing branches)
- Align to 4/8-byte boundaries (avoid unaligned access penalties)
- Most-checked fields first (version, type, length)
- Include sequence number from the start

---

## 2. Building Reliable-Enough Delivery on UDP

### Why UDP
TCP's latency problems are **fundamental**:
- Head-of-line blocking (single lost segment stalls everything)
- Nagle's algorithm batches small writes
- Slow start on new connections
- Mandatory in-order delivery
- TIME_WAIT holds resources for ~60s

### QUIC-Inspired Reliable UDP Architecture
```
[Stream Abstraction Layer]  -- multiplexed logical streams
        |
[Reliability Layer]         -- ACKs, retransmission, ordering per-stream
        |
[Congestion Control]        -- BBR, CUBIC, or custom
        |
[Packet Framing]            -- frames packed into datagrams
        |
[Encryption]                -- optional TLS 1.3 style
        |
[UDP Socket]
```

### Tiered Reliability (Per Message Type)
| Mode | Behavior | Use Case |
|------|----------|----------|
| Unreliable | Fire-and-forget | Ephemeral state (cursor position) |
| Unreliable-sequenced | Drop out-of-order | Latest-state-wins |
| Reliable-unordered | Retransmit, any order | Independent state updates |
| Reliable-ordered | Full TCP semantics per stream | Critical operations |

### Key Implementation Details
- **Selective ACK (SACK)**: Every ACK contains ranges of received packets
- **Never retransmit same packet number** (QUIC approach): Eliminates retransmission ambiguity
- **Adaptive RTO**: `RTO = SRTT + max(4 * RTTVAR, 1ms)`
- **Loss detection**: Packet gap >= 3 OR time threshold exceeded

---

## 3. Async I/O: io_uring (Linux) and IOCP (Windows)

### io_uring Architecture
Two shared-memory ring buffers between userspace and kernel:
- **Submission Queue (SQ)**: Userspace writes I/O requests
- **Completion Queue (CQ)**: Kernel writes results

**No syscalls in hot path** with `IORING_SETUP_SQPOLL` (kernel thread polls SQ).

### Key io_uring Operations for Networking
- `IORING_OP_RECV/SEND` -- Basic ops
- `IORING_OP_RECVMSG/SENDMSG` -- Scatter-gather
- `IORING_OP_PROVIDE_BUFFERS` -- Pre-registered buffers for zero-copy
- `IORING_OP_SEND_ZC` -- Zero-copy send (Linux 6.0+)
- **Multishot recv**: One SQE generates multiple CQEs (one per packet)

### Performance
- io_uring submission: ~100-200ns (vs ~500-1000ns syscall)
- With SQPOLL: submissions free (just memory write)
- Throughput: >10M requests/second per core

### IOCP (Windows)
```c
HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, num_threads);
CreateIoCompletionPort((HANDLE)socket, iocp, (ULONG_PTR)data, 0);
WSARecv(socket, &buf, 1, NULL, &flags, &overlapped, NULL);
GetQueuedCompletionStatus(iocp, &bytes, &key, &ov, INFINITE);
```

### Registered I/O (RIO) -- Windows 8+
Pre-registered buffers + completion queues. ~2-3x throughput over standard IOCP for UDP.

### Cross-Platform Strategy
Abstract I/O layer: io_uring on Linux, IOCP/RIO on Windows. Same conceptual model.

---

## 4. Zero-Copy Networking

| Technique | Platform | Direction | Min Beneficial Size |
|-----------|----------|-----------|-------------------|
| `sendfile()` | Linux | Send (file->socket) | Any |
| `splice()` | Linux | Send (fd->fd via pipe) | Any |
| `MSG_ZEROCOPY` | Linux 4.14+ | Send | >10KB |
| `io_uring SEND_ZC` | Linux 6.0+ | Send | >10KB |
| `PACKET_RX_RING` | Linux | Recv (raw) | Any |
| TransmitFile() | Windows | Send (file->socket) | Any |
| RIO | Windows 8+ | Both | Any |

**Important**: MSG_ZEROCOPY has overhead for notification + page pinning. Only wins for large messages (>10KB). For small messages, the copy is cheaper.

---

## 5. DPDK -- Kernel Bypass Networking

### Architecture
```
Traditional: App -> syscall -> kernel TCP/IP -> driver -> NIC
DPDK:        App -> DPDK PMD (Poll Mode Driver) -> NIC (via UIO/VFIO)
```

### Performance
- Latency: **~1-2 microseconds** wire-to-userspace (vs ~10-50us through kernel)
- Throughput: Saturates 100 Gbps with a few cores
- Tradeoff: Dedicates CPU cores to polling (100% usage even when idle)

### Basic Receive Loop
```c
#define BURST_SIZE 32
struct rte_mbuf *bufs[BURST_SIZE];
while (1) {
    uint16_t nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);
    for (int i = 0; i < nb_rx; i++) {
        process_packet(bufs[i]);
        rte_pktmbuf_free(bufs[i]);
    }
}
```

### Alternatives
- **AF_XDP**: ~24 Mpps single core, kernel-friendly, near-DPDK performance
- **Netmap**: BSD-licensed, simpler API
- **PF_RING ZC**: Commercial, AF_PACKET compatible

---

## 6. eBPF/XDP -- Programmable Packet Processing

### XDP Actions
- `XDP_DROP`: Drop at line rate (DDoS mitigation)
- `XDP_TX`: Bounce back out same NIC
- `XDP_REDIRECT`: Send to another NIC, CPU, or AF_XDP socket
- `XDP_PASS`: Normal kernel processing

### AF_XDP Sockets
Bridge between XDP and userspace. Shared-memory ring buffer (UMEM). Near-DPDK performance while keeping kernel in the loop.

### For Our Protocol
- Parse custom protocol header in eBPF for fast-path processing
- Route/filter without full kernel stack
- Collect per-packet latency stats with nanosecond precision

---

## 7. Lock-Free Data Structures

### SPSC Queue (Single-Producer, Single-Consumer)
```c
struct spsc_queue {
    _Alignas(64) _Atomic(uint64_t) head;  // Producer writes
    _Alignas(64) _Atomic(uint64_t) tail;  // Consumer writes
    void *buffer[];                        // Power-of-2 size
};
```
~10-20ns per operation. Essentially cost of a cache-line transfer.

### LMAX Disruptor Pattern
- **Single-writer principle**: Each variable written by only one thread
- **Ring buffer with sequence barriers**: Pre-allocated, power-of-2
- **Mechanical sympathy**: Cache-line-sized entries, no false sharing
- **Batching**: Process 100 entries, update sequence once
- **Performance**: <50ns inter-thread messaging, 100M+ msg/sec

### Pipeline Architecture
```
NIC -> [Receive Ring] -> Parser Thread -> [Parsed Ring] -> Logic Thread -> [Send Ring] -> NIC
```
Each ring is SPSC. Zero lock contention.

---

## 8. Serialization Formats

### Ranked by Overhead (Low to High)
| Format | Encode Cost | Decode Cost | Schema Evolution | Random Access |
|--------|------------|-------------|------------------|--------------|
| Raw structs | Zero | Zero | No | Yes |
| Cap'n Proto | Zero | Zero | Yes | Yes |
| FlatBuffers | Near-zero | Near-zero | Yes | Yes |
| SBE | Near-zero | Near-zero | Limited | Yes |
| MessagePack | Low | Low | Self-describing | No |
| Protobuf | Moderate | Moderate | Yes | No |

### Recommendation: Tiered Approach
- **Data plane (hot path)**: Cap'n Proto or raw structs. Zero decode cost.
- **Control plane (setup, config)**: Protobuf or FlatBuffers. Schema evolution matters.
- **Debug/logging**: MessagePack or JSON. Human-readable.

---

## 9. HFT Latency Techniques (Applicable to Our Protocol)

### The Optimization Stack (Ordered by Impact)
| # | Technique | Savings |
|---|-----------|---------|
| 1 | Kernel bypass (DPDK/OpenOnload) | 5-20us |
| 2 | Busy-wait polling | 1-5us |
| 3 | Core isolation + pinning | 1-10us |
| 4 | Zero-copy / pre-registered buffers | 0.5-2us |
| 5 | Zero-overhead serialization | 0.1-1us |
| 6 | Lock-free inter-thread communication | 0.5-5us |
| 7 | Hugepages (2MB/1GB) | 0.1-1us |
| 8 | NUMA-local memory | ~0.1us/access |
| 9 | Disable CPU power management | Eliminates tail spikes |
| 10 | Pre-allocated memory / object pools | Eliminates malloc spikes |

### Minimum Achievable Latency
| Method | Latency |
|--------|---------|
| Shared memory (same machine) | **30-80ns** |
| RDMA (same datacenter) | **0.5-2us** |
| DPDK (same datacenter) | **1.5-4us** |
| Kernel UDP (same datacenter) | 5-20us |
| Kernel TCP (same datacenter) | 10-50us |
| Cross-internet | Physics-limited (~5ms/1000km fiber) |

---

## 10. Game Engine State Replication Patterns

### Unreal Engine
- **Property replication**: `UPROPERTY(Replicated)` auto-tracked for changes
- **Per-connection shadow state**: Diff against last-acknowledged state
- **Priority + bandwidth budgeting**: Sort by priority * staleness, fit into budget
- **Quantization**: Floats -> fixed-point, quaternions -> smallest-three encoding

### Key Patterns for Our Protocol
- **Snapshot interpolation**: Buffer 2-3 snapshots, interpolate between
- **Input prediction**: Send inputs, predict result, reconcile on server response
- **Interest management**: Only send state that matters to a given client
- **Bit-packing**: Pack booleans into 1 bit, enums into 3 bits, etc.

---

## 11. Recommended Architecture

```
                    +-----------------------+
                    |   Application Logic   |
                    | (single-threaded core)|
                    +----------+------------+
                               |
                +--------------+--------------+
                |                             |
        +-------+-------+            +-------+-------+
        | Inbound Ring  |            | Outbound Ring |
        | (Disruptor)   |            | (Disruptor)   |
        +-------+-------+            +-------+-------+
                |                             |
        +-------+-------+            +-------+-------+
        | Decode Thread |            | Encode Thread |
        | (Cap'n Proto) |            | (Cap'n Proto) |
        +-------+-------+            +-------+-------+
                |                             |
        +-------+-------+            +-------+-------+
        |  I/O Thread   |            |  I/O Thread   |
        | (io_uring /   |            | (io_uring /   |
        |  IOCP+RIO)    |            |  IOCP+RIO)    |
        +---------------+            +---------------+
                |                             |
              [NIC]                         [NIC]
```

Each layer pinned to isolated core. Lock-free rings between layers. Zero-copy buffers throughout.
