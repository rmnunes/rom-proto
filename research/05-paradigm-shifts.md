# 05 - Paradigm-Shifting Concepts for Protocol Design

## Overview
Radical ideas from dataflow programming, distributed systems, biology, and networking research that inform a protocol designed from scratch for continuous state synchronization.

---

## 1. Dataflow Programming

### Core Inversion
Programs are directed graphs. Nodes = operations, edges = data pathways. Data drives execution, not a program counter.

### Key Systems
- **Lucid (1974)**: Variables are infinite temporal streams. `x fby x + 1` generates natural numbers. Time is first-class.
- **Lustre (1984)**: Synchronous dataflow for safety-critical systems (Airbus SCADE). Clock-synchronized, formally verifiable.
- **Signal (1986)**: Multi-clock semantics. Different system parts at different rates.

### Protocol Implications
- Messages are not requests/responses -- they are **tokens flowing through a computation graph**
- Implicit parallelism: independent nodes fire concurrently
- **Demand-driven dataflow**: Downstream pulls only when needed = natural backpressure

---

## 2. Reactive Streams Specification

### The Four Interfaces
- `Publisher<T>`, `Subscriber<T>`, `Subscription` (carries `request(n)`), `Processor<T,R>`

### Core Innovation: Wire-Level Backpressure
Subscriber calls `request(n)` to grant credits. Publisher sends at most n elements. **Pull-push hybrid.**

### RSocket: Reactive Streams as Network Protocol
- Binary protocol over TCP/WebSocket/Aeron
- Four models: Fire-and-Forget, Request-Response, Request-Stream, Request-Channel
- **REQUEST_N frame**: Backpressure at the wire level
- Resumability, multiplexing, lease mechanism
- **Insight**: Request-response is just a stream of length 1

---

## 3. Tuple Spaces (Linda Model, Gelernter 1985)

### Operations
- `out(t)`: Place tuple into shared space
- `in(p)`: Atomically find and remove matching tuple (blocks if none)
- `rd(p)`: Read without removing (blocks if none)
- `eval(t)`: Place "live" tuple that computes concurrently

### Radical Properties
1. **Spatial decoupling**: Sender and receiver don't know each other
2. **Temporal decoupling**: Tuples persist after producer exits
3. **Associative access**: Match by content pattern, not address
4. **Atomic coordination**: `in()` provides natural mutual exclusion

### Gelernter's Insight
> "The network is not a wire between computers. It is a shared medium, like a blackboard. Anyone can write, anyone can read, and writing does not require knowing who will read."

### Protocol Implication
Replace addressing with matching. You don't send to IP:port -- you publish state. Consumers declare interest patterns.

---

## 4. Content-Centric Networking (NDN)

### The Fundamental Inversion (Van Jacobson, 2006)
The Internet routes to **locations** (IP addresses). NDN routes to **content** (names).

### Two Packet Types
1. **Interest**: "I want `/ndn/weather/today`" (carries name, not destination)
2. **Data**: Response with name + content + cryptographic signature

### Three Router Data Structures
1. **Content Store (CS)**: Cache. Every router is a cache.
2. **Pending Interest Table (PIT)**: Tracks unsatisfied Interests. Duplicate Interests collapsed = **natural multicast**.
3. **Forwarding Information Base (FIB)**: Name prefix -> interface mapping

### Radical Properties
- **No addresses**: Names are the fundamental primitive
- **Ubiquitous caching**: CDN built into the network layer
- **Data-centric security**: Sign the data, not the channel. Eliminates MITM.
- **Trivial mobility**: Re-issue Interest from new location

### Key Lesson
**Self-certifying data**: Each state update is signed by its author. Any node can verify regardless of path (push, cache, relay, gossip). Eliminates need for secure channels between every pair.

---

## 5. Distributed Shared Memory

### RDMA (Remote Direct Memory Access)
- Read/write remote memory **without involving remote CPU or OS**
- Latency: **~1 microsecond**
- Operations: `RDMA READ`, `RDMA WRITE`, `RDMA ATOMIC` (CAS, fetch-and-add)
- Used by FaRM (Microsoft): 140M key-value ops/sec on 90 machines

### The Abstraction We Want
Even if we can't literally share memory across the internet, the **developer experience** should feel like shared memory: "both sides see the same state, changes automatically visible." CRDTs + automatic sync provide this at a higher level.

### One-Sided Operations
Remote side doesn't need to be actively involved. Eliminates request-response entirely.

---

## 6. Biological Neural Network Inspiration

### Key Properties for Protocol Design

