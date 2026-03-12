# 07 - Beyond UDP: Breaking the Transport Layer Paradigm

## Overview
Deep analysis of whether UDP is a bottleneck for innovation, what lies below it, and how to build a transport that is state-semantic-aware from the ground up.

---

## The Core Question

UDP has been here for 40+ years. If we're reinventing the application protocol, should we accept a decades-old transport as our foundation?

### The Verdict (Three Parts)

1. **UDP's 8-byte header is NOT the bottleneck.** The kernel networking stack (~7-25us per packet) is the real cost. UDP protocol processing itself is ~100-200ns.

2. **UDP is the minimum viable internet container.** SCTP (protocol 132) and DCCP (protocol 33) were killed by middlebox ossification. NATs only understand TCP and UDP. Fighting this is futile.

3. **In controlled environments, UDP is unnecessary.** Kernel bypass (DPDK/AF_XDP), RDMA, custom EtherTypes, and CXL shared memory all operate below UDP with dramatically better performance.

---

## 1. The Kernel Is the Real Bottleneck

### Per-Packet Cost Through Linux Kernel

| Stage | Cost |
|-------|------|
| Hardware interrupt + IRQ | 1-5 us |
| Softirq/NAPI poll | 2-5 us |
| sk_buff allocation (~256 bytes metadata) | 0.5-2 us |
| Protocol processing (IP + UDP) | 1-3 us |
| Socket buffer queueing | 0.5-1 us |
| Context switch to userspace | 1-5 us |
| Copy to user buffer | 0.5-3 us |
| **Total kernel path** | **7-25 us** |
| **Wire latency (same rack, 25GbE)** | **0.5-1 us** |

**The kernel adds 10-50x the hardware wire latency.** The single biggest cost is aggregate memory indirection and cache pollution -- `sk_buff` touches dozens of cache lines across multiple subsystems per packet.

### Bypass Comparison

| Method | RTT Latency | Throughput/Core |
|--------|------------|----------------|
| Kernel UDP | 20-50 us | 1-5 Mpps |
| io_uring + SQPOLL | 10-15 us | 2-5 Mpps |
| AF_XDP (zero-copy) | 5-10 us | 10-24 Mpps |
| DPDK poll-mode | 2-5 us | 15-40 Mpps |
| RDMA (InfiniBand) | 1-2 us | Line rate |
| RDMA (RoCEv2) | 2-3 us | Line rate |
| CXL shared memory | 0.2-0.6 us | Memory bandwidth |

---

## 2. What Lives Below UDP

### Raw IP Protocols

Protocol numbers 143-252 are unassigned. Numbers 253-254 are reserved for experimentation (RFC 3692).

**The SCTP/DCCP Lesson**: Both were well-designed IETF standards that solved real problems. Both were killed by middlebox ossification. SCTP took ~20 years to see deployment, and only in telecom networks where operators control middleboxes.

**Practical barriers for custom IP protocols:**
- Firewalls default-deny anything not TCP/UDP/ICMP
- NATs can't multiplex (no port concept)
- Cloud VPCs may not forward custom protocols
- Requires root/CAP_NET_RAW
- **Verdict**: Viable only in controlled environments

### Custom EtherTypes (Layer 2)

Register a custom EtherType (e.g., 0x88B5 for local experimental use, or ~$3,000 for IEEE registration). This eliminates IP and UDP headers entirely.

**Pros**: Maximum efficiency, clean protocol, easy NIC classification
**Cons**: Not routable across L3, limited to single L2 domain
**Use case**: Datacenter same-rack, where all nodes share a switch

### RDMA: The Transport Disappears

RDMA bypasses the **entire** networking stack:

```
Traditional: App -> syscall -> kernel TCP/IP -> driver -> NIC
RDMA:        App -> RDMA verbs (userspace) -> NIC hardware -> wire
```

**One-sided operations** are transformative: `RDMA WRITE` pushes data directly into remote memory without involving the remote CPU. The remote node doesn't even know it happened until it checks.

