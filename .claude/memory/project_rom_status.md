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

## Test Suite
366 tests across 74 test suites, all passing (Windows MSVC, Linux GCC/Clang).

## Language Bindings
- **Rust:** `rmnunes-rom` + `rmnunes-rom-sys` (crates.io naming, git dependency for private use)
- **Python:** `rmnunes-rom` (cffi-based, context managers)
- **TypeScript/WASM:** `@rmnunes/rom` (Emscripten, GitHub Packages private)
- **SolidJS:** `useProtocoll` hook demo (each CRDT path → SolidJS signal)

## CI/CD
- GitHub Actions: ci.yml (build+test matrix), release.yml (semantic-release)
- Semantic versioning from conventional commits (fix→patch, feat→minor, feat!→major)
- npm publishes to GitHub Packages (private @rmnunes scope)
- Python/C++ artifacts attached to GitHub Releases

## Remaining Plan (Phases 7-16)
See plan file for: CLI tools, DTLS encryption, io_uring/IOCP native async, DPDK kernel bypass, in-transit computation/relay, full WASM binding completion.
