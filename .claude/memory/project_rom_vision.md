---
name: ROM Protocol Vision
description: Replicated Object Memory — state streaming protocol replacing request/response with continuous CRDT propagation across mesh networks.
type: project
---

ROM (Replicated Object Memory) is a fundamentally new communication protocol that replaces HTTP/gRPC request-response with continuous state streaming — like video frames where state changes flow instantly between nodes.

**Why:** Every existing protocol is message-oriented, not state-oriented. None natively understand structured, versioned, diffable application state. The vision: developers mutate state locally and ROM handles capture, delta propagation, CRDT merge, persistence, and subscription delivery automatically.

**How to apply:** All design decisions should favor continuous state flow over request/response patterns. Key principles:
- State not Messages
- Streams not Requests
- Content-Addressed, Self-Certifying (Ed25519 signed deltas)
- Merge not Overwrite (CRDTs: LWW, G-Counter, PN-Counter, OR-Set)
- Differential Propagation (multi-resolution tiers)
- Adaptive Hebbian routing (mesh self-organizes)

**Critical transport insight:** UDP is NOT the bottleneck — the kernel is (7-25us vs 0.5-1us wire). Protocol is transport-agnostic with modes: UDP, io_uring, IOCP, DPDK, DTLS, Loopback. Same state semantics, 1000x performance range.

**Research completed (2026-03-12):** 9 deep research agents across 2 rounds covering existing protocols, video streaming, state sync/CRDTs, low-level networking, kernel bypass, novel transports, and programmable hardware. Results in /research/ directory.