For state streaming: the sender RDMA-writes a state delta directly into the receiver's memory. **Zero kernel, zero remote CPU, sub-microsecond.**

**Transports:**
| Flavor | Latency | Requirement |
|--------|---------|-------------|
| InfiniBand | ~1 us | Dedicated fabric |
| RoCEv2 | ~2-3 us | Lossless Ethernet (PFC/ECN) |
| iWARP | ~5-10 us | Standard Ethernet (over TCP) |

### CXL 3.0: The Protocol Vanishes Entirely

CXL (Compute Express Link) provides **hardware-coherent shared memory** across machines:

| Access Type | Latency |
|-------------|---------|
| Local DRAM | ~80-100 ns |
| CXL same server | ~150-250 ns |
| CXL through switch | ~200-400 ns |
| CXL multi-hop fabric | ~300-600 ns (estimated) |

With CXL 3.0:
- Multiple hosts map the same physical memory
- Hardware handles cache coherency (back-invalidate snooping)
- A CRDT G-Counter merge = **atomic add to shared memory location**
- No packets, no headers, no serialization

**Limitation**: Rack-scale only (meters, not kilometers). CXL 3.0 hardware arriving 2024-2025, broad deployment later.

---

## 3. Novel Transport Protocols: Academic State of the Art

### Homa (Stanford, John Ousterhout) -- SIGCOMM 2018, ATC 2021

**Core insight**: The receiver is in the best position to schedule traffic.

**Key mechanisms:**
- **Receiver-driven**: Receiver explicitly grants permission to senders
- **Unscheduled bytes** (first ~10KB): Sent immediately, no waiting. Small messages complete in 0 extra RTTs.
- **Scheduled bytes** (large messages): Receiver sends GRANT packets controlling rate and priority
- **8 priority levels** in switch queues: SRPT scheduling (shortest remaining first)
- **No congestion window**: No slow start, no AIMD. Messages begin immediately.

**Performance**: P99 tail latency **50-100x better than TCP** for short messages under load.

**Key lesson for us**: State consumers should control the rate. Small deltas sent immediately (unscheduled). Large snapshots use receiver-granted scheduling.

### eRPC (Carnegie Mellon) -- NSDI 2019

**Thesis**: You don't need RDMA or custom hardware. A well-engineered userspace library over standard UDP matches RDMA performance.

- **10M small RPCs/sec** on a single core
- **2.5 us median latency**
- Zero-copy via pre-registered memory + hugepages
- RTT-based congestion control (Timely-inspired) with microsecond precision

**Key lesson**: UDP is sufficient if you bypass the kernel and engineer carefully. The protocol wire format matters less than implementation quality.

### NDP (SIGCOMM 2017) -- Receiver-Driven with Switch Trimming

- Senders blast at line rate
- Congested switches **trim packets to headers** (discard payload, forward header)
- Receiver sees all headers, pulls full data for what it needs
- Per-packet ECMP spraying eliminates hotspots

**Key lesson**: Under congestion, "trim" state updates to metadata (object ID + version). Let receivers pull full updates selectively.

### Google Snap (SOSP 2019) -- Userspace Networking at Scale

- Custom transport (**Pony Express**) in userspace on every Google server
- Dedicated process with kernel bypass
- **Swift** congestion control: delay-based, deployed at millions-of-servers scale
- **Weekly** iteration on congestion control algorithms (impossible with kernel transport)
- Performance isolation between applications

**Key lesson**: Userspace transport is production-viable at the largest scale. Rapid CC iteration is a strategic advantage.

### AWS SRD (Scalable Reliable Datagram) -- IEEE Micro 2020

- Custom transport on **Nitro Card** (SmartNIC)
- Connectionless datagrams with multi-path spraying
- Per-packet load balancing across all fabric paths
- P99 within 2x of P50 (vs 10-100x for TCP)
- **All protocol processing offloaded to hardware**

**Key lesson**: Hardware offload for reliability (ACK/retransmit on SmartNIC) removes host CPU from critical path entirely.

