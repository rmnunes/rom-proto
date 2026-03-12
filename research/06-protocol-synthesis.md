# 06 - Protocol Design Synthesis

## Overview
Unifying all research into a coherent protocol design. This document synthesizes findings from existing protocols, video streaming, state synchronization, low-level networking, and paradigm-shifting concepts.

---

## The Problem Statement

Every existing protocol is **message-oriented**. Our protocol is **state-oriented**.

| Traditional | Our Protocol |
|-------------|-------------|
| Send message to address | Update named state |
| Request-response | Continuous streaming with backpressure |
| Server has the data | Network has the data (caching, replication) |
| Trust the channel (TLS) | Trust the data (signatures) |
| Full state transfers | Differential updates with CRDT merging |
| Fixed routing | Adaptive, proximity-weighted propagation |
| Dumb pipes | Smart forwarding (aggregation, filtering) |
| Role-based access | Capability-based access |
| Serialization overhead | Zero-copy wire format |
| All updates equal priority | Multi-resolution, interest-based filtering |

---

## Core Architecture

### Three Layers

```
+=========================================================+
|                    APPLICATION LAYER                      |
|  State declarations, mutations via proxy, subscriptions  |
|  Developer writes: state.user.name = "Alice"             |
|  System handles: capture, delta, propagate, persist      |
+=========================================================+
|                    PROTOCOL LAYER                         |
|  State-aware framing, CRDT merge, delta encoding,        |
|  version vectors, interest management, backpressure      |
+=========================================================+
|                    TRANSPORT LAYER                        |
|  UDP-based, tiered reliability, congestion control,      |
|  FEC, zero-copy I/O, connection management               |
+=========================================================+
```

---

## Wire Format

### Packet Structure
```
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Ver  | Flags |  Packet Type  |       Connection ID           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Packet Number                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Timestamp (us)                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Payload Length        |        Checksum               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Frames (variable)                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```
**Header: 16 bytes.** Multiple frames packed per packet.

### Frame Types

| Frame Type | Code | Purpose |
|-----------|------|---------|
| STATE_DECLARE | 0x01 | Declare named state region with schema + CRDT type |
| STATE_SNAPSHOT | 0x02 | Full state for initial sync or recovery |
| STATE_DELTA | 0x03 | Differential update (the "P-frame" of state) |
| STATE_SUBSCRIBE | 0x04 | Subscribe with predicate filter + resolution |
| STATE_UNSUBSCRIBE | 0x05 | Remove subscription |
| CREDIT | 0x06 | Backpressure credits (Reactive Streams style) |
| ACK | 0x07 | Selective acknowledgment with ranges |
| NACK | 0x08 | Negative acknowledgment (request retransmission) |
| CAPABILITY_GRANT | 0x09 | Grant access capability |
| CAPABILITY_REVOKE | 0x0A | Revoke capability |
| CLOCK_SYNC | 0x0B | Hybrid logical clock synchronization |
| PING / PONG | 0x0C/0D | Keepalive + RTT measurement |
| CONNECT / ACCEPT | 0x0E/0F | Connection establishment |
| CLOSE | 0x10 | Graceful shutdown |

### STATE_DELTA Frame (Most Common Frame)
```
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Frame Type=0x03 | Reliability |    State Path Hash            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Version Vector (compact)                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  CRDT Type  |  Merge Func   |     Delta Payload (Cap'n Proto) |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Author Signature                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### State Addressing
```
/domain/entity-type/entity-id/property#version@author

Examples:
  /app/users/alice/profile
  /app/documents/doc-123/content
  /game/world/chunk-3-7/entities
