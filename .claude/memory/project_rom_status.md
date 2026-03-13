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
- C API (48+ functions, stable FFI surface)
- Multi-resolution propagation (FULL/NORMAL/COARSE/METADATA via DeltaFilter)
- Adaptive Hebbian routing + ConnectionManager for mesh topologies
- CLI tools (rom_dump, rom_inspect)

## Phase 17: Browser Transports (completed 2026-03-12)
- ExternalTransport (C++ queue-based transport bridging JS <> WASM)
- Non-blocking handshake (connect_start/poll, accept_start/poll for single-threaded WASM)
- BrowserTransport base class (TypeScript, drains send queue, pushes recv)
- WebTransportTransport (default/recommended — QUIC unreliable datagrams)
- WebSocketTransport (compatibility fallback — logs console warning recommending WebTransport)
- Async connect/accept + startPolling/stopPolling convenience on Peer class
- SolidJS demo updated to use real @rmnunes/rom imports (loopback + network code examples)
- CI fix: added missing EXPORTED_FUNCTIONS (_pcol_subscribe_with_resolution, _pcol_api_version)

## Phase 18: .NET Bindings (completed 2026-03-12)
- `RMNunes.Rom` NuGet package, net8.0, [LibraryImport] source generator
- Two-layer: Interop/ (raw P/Invoke) + public API (KeyPair, Transport, Peer)
- SafeHandle for opaque pointers (PcolPeer*, PcolTransport*), IDisposable pattern
- PcolEndpoint struct marshalled with manual string allocation (MakeEndpoint/FreeEndpoint)
- PROTOCOLL_LIB_PATH env var for custom native lib location
- Callbacks via [UnmanagedCallersOnly] + GCHandle pinning
- 21 xUnit tests (keypair, transport, peer CRDT ops, handshake, state exchange, counter convergence)
- CMakeLists.txt: BUILD_SHARED_LIBS support with PROTOCOLL_BUILDING_DLL + OUTPUT_NAME protocoll
- CI: dotnet job + NuGet publish to GitHub Packages

## Test Suite
388 C++ tests + 21 .NET tests, all passing.

## Language Bindings
- **Rust:** `rmnunes-rom` + `rmnunes-rom-sys` (crates.io naming, git dependency for private use)
- **Python:** `rmnunes-rom` (cffi-based, context managers)
- **TypeScript/WASM:** `@rmnunes/rom` (Emscripten, GitHub Packages private)
  - Browser transports: WebTransportTransport (default), WebSocketTransport (fallback)
- **SolidJS:** `useProtocoll` hook demo (each CRDT path -> SolidJS signal, supports loopback + network modes)
- **.NET:** `RMNunes.Rom` ([LibraryImport], SafeHandle, net8.0, 21 tests passing)

## CI/CD
- GitHub Actions: ci.yml (build+test matrix + semantic-release + publish)
- Semantic versioning from conventional commits (fix->patch, feat->minor, feat!->major)
- npm publishes to GitHub Packages (private @rmnunes scope)
- Python/C++ artifacts attached to GitHub Releases
- NuGet publishes to GitHub Packages (RMNunes.Rom)

## Key Decisions
- WebTransport is the default/recommended browser transport; WebSocket is compatibility fallback only
- No WebRTC transport — WebTransport covers the use case better
- .NET bindings use [LibraryImport] (not [DllImport]) for AOT compatibility
- Single NuGet package with runtime natives (not split per-platform)