### Cornflakes (OSDI 2023) -- Zero-Copy Serialization

- Serialize directly from scattered application memory using NIC scatter-gather DMA
- **No memcpy** in the send path
- 30-50% throughput improvement for payloads >256 bytes

**Key lesson**: Our wire format must be designed for scatter-gather from day one. State objects should be DMA-able directly from application memory.

---

## 4. State-Semantic-Aware Transport: The Innovation Space

**No existing transport understands application semantics.** This is our protocol's unique design point.

### What State-Awareness Means at the Transport Level

| Feature | Traditional Transport | State-Aware Transport |
|---------|----------------------|----------------------|
| Loss handling | Retransmit everything | Drop stale, retransmit only if newer doesn't exist |
| Ordering | Strict in-order delivery | Per-state-object ordering; cross-object unordered |
| Congestion | Reduce rate uniformly | Drop low-priority state, keep critical |
| Flow control | Byte-count windows | Credit-based per subscription |
| Dedup | Sequence number only | Version vector comparison |
| Multicast | N unicast copies | In-network replication by subscription |

### Minimum State-Streaming Header

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Ver  | Flags |    StateID (variable, 2-8 bytes)              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Generation/Version Number (4 bytes)                 |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Delta Type   |           Payload...                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**8-14 bytes** of transport header. Any network element can inspect the Generation field and drop stale packets.

### State-Aware Congestion/Flow Control

- **Sender**: Maintains "state outbox" per subscriber. Under congestion, skips intermediate versions, sends only latest.
- **Network** (P4 switches): Inspects Generation numbers, drops packets with lower generation than what already passed. **In-network state coalescing.**
- **Receiver**: Sends "I have generation X" acknowledgments. Sender only sends deltas from receiver's known generation.
- **No retransmission of stale data**: If generation 5 is lost but 7 was sent, 5 is never retransmitted.

---

## 5. Programmable Hardware for Our Protocol

### P4 Switches: In-Network CRDT Merging

| CRDT Type | In-Switch Feasible? | Mechanism |
|-----------|-------------------|-----------|
| G-Counter | YES | Stateful ALU: max(stored, incoming) |
| PN-Counter | YES | Two register operations |
| LWW-Register | YES | Compare timestamps, keep newer |
| Bloom filter union | YES | Bitwise OR |
| OR-Set | NO | Needs variable-size state |
| Vector clocks | PARTIAL | Fixed-size, across pipeline stages |

**In-flight delta aggregation**: Two deltas for the same destination merged inside the switch at **~500ns-1us**. Proven by SwitchML for ML gradients.

**Caveat**: Intel cancelled Tofino line (late 2023). Alternatives: AMD/Pensando, or SmartNIC-based P4.

### SmartNICs (BlueField-3 DPU)

- 16 ARM A78 cores + hardware accelerators on the NIC
- Run full custom protocol stack on ARM cores
- Hardware pipeline for: parsing, classification, checksums, crypto, filtering
- **NIC can drop stale packets before interrupting host CPU** by comparing version numbers in hardware
- Latency: 0.5-2us for hardware pipeline, 2-5us for ARM processing

### FPGA NICs

- **Sub-microsecond** deterministic protocol processing
- Can implement CRDT merge logic in hardware
- HFT firms build entire custom transport protocols on FPGA
- Example: ExaNIC achieves ~900ns port-to-port

### NIC Hardware Offloads for Custom Protocols

- **Mellanox ConnectX-6+ flex parser**: Teach NIC your custom headers
- **Intel E810 DDP**: Load custom protocol profiles
- **RSS on custom fields**: Distribute your protocol's flows across cores
- **Hardware timestamps**: Nanosecond-precision PTP timestamps per packet

---

## 6. The Layered Transport Architecture

### Our Protocol's Multi-Layer Design