```
Hierarchical, versioned, with provenance. Wildcard subscriptions: `/app/users/*/status`

---

## Consistency Model

### Per-State-Region Consistency Levels
| Level | Mechanism | Latency | Use Case |
|-------|-----------|---------|----------|
| EVENTUAL | CRDTs, merge on arrival | Lowest | Collaborative editing, counters |
| CAUSAL | Vector clocks | Low | Chat, social feeds |
| SEQUENTIAL | Consensus (Raft-like) | Medium | Financial transactions |
| LOCAL | No replication | Zero | Scratch state |

### Version Vectors
Each state region maintains a version vector `{node_id: sequence}`. Enables:
- Causal ordering: detect concurrent updates
- Efficient sync: "send me everything after version V"
- Conflict detection: concurrent versions trigger CRDT merge

---

## Sync Protocol

### Initial Connection
```
Client                              Server
  |--- CONNECT ---------------------->|
  |<-- ACCEPT + capability tokens ----|
  |--- STATE_SUBSCRIBE (paths) ------>|
  |<-- STATE_SNAPSHOT (full state) ---|
  |<-- STATE_DELTA (live stream) -----|
  |--- STATE_DELTA (mutations) ------>|
  |    ... continuous bidirectional ...|
```

### Steady State
```
1. Client mutates local state (via proxy)
2. Proxy captures delta, assigns version, signs
3. Delta sent as STATE_DELTA frame
4. Server receives, CRDT-merges into canonical state
5. Server appends to event log (auto-persist)
6. Server computes per-subscriber filtered deltas
7. Server pushes STATE_DELTA to relevant subscribers
8. Subscribers CRDT-merge into local state
9. Local reactive system updates UI/consumers
```

### Recovery
- Client reconnects with last-known version vector
- Server computes diff since that version
- If diff is small: send deltas
- If diff is large or version too old: send snapshot + deltas since snapshot

---

## Reliability Model

### Inspired by Video Streaming
| Concept | Video | Our Protocol |
|---------|-------|-------------|
| I-frame | Full frame, self-contained | STATE_SNAPSHOT |
| P-frame | Delta from previous | STATE_DELTA |
| GOP interval | Keyframe frequency | Snapshot frequency |
| FEC | Proactive error correction | FEC for snapshots |
| NACK | Request retransmission | NACK for missed deltas |
| Jitter buffer | Smooth playback | Reorder buffer (optional) |
| SRT latency window | Bounded retransmission | Freshness deadline per delta |

### Freshness Deadlines
Each STATE_DELTA carries a freshness deadline. If it can't be delivered by the deadline:
- **Don't retransmit** -- the data is stale
- Wait for next delta or request a snapshot
- This is the "willing to lose" principle from video streaming

---

## Backpressure

### Credit-Based (RSocket-inspired)
```
Subscriber sends: CREDIT(n=100)  -- "I can handle 100 deltas"
Publisher sends at most 100 deltas, then waits for more credits.
```

### Adaptive Strategies When Overwhelmed
1. **Batch deltas**: Merge multiple pending deltas into one (CRDT merge)
2. **Reduce resolution**: Skip intermediate deltas, send only latest
3. **Sample**: Emit latest state at fixed intervals
4. **Priority shed**: Drop low-priority state updates first

---

## Security Model

### Self-Certifying State (NDN-inspired)
- Every STATE_DELTA and STATE_SNAPSHOT is signed by its author
- Verification is independent of transport path
- Data can be cached, relayed, or gossipped without losing trust

### Capability-Based Access (Cap'n Proto / E Language)
- CAPABILITY_GRANT frame issues unforgeable tokens
- Tokens specify: state path pattern, allowed operations (read/write/subscribe), expiry
- Tokens can be attenuated (e.g., read-only view) and delegated
- No ambient authority -- you can only access state you have a capability for

---

## Implementation Roadmap

### Phase 1: Foundation (C/C++)
- [ ] UDP socket layer with io_uring (Linux) / IOCP (Windows)
- [ ] Packet framing and parsing
- [ ] Connection management (CONNECT/ACCEPT/CLOSE)
- [ ] Basic reliability (ACK/NACK, selective retransmission)
- [ ] Congestion control (GCC-inspired, delay-based)

### Phase 2: State Layer
- [ ] State declaration and addressing
- [ ] CRDT library (LWW-Register, OR-Set, counters, sequences)
- [ ] Delta encoding and merging
- [ ] Version vectors
- [ ] Snapshot generation and delivery
- [ ] Event log (append-only persistence)

### Phase 3: Subscription & Sync
- [ ] Subscribe/unsubscribe with path patterns
- [ ] Interest management (per-subscriber delta filtering)
- [ ] Backpressure (credit-based)
- [ ] Freshness deadlines
- [ ] Recovery protocol (reconnect with version vector)

### Phase 4: Security
- [ ] State signing (Ed25519)
- [ ] Capability tokens
- [ ] DTLS integration for transport encryption

### Phase 5: Developer Experience
- [ ] Proxy-based change detection layer (C++ / language bindings)
- [ ] Reactive subscription API
- [ ] Language bindings (Rust, TypeScript/WASM, Python)
- [ ] CLI tools for inspection and debugging

### Phase 6: Advanced
- [ ] FEC for snapshots
- [ ] Multi-resolution propagation
- [ ] Adaptive routing
- [ ] In-transit computation (eBPF/Wasm)
- [ ] DPDK support for datacenter deployments

---

## Technology Stack

| Component | Choice | Rationale |
|-----------|--------|-----------|
| Core language | C/C++ | Maximum control, zero-overhead, cross-platform |
| Transport | UDP | Avoid TCP head-of-line blocking |
| Async I/O (Linux) | io_uring | No-syscall hot path |
| Async I/O (Windows) | IOCP + RIO | Native high-performance |
| Serialization | Cap'n Proto | Zero-copy, wire = memory format |
| CRDT library | Custom (C++) | Tight integration, minimal overhead |
| Inter-thread | Disruptor ring buffers | Lock-free, <50ns |
| Crypto | libsodium (Ed25519, ChaCha20) | Fast, audited, cross-platform |
| Build system | CMake | Cross-platform standard |

---

## Key Metrics to Target

| Metric | Target | Baseline (WebSocket) |
|--------|--------|---------------------|
| State propagation (LAN) | <1ms | 1-10ms |
| State propagation (same city) | <20ms | 20-100ms |
| Connection setup | <1 RTT | 3-4 RTT |
| Throughput (updates/sec) | >1M | ~100K |
| Memory per connection | <1KB | ~10KB |
| Wire overhead per delta | <16 bytes | >100 bytes (JSON+WS) |

---

## References (Complete)

### Protocol RFCs
- RFC 3550 (RTP), RFC 6455 (WebSocket), RFC 9000 (QUIC), RFC 9114 (HTTP/3)
- RFC 9221 (QUIC Datagrams), RFC 8445 (ICE), RFC 3711 (SRTP)
- RFC 5109 (ULPFEC), RFC 8627 (FlexFEC), RFC 8298 (SCReAM)
- RFC 6902 (JSON Patch), RFC 7386 (JSON Merge Patch)

### Foundational Papers
- Lamport, "Time, Clocks, and the Ordering of Events" (1978)
- Shapiro et al., "CRDTs" (2011, INRIA)
- Almeida et al., "Delta State CRDTs" (2018, JPDC)
- Kleppmann, "Conflict-Free Replicated JSON" (2017, IEEE TPDS)
- Kleppmann et al., "Local-first software" (2019, Ink & Switch)
- Hellerstein, "CALM Theorem" (2010)
- Yu & Vahdat, "Continuous Consistency" (2002, OSDI)
- Kreps, "The Log" (2013, LinkedIn)
- Fraser, "Differential Synchronization" (2009)
- McSherry et al., "Differential Dataflow" (CIDR 2013)
- Murray et al., "Naiad" (SOSP 2013)

### Architecture & Systems
- Jacobson et al., "Networking Named Content" (CoNEXT 2009)
- von Eicken et al., "Active Messages" (ISCA 1992)
- Gelernter, "Generative Communication in Linda" (ACM TOPLAS 1985)
- Dragojevic et al., "FaRM" (NSDI 2014)
- Bettner & Terrano, "1500 Archers on a 28.8" (GDC 2001)
- Ford, "Overwatch Gameplay Architecture" (GDC 2017)

### Libraries & Tools
- Automerge (automerge.org), Yjs (yjs.dev), Diamond Types
- Cap'n Proto (capnproto.org), FlatBuffers, SBE
- RSocket (rsocket.io), libsodium
- io_uring, DPDK, eBPF/XDP
