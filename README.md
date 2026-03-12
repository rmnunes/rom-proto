# ROM

**Replicated Object Memory** — a protocol for streaming CRDT state across mesh networks with adaptive routing and multi-resolution propagation.

ROM treats shared state as the primitive. You declare what you want to share, and ROM handles replication, conflict resolution, verification, and routing — across any number of peers, at any distance, at any fidelity.

```cpp
auto keys = KeyPair::generate();
Peer peer(node_id, transport, keys);

peer.declare("/game/player/1/pos", CrdtType::LWW_REGISTER);
peer.set_lww("/game/player/1/pos", data, len);

peer.flush();   // sign, encode, send
peer.poll();    // receive, verify, decode, merge
```

Every delta is Ed25519-signed at the source. Every delta is verified at the destination. Trust the data, not the channel.

---

## Why ROM

Most networking libraries make you think about connections, serialization, and message ordering. ROM makes you think about *state*.

You declare paths in a shared state tree. Each path holds a CRDT — a data structure that can be modified concurrently by any peer and always converges to the same result. ROM signs each mutation at the source, replicates it through the mesh, verifies it at each hop, and merges it into the local state. No central server. No conflict resolution logic. No manual serialization.

**What makes ROM different from other CRDT libraries:**

- **Multi-resolution propagation** — nearby peers get full-fidelity updates at 60fps. Distant peers get rate-limited or threshold-filtered summaries. Same data, different resolution, zero application logic.
- **Adaptive Hebbian routing** — routes strengthen when deliveries succeed and weaken when they fail. The mesh learns the fastest paths organically, like synapses in a neural network.
- **Signed deltas** — every state mutation carries an Ed25519 signature from the authoring node. Verification happens at the data level, not the transport level. A delta is trustworthy regardless of how it arrived.
- **In-transit computation** — relay nodes can merge deltas before forwarding, reducing bandwidth. Ten position updates from ten players become one merged update downstream.

---

## Architecture

```
Application
    |
    v
  Peer  -----> StateRegistry (CRDTs + version vectors)
    |   -----> Router (Hebbian adaptive routing)
    |   -----> ConnectionManager (multi-peer mesh)
    |   -----> DeltaFilter (per-connection resolution)
    |   -----> CapabilityStore (access control tokens)
    v
Transport (UDP | DTLS | io_uring | IOCP | DPDK | Loopback)
    |
    v
  Wire (packets → frames → signed deltas)
```

### State Layer

| CRDT | Use case | Merge strategy |
|------|----------|---------------|
| LWW Register | Player position, config values | Last-writer-wins by timestamp |
| G-Counter | Scores, click counts | Sum of per-node increments |
| PN-Counter | Resource pools, bidirectional meters | Positive + negative counters |
| OR-Set | Inventory, player lists | Observed-remove semantics |

State is organized as a path tree (`/game/player/1/pos`, `/chat/room/42/messages`). Wildcards in subscriptions (`/game/**`) match subtrees.

### Transport Layer

| Transport | Platform | Use case |
|-----------|----------|----------|
| Loopback | All | Testing, in-process |
| UDP | All | Standard networking |
| DTLS | All | Encrypted UDP (mbedTLS 3.x) |
| io_uring | Linux 5.1+ | High-throughput async I/O |
| IOCP | Windows | Native async I/O |
| DPDK | Linux | Kernel-bypass for datacenter |

All transports implement the same interface. Swap them without changing application code.

### Security

- **Ed25519 signatures** on every delta frame (Monocypher)
- **Capability tokens** for fine-grained access control (path + permission + expiry)
- **DTLS 1.2** for optional channel encryption
- Signatures are verified even through encrypted channels — defense in depth

### Wire Format

```
Packet: [header 12B] [frame₁] [frame₂] ... [CRC32 4B]
Frame:  [type 1B] [length 2B] [payload ...]
Delta:  [path_hash 4B] [crdt_type 1B] [reliability 1B]
        [author_node_id 2B] [data ...] [ed25519_sig 64B]
```

Compact binary format. No schema negotiation. Every frame self-describes.

---

## Multi-Resolution Propagation

The core research differentiator. Each connection can have a different resolution tier:

```cpp
// Nearby player: full updates, every frame
peer.set_connection_resolution(nearby_node, ResolutionTier::FULL);

// Same-region player: rate-limited to 10 updates/sec
peer.set_connection_resolution(region_node, ResolutionTier::NORMAL);

// Far-away player: only significant changes (> threshold)
peer.set_connection_resolution(distant_node, ResolutionTier::COARSE);

// Spectator: version vectors only, pull on demand
peer.set_connection_resolution(spectator_node, ResolutionTier::METADATA);
```

The `DeltaFilter` sits in the `flush()` path and applies per-connection filtering. The application writes state once; ROM decides what each peer needs to see.

---

## Adaptive Routing

ROM's router uses Hebbian learning — "neurons that fire together wire together."

When a delta is successfully delivered through a node, that route's weight increases. When delivery fails, the weight decreases. Over time, the mesh discovers optimal paths organically.

```cpp
// Routes are learned automatically from ROUTE_ANNOUNCE frames
// and refined through delivery feedback

auto hops = peer.router().select_next_hops(path_hash);
// Returns the highest-weighted routes for this path

peer.router().tick(); // Decay unused routes periodically
```

This enables multi-hop state propagation without explicit topology configuration. Peers connect, announce their paths, and the mesh self-organizes.

---

## Language Bindings

### C API

The stable FFI surface. Every binding is built on top of this.

