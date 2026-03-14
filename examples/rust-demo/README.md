# ROM Rust Demo — CRDT State Streaming

Two peers connected over loopback, streaming CRDT state updates.
No HTTP. No REST. No JSON. Just continuous state with automatic merge.

## Prerequisites

- Rust 1.70+
- C compiler (the native library is built from source via the `-sys` crate)

## Run

```bash
cargo run
```

## What it demonstrates

- **LWW Register**: last-write-wins text field synced between peers
- **G-Counter**: grow-only counter that converges across peers
- **Ed25519 signing**: every delta is cryptographically signed
- **Bidirectional streaming**: both peers can push state to each other
- **RAII cleanup**: Rust's `Drop` trait handles resource management
