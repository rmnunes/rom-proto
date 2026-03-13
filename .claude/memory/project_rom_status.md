---
name: ROM Implementation Status
description: Current state of the ROM codebase — completed phases, test counts, language bindings, CI/CD setup.
type: project
---

## Completed (Phases 1-6)
- Wire format (packets, frames, CRC32)
- Transport abstraction (Loopback, UDP, AsyncTransport with thread pool)
- Connection handshake (CONNECT/ACCEPT/ESTABLISHED)
- Reliability (ACK/NACK/retransmit, congestion control GCC-inspired, FEC)
- CRDTs (LWW Register, G-Counter, PN-Counter, OR-Set)
- Mandatory Ed25519 signing on every delta (Monocypher)
- Capability tokens (path + permission + expiry)
- Reactive StateObserver
- C API (40+ functions, stable FFI surface)
- Multi-resolution propagation (FULL/NORMAL/COARSE/METADATA via DeltaFilter)
- Adaptive Hebbian routing + ConnectionManager for mesh topologies
- CLI tools (rom_dump, rom_inspect)

## Phase 17: Browser Transports (completed 2026-03-12)
- ExternalTransport (C++ queue-based transport bridging JS ↔ WASM)
- Non-blocking handshake (connect_start/poll, accept_start/poll for single-threaded WASM)
- BrowserTransport base class (TypeScript, drains send queue, pushes recv)
- WebTransportTransport (default/recommended — QUIC unreliable datagrams)
- WebSocketTransport (compatibility fallback — logs console warning recommending WebTransport)
- Async connect/accept + startPolling/stopPolling convenience on Peer class

## Test Suite
388 tests across 78 test suites, all passing (Windows MSVC, Linux GCC/Clang).

## Language Bindings
- **Rust:** `rmnunes-rom` + `rmnunes-rom-sys` (crates.io naming, git dependency for private use)
- **Python:** `rmnunes-rom` (cffi-based, context managers)
- **TypeScript/WASM:** `@rmnunes/rom` (Emscripten, GitHub Packages private)
  - Browser transports: WebTransportTransport (default), WebSocketTransport (fallback)
- **SolidJS:** `useProtocoll` hook demo (each CRDT path → SolidJS signal)

## CI/CD
- GitHub Actions: ci.yml (build+test matrix + semantic-release + publish)
- Semantic versioning from conventional commits (fix→patch, feat→minor, feat!→major)
- npm publishes to GitHub Packages (private @rmnunes scope)
- Python/C++ artifacts attached to GitHub Releases