```c
PcolKeyPair kp;
pcol_generate_keypair(&kp);

PcolTransport* t = pcol_transport_udp_create();
pcol_transport_bind(t, (PcolEndpoint){"0.0.0.0", 9000});

PcolPeer* peer = pcol_peer_create(1, t, &kp);
pcol_declare(peer, "/game/pos", PCOL_LWW_REGISTER, PCOL_RELIABLE);
pcol_set_lww(peer, "/game/pos", data, len);
pcol_flush(peer);
```

40+ functions covering transport, peer lifecycle, state operations, multi-connection mesh, resolution tiers, routing, subscriptions, and access control.

### Rust

```rust
use rom::{Peer, Transport, KeyPair, CrdtType};

let keys = KeyPair::generate();
let transport = Transport::udp()?;
transport.bind("0.0.0.0", 9000)?;

let peer = Peer::new(1, transport, &keys);
peer.declare("/game/pos", CrdtType::LwwRegister, Default::default())?;
peer.set_lww("/game/pos", b"x:100,y:200")?;
peer.flush()?;
```

Safe RAII wrapper — `Peer` calls `pcol_peer_destroy` on `Drop`.

### Python

```python
from rom import Peer, Transport, CrdtType

keys = Peer.generate_keypair()
transport = Transport.udp()
transport.bind("0.0.0.0", 9000)

with Peer(1, transport, keys) as peer:
    peer.declare("/game/pos", CrdtType.LWW_REGISTER)
    peer.set_lww("/game/pos", b"x:100,y:200")
    peer.flush()
```

Context manager for automatic cleanup. cffi-based, no compilation needed at install time.

### TypeScript / WASM

```typescript
import { initProtocoll, CrdtType } from '@rom/wasm';

const rom = await initProtocoll();
const keys = rom.generateKeyPair();
const transport = rom.createLoopbackTransport();
transport.bind("loopback", 1);

const peer = rom.createPeer(1, transport, keys);
peer.declare("/game/pos", CrdtType.LWW_REGISTER);
peer.setLww("/game/pos", new TextEncoder().encode("x:100,y:200"));
peer.flush();
```

Compiled to WASM via Emscripten. Runs in browsers and Node.js.

### SolidJS Integration

```tsx
import { useProtocoll } from './useProtocoll';

function GameHUD() {
  const { lww, counter, flush } = useProtocoll({ nodeId: 1, protocoll });

  const [playerName, setPlayerName] = lww("/game/player/1/name");
  const [score, addScore] = counter("/game/score");

  setInterval(() => flush(), 50);

  return (
    <div>
      <input value={playerName()} onInput={e => setPlayerName(e.target.value)} />
      <button onClick={() => addScore(10)}>Score: {score()}</button>
    </div>
  );
}
```

Each CRDT path becomes a SolidJS signal. State changes from remote peers update the UI reactively.

---

## Building

### Requirements

- CMake 3.20+
- C++20 compiler (MSVC 19.30+, GCC 12+, Clang 14+)
- Dependencies fetched automatically via CMake FetchContent

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Run Tests

```bash
./build/Release/rom_tests    # Windows
./build/rom_tests             # Linux/macOS
```

366 tests across 74 test suites.

### Optional Features

```bash
cmake -B build \
  -DPROTOCOLL_ENABLE_DTLS=ON      # DTLS encryption (fetches mbedTLS 3.6)
  -DPROTOCOLL_ENABLE_IOURING=ON   # io_uring transport (Linux only)
  -DPROTOCOLL_ENABLE_DPDK=ON      # DPDK kernel bypass (Linux only)
```

All optional dependencies are off by default. The core library has zero external dependencies beyond Monocypher (Ed25519, fetched automatically).

### CLI Tools

```bash
rom_dump --port 9000          # Decode packets on the wire (like tcpdump for ROM)
rom_inspect --connect 1.2.3.4:9000  # Inspect remote peer state tree
```

---

## Project Structure

```
include/protocoll/
  peer.h                    # High-level Peer API
  protocoll.h               # C API (stable FFI surface)
  connection/               # Handshake, ConnectionManager
  reliability/              # ACK/NACK, congestion, FEC
  routing/                  # Hebbian Router, RouteTable
  relay/                    # RelayNode, Aggregator
  security/                 # Ed25519, capability tokens
  state/                    # CRDTs, subscriptions, resolution
  transport/                # UDP, DTLS, io_uring, IOCP, DPDK
  wire/                     # Packet/frame codec

bindings/
  rust/                     # rom-sys (FFI) + rom (safe wrapper)
  python/                   # cffi-based Python package
  wasm/                     # Emscripten WASM + TypeScript

examples/
  solidjs-demo/             # SolidJS + ROM reactive state demo
  state_streaming_demo.cpp  # C++ two-peer state sync example

tools/
  protocoll_dump.cpp        # Packet capture/decode
  protocoll_inspect.cpp     # Remote state tree inspector

tests/                      # 366 tests across 74 suites
```

---

## Design Principles

1. **Trust the data, not the channel.** Signatures travel with every delta. Verification at the data layer means you can relay through untrusted nodes, cache aggressively, and encrypt optionally.

2. **State is the API.** Applications declare paths and CRDTs. ROM handles replication, ordering, conflict resolution, and delivery. No message types, no serialization schemas, no request/response patterns.

3. **Resolution is a first-class concept.** Not every peer needs every update at full fidelity. Multi-resolution propagation is built into the core, not bolted on as a filter.

4. **The mesh learns.** Routes aren't configured — they're discovered and refined through Hebbian feedback. Good paths get stronger. Failed paths get weaker. The network adapts.

5. **Zero mandatory dependencies.** The core compiles with just a C++20 compiler. Monocypher (Ed25519) is fetched by CMake. Every optional feature (DTLS, io_uring, DPDK) is gated behind a CMake flag that defaults to OFF.

---

## License

MIT

---

*ROM: Replicated Object Memory*
