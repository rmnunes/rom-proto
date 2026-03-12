# 03 - State Synchronization Technologies

## Overview
How to automatically detect, propagate, merge, and persist state changes across distributed nodes.

---

## 1. CRDTs (Conflict-free Replicated Data Types)

### Mathematical Foundation
Shapiro et al. (2011): If operations are **commutative**, **associative**, and **idempotent**, replicas converge regardless of operation order, without coordination.

### Two Formulations

**State-based (CvRDTs)**:
- Replicas exchange full state via `merge` function
- State forms a **join semi-lattice** (partial order with least upper bound)
- `merge(s1, s2)` = join of s1 and s2
- Properties: commutative, associative, idempotent

**Operation-based (CmRDTs)**:
- Replicas broadcast operations
- Operations must be commutative
- Requires reliable causal broadcast

### Key CRDT Types

| Type | Operations | Semantics |
|------|-----------|-----------|
| G-Counter | increment | Vector of per-replica counts, pointwise max merge |
| PN-Counter | increment, decrement | Two G-Counters (positive + negative) |
| G-Set | add | Union merge, grow only |
| OR-Set | add, remove | Each add gets unique tag; remove only observed tags |
| LWW-Register | assign | Keep highest-timestamp value |
| MV-Register | assign | Preserve all concurrent values |

### Sequence CRDTs (Collaborative Text)
- **YATA** (Yjs): Origin-based ordering, O(1) local operations
- **RGA**: Linked list with timestamps
- **Fugue** (2023): Tree-based, avoids interleaving anomalies
- **Peritext** (2022, Ink & Switch): Rich text with concurrent formatting

### Delta-state CRDTs (Almeida et al., 2015)
**Critical optimization**: Send only the delta-mutator (minimal state change), not full state. Deltas are themselves valid states in the lattice. Gives:
- Simplicity of state-based design (merge is still a join)
- Efficiency of operation-based design (small deltas)
- Deltas can be batched and merged before sending

### Libraries
| Library | Core Language | Key Feature |
|---------|--------------|-------------|
| Automerge | Rust (WASM bindings) | JSON-like CRDT document, branching/merging |
| Yjs | JavaScript (Rust port: yrs) | YATA algorithm, ~80 bytes/item, provider architecture |
| Diamond Types | Rust | Time DAGs, 10-100x faster than Yjs for some ops |

---

## 2. Operational Transformation (OT)

### How It Works (Ellis & Gibbs, 1989)
1. Each client has local document copy
2. Operations applied locally immediately (optimistic)
3. Operations sent to server
4. Remote operations **transformed** against local pending operations

### Transform Example
```
T(insert(3, 'a'), insert(1, 'b')) = insert(4, 'a')
// insert(1, 'b') shifted position, so insert(3, 'a') must adjust
```

### Google Docs / Jupiter Protocol
- Client-server (not P2P) -- simplifies to 2-way transform
- Server is single source of truth for ordering
- Each client transforms incoming server ops against local pending ops

### OT vs CRDTs
| Aspect | OT | CRDTs |
|--------|-----|-------|
| Server required | Yes (practical) | No (P2P possible) |
| Offline support | Limited | Excellent |
| Proven correctness | Many subtle bugs found | Mathematical proof |
| History | Can be garbage collected | Often needs history |

---

## 3. Differential Synchronization (Neil Fraser, 2009)

### Algorithm
```
Client maintains: client_text + client_shadow
Server maintains: server_text + server_shadow

Sync cycle:
1. Client: diff(client_shadow, client_text) -> patch
2. Client sends patch to server
3. Client: client_shadow = client_text
4. Server: apply patch to server_shadow, then to server_text
5. Server: diff(server_shadow, server_text) -> patch
6. Server sends patch to client
7. Repeat
```

Simple, uses standard diff/patch. Shadow copies act as common ancestor.

---

## 4. Event Sourcing

### Core Concept
Store the complete sequence of **events** (facts), not current state. Current state = replay of events.

```
Command -> Command Handler -> Event Store -> [Event1, Event2, ..., EventN]
                                                      |
                                                 Projection
                                                      |
                                                 Read Model (materialized view)
```

### Relevance to State Streaming
1. **Events ARE the stream**: The event log is inherently a stream of state changes
2. **Temporal queries**: Replay to any point in time
3. **Multi-model projections**: Different consumers build different views from same events
4. **Stream-table duality** (Jay Kreps, 2013): Every table is a changelog stream; every stream materializable as a table