1. **Threshold-based firing**: Accumulate signals, act only when significance threshold crossed. Reduces processing overhead.

2. **Inhibition as first-class primitive**: ~50% of neural communication is inhibitory. Protocol analog: **negative signals that counteract pending positive ones**.

3. **Hebbian learning** ("fire together, wire together"): Connections strengthen with use. Protocol: **adaptive routing that strengthens useful paths**.

4. **Chemical gradients**: State changes spread outward with decreasing urgency. Protocol: **proximity-weighted propagation**.

5. **Neurotransmitter diversity**: Different transmitters = different effects. Protocol: **meta-messages that change receiver's processing mode**.

6. **Sparse coding**: Only 1-5% of neurons active at any time. Protocol: **sparse state updates, minimal encoding**.

7. **Dendritic computation**: Local computation before signals reach cell body. Protocol: **edge computation -- intermediate nodes transform state in transit**.

---

## 7. Plan 9's 9P Protocol

### Philosophy
Everything is a file. Every resource accessed through filesystem interface.

### 9P Design
~14 message types total. Tag-based multiplexing. Fid-based references. Nearly stateless.

### Protocol Implications
- **Uniform interface**: State sync as virtual filesystem: `/state/player/1/position`
- **Namespaces as isolation**: Different clients get different views
- **Composability via mounting**: Complex state from multiple sources unified
- **Extreme simplicity**: Low overhead, simple parsing

---

## 8. Cap'n Proto RPC

### Zero-Copy: Wire Format IS Memory Format
No serialization step. Received message directly usable as in-memory data structure.

### Promise Pipelining (Time-Travel RPC)
```
promise1 = server.getUser(id)
promise2 = promise1.getProfile()   // sent immediately, referencing promise1
promise3 = promise2.getAvatar()    // sent immediately, referencing promise2
// ALL sent in ONE batch. ONE round trip total.
```

### Capability-Based Security (OCaps)
- Object reference IS a capability
- Unforgeable, attenuable, delegatable
- Three-party handoff: A gives B's capability to C; C talks directly to B

---

## 9. Active Messages (von Eicken et al., 1992)

### Concept
Messages carry handler address -- code to execute on arrival. Eliminates receive-side dispatch.

### Modern Echoes
- **eBPF**: Programs on network packets, in kernel
- **P4**: Programs on switches
- **SmartNICs**: Programmable processors on arriving messages
- **Wasm as message payload**: Emerging research

### Protocol Implications
- Messages carry merge functions: "here is how to merge this with your current state"
- Protocol extensible by sending new handlers
- In-network computation: routers aggregate/filter/transform
- Security via sandboxing (Wasm, eBPF verifier)

---

## 10. Differential Dataflow (McSherry, MSR)

### Core Idea
Extend dataflow with "differences" -- only compute and transmit how output changes as input changes. Uses partially ordered timestamps.

### Naiad (SOSP 2013)
Timely dataflow implementing differential dataflow. Low-latency iterative computation.

### Materialize
Production system based on differential dataflow. Subscribe to a view that is continuously updated. The system maintains the view.

### This Is Our Closest Academic Ancestor
Differential dataflow is the most efficient known approach to maintaining synchronized state across nodes.

---

## 11. Key Research Projects

| Project | Institution | Relevance |
|---------|------------|-----------|
| Diamond (2016) | CMU | Reactive data structures with automatic cross-device sync |
| XIA | CMU | Multiple communication types simultaneously, extensible |
| SCION | ETH Zurich | Secure multi-path Internet, path-aware networking |
| Lasp (2015) | - | Programming model based entirely on CRDTs |
| MobilityFirst | Rutgers | GUID-based routing, identity/location separation |

---

## 12. Synthesis: Ten Foundational Principles

| # | Principle | Inspiration |
|---|-----------|-------------|
| 1 | **State, Not Messages** | DSM, Tuple Spaces |
| 2 | **Streams, Not Requests** | Dataflow, Reactive Streams |
| 3 | **Content-Addressed** | NDN |
| 4 | **Self-Certifying State** | NDN, OCaps |
| 5 | **Merge, Not Overwrite** | CRDTs |
| 6 | **Differential Propagation** | Differential Dataflow |
| 7 | **Multi-Resolution** | Neural gradients |
| 8 | **Adaptive Routing** | Hebbian learning |
| 9 | **Computation in Transit** | Active Messages, P4/SDN |
| 10 | **Capability-Based Access** | Cap'n Proto, E Language |

### The Paradigm in One Sentence
> The network is a distributed, content-addressed, CRDT-backed state store with adaptive differential synchronization, capability-based security, and computation at every node.
