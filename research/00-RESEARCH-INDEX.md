# Protocol Research Index

## Vision
Create a fundamentally new communication protocol where state changes flow continuously between nodes -- like video frames -- eliminating the request/response paradigm entirely. The frontend doesn't request state; it *observes* it. The backend doesn't wait for requests; it *streams* reality. Everything auto-persists as it flows.

## Research Documents

| # | Document | Focus Area |
|---|----------|------------|
| 01 | [Existing Protocols](01-existing-protocols.md) | HTTP, WebSocket, gRPC, WebRTC, QUIC, MQTT, SSE -- architectures and why they fall short |
| 02 | [Video Streaming Internals](02-video-streaming.md) | RTP, SRT, WebRTC pipeline, frame encoding, FEC, congestion control |
| 03 | [State Synchronization](03-state-sync.md) | CRDTs, OT, differential sync, event sourcing, reactive models |
| 04 | [Low-Level C/C++ Networking](04-low-level-networking.md) | Raw sockets, io_uring, DPDK, zero-copy, lock-free structures, serialization |
| 05 | [Paradigm-Shifting Concepts](05-paradigm-shifts.md) | Dataflow, tuple spaces, NDN, distributed shared memory, biological inspiration |
| 06 | [Protocol Design Synthesis](06-protocol-synthesis.md) | Unified design principles, architecture, wire format, and roadmap |
| 07 | [Beyond UDP](07-beyond-udp.md) | Transport layer innovation: kernel bypass, RDMA, CXL, novel transports, programmable hardware |

## Key Insights

> "Real-time is not about going fast, it's about being willing to lose." Traditional protocols promise every byte, in order, eventually. Our protocol promises the most useful state, right now.

> "UDP is not the constraint -- the kernel is. And for controlled environments, even the kernel is unnecessary." Our protocol should be transport-agnostic across a 1000x performance range.

## The Universal Gap
Every existing protocol is **message-oriented**, not **state-oriented**. None natively understand structured, versioned, diffable application state. Our protocol fills this gap.

## The Transport Revolution
Our protocol operates at **six transport levels** -- from CXL shared memory (~200ns) to browser WebTransport (~100ms) -- with identical state-streaming semantics. The transport is negotiated, not fixed.