```
+=========================================================+
|              PROTOCOL SEMANTICS (Identical)               |
|   State declarations, deltas, CRDTs, subscriptions,      |
|   version vectors, capabilities, backpressure             |
+=========================================================+
|              TRANSPORT ABSTRACTION LAYER                  |
+=========+=========+=========+=========+=========+=======+
| Mode 1  | Mode 2  | Mode 3  | Mode 4  | Mode 5  |Mode 6|
| UDP/    | AF_XDP  | DPDK    | RDMA    | CXL     |WebTr.|
| io_uring|         |         | Write   | shmem   |      |
+---------+---------+---------+---------+---------+-------+
| <50us   | <10us   | <5us    | <3us    | <0.5us  |<100us|
| Any net | Linux   | Linux   | RDMA NIC| Rack    |Browser|
+---------+---------+---------+---------+---------+-------+
```

### Transport Negotiation

On connection, peers negotiate the best available transport:
1. Both in same rack with CXL? -> Mode 5 (shared memory)
2. Both have RDMA NICs on lossless fabric? -> Mode 4
3. Linux with AF_XDP support? -> Mode 2
4. Standard internet? -> Mode 1 (UDP)
5. Browser client? -> Mode 6 (WebTransport/QUIC)

**Same state-streaming semantics, 1000x performance range.**

---

## 7. Design Principles from Novel Transport Research

### Synthesized from Homa, eRPC, NDP, Snap, SRD, Cornflakes

| # | Principle | Source | How It Applies |
|---|-----------|--------|---------------|
| 1 | **Receiver-driven flow** | Homa, NDP, NDN | State consumers control update rate via credits |
| 2 | **Priority by semantic importance** | Homa (SRPT) | Fresh > stale, critical > ambient, small delta > large |
| 3 | **Message-oriented, not byte-stream** | R2P2, SCTP | State updates are discrete objects with boundaries |
| 4 | **Multi-stream without HOL blocking** | SCTP, QUIC | Each state object = independent stream |
| 5 | **Zero-copy serialization** | Cornflakes, eRPC | DMA directly from application state memory |
| 6 | **Rate-based congestion control** | Swift, DCQCN, Timely | RTT-based with hardware timestamps |
| 7 | **Multi-path native** | SRD, MPTCP | Per-packet spraying (DC), per-subflow (internet) |
| 8 | **Userspace implementation** | Snap, eRPC | Rapid iteration, zero-copy, per-app isolation |
| 9 | **Trim under congestion** | NDP | Send metadata only; receivers pull full updates |
| 10 | **Content-aware networking** | NDN, R2P2 | Subscriptions as first-class network objects |

---

## 8. Key Academic References

### Must-Read Papers

| Paper | Venue | Year | Key Contribution |
|-------|-------|------|-----------------|
| Homa | SIGCOMM | 2018 | Receiver-driven priority transport |
| eRPC | NSDI | 2019 | UDP matches RDMA with good engineering |
| NDP | SIGCOMM | 2017 | Trim-and-pull under congestion |
| Snap (Google) | SOSP | 2019 | Userspace transport at hyperscale |
| Swift | SIGCOMM | 2020 | Delay-based CC for datacenter |
| SRD (AWS) | IEEE Micro | 2020 | Multi-path hardware-offloaded transport |
| Cornflakes | OSDI | 2023 | Zero-copy scatter-gather serialization |
| DCQCN | SIGCOMM | 2015 | Rate-based CC for RDMA |
| AccelNet | NSDI | 2018 | FPGA SmartNIC at cloud scale |
| pFabric | SIGCOMM | 2013 | Near-optimal with just priorities |
| NetCache | SOSP | 2017 | In-network state caching |
| SwitchML | NSDI | 2021 | In-network aggregation |
| Ensō | OSDI | 2023 | Streaming NIC interface |
| NDN | CoNEXT | 2009 | Named content networking |
| FaRM | NSDI | 2014 | Distributed computing over RDMA |

### The Takeaway

> **UDP is not the constraint -- the kernel is. And for controlled environments, even the kernel is unnecessary. Our protocol should be transport-agnostic, with the same state-streaming semantics across a 1000x performance range from browser WebTransport to CXL shared memory.**