### Snapshots
- Periodic snapshots prevent slow replay from beginning
- Kafka log compaction: keep only latest event per key
- **Snapshot + stream = our protocol's sync model**

---

## 5. Delta State Propagation

### Approaches
| Method | Use Case | Cost |
|--------|----------|------|
| JSON Patch (RFC 6902) | Structured data | Path-based operations |
| JSON Merge Patch (RFC 7386) | Simple overwrites | Send changed subset |
| Immer.js patches | JavaScript state | Proxy-captured diffs |
| Binary diff (VCDIFF, RFC 3284) | Raw bytes | Copy/add instructions |

### Proxy-Based Automatic Change Detection
- **Immer**: Wrap state in `produce()`, mutations captured as patches automatically
- **Valtio**: Vanilla JS objects become reactive via Proxy, generate patches
- **MobX**: Auto-tracks property access, changes trigger subscriptions

**Key for our protocol**: Developers write `state.count++` and the system captures, propagates, and persists the change automatically.

---

## 6. Reactive Programming Models

### The Observable Abstraction
```
Observable<T>: stream of values over time
  .pipe(
    scan(reducer, initial),      // Event sourcing as operator
    distinctUntilChanged(),       // Auto delta detection
    shareReplay(1),              // Cache latest, multicast
  )
  .subscribe(value => handle(value))
```

### Backpressure Strategies (critical for state streaming)
| Strategy | Behavior | When to Use |
|----------|----------|-------------|
| Buffer | Queue pending items | Risk OOM |
| Drop | Discard items consumer can't handle | Lossy acceptable |
| **Latest** | Keep only most recent | **State sync (usually best)** |
| Sample | Emit latest at fixed intervals | Rate limiting |

### Signals (Modern Reactive Primitives)
- **Pull-based with push invalidation**: Change invalidates dependents (push), recompute only when read (pull)
- Fine-grained: track at individual value level
- SolidJS, Angular Signals, Preact Signals, TC39 proposal

---

## 7. Real-World State Sync Systems

### Figma (Evan Wallace, 2019)
- Custom CRDT-inspired, server-authoritative
- LWW for properties, add-wins for object creation/deletion
- Changes as deltas: "set property X on object Y to value Z"
- Pragmatic: LWW is "good enough" for design tools

### Google Docs
- OT with Jupiter protocol, server canonical ordering
- Considering CRDT for newer products

### Multiplayer Games (Overwatch, Source Engine)
- Server-authoritative with client-side prediction
- State snapshots at 20-60 Hz with delta compression
- Entity interpolation: render slightly in the past for smoothness
- Dead reckoning / extrapolation for near-future prediction
- Interest management: only send state client can see
- **Overwatch ECS**: Components auto-detected for changes, auto-delta-compressed

### Firebase Realtime Database
- `onSnapshot()` = continuous state stream
- Never "fetch" -- subscribe and receive current + all future changes

### Electric SQL
- Transparent sync between PostgreSQL <-> SQLite (client-side)
- Developer just uses SQL; sync is invisible

---

## 8. The Synthesized Protocol Flow

```
1. Client connects -> receives snapshot (full CRDT state)
2. Client subscribes to paths of interest
3. Client mutates local state (via proxy, automatic op capture)
4. Client sends delta to server (batched, compressed)
5. Server merges delta into canonical state (CRDT merge)
6. Server persists to event log
7. Server computes per-subscriber deltas (filtered by subscriptions)
8. Server pushes deltas to all other subscribers
9. Subscribers merge deltas into local state (CRDT merge)
10. Local reactive system propagates changes to UI
```

**No explicit "save," no explicit "fetch," no explicit "send." State just flows.**

---

## Key Academic References

- Shapiro et al., "A comprehensive study of CRDTs" (2011, INRIA RR-7506)
- Kleppmann & Beresford, "A Conflict-Free Replicated JSON Datatype" (2017, IEEE TPDS)
- Almeida, Shoker & Baquero, "Delta State Replicated Data Types" (2018, JPDC)
- Fraser, "Differential Synchronization" (2009, DocEng)
- Kreps, "The Log" (2013, LinkedIn Engineering)
- Yu & Vahdat, "Continuous Consistency Model" (2002, OSDI)
- Bailis et al., "Quantifying Eventual Consistency with PBS" (2014, VLDB Journal)
- Lamport, "Time, Clocks, and the Ordering of Events" (1978, CACM)
- **CALM Theorem** (Hellerstein, 2010): Monotonic programs = coordination-free consistency. CRDTs embody this.
- Kleppmann et al., "Local-first software" (2019, Ink & Switch)
