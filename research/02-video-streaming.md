# 02 - Video Streaming Protocol Internals

## Overview
How real-time video achieves the perception of "instant" feedback, and what we can learn for state streaming.

---

## 1. RTP (Real-time Transport Protocol) -- RFC 3550

### Packet Header (12 bytes minimum)
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       Sequence Number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           Timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             SSRC                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

Key fields:
- **Sequence Number (16 bits)**: Detect loss and reorder
- **Timestamp (32 bits)**: Media clock (90kHz for video). NOT wall-clock time.
- **SSRC (32 bits)**: Uniquely identifies stream within session
- **Marker bit**: Application-defined, typically marks frame boundaries

### Why UDP, Not TCP
1. **Head-of-line blocking**: TCP retransmission timeout ~200ms = 6 frames stall at 30fps
2. **Congestion backoff**: TCP halves send rate on loss -- video can't do that instantly
3. **No partial delivery**: TCP won't deliver 99% of a frame; RTP/UDP lets app decide
4. **Stale data**: A 200ms-old retransmission is less useful than current data

---

## 2. SRT (Secure Reliable Transport)

### The Latency Window Concept
1. Sender and receiver agree on latency value (e.g., 120ms) at connection time
2. Sender keeps buffer of recently sent packets for the window duration
3. Receiver detects losses, sends NAK (negative acknowledgment)
4. Sender retransmits if packet is still within the window
5. **If packet can't be recovered in time, it's dropped**

### Latency Numbers
| Network | SRT Configured Latency | Rule of Thumb |
|---------|----------------------|---------------|
| LAN | 20-60ms | |
| Same continent | 120-250ms | ~4x RTT |
| Intercontinental | 500ms-2s | |

---

## 3. WebRTC Capture-to-Display Pipeline

```
[Camera Capture]      ~0-33ms   (frame exposure + readout)
       |
[Color Convert/Scale] ~1-2ms    (GPU-accelerated)
       |
[Encode]              ~3-30ms   (HW: 3-8ms, SW: 10-30ms)
       |
[Packetize (RTP)]     ~0.1ms
       |
[PACE/Send]           ~0-5ms    (pacer smooths burst)
       |
[Network Transit]     ~5-150ms  (LAN=1ms, city=5-20ms, continent=30-80ms)
       |
[Receive/Depacketize] ~0.1ms
       |
[Jitter Buffer]       ~10-80ms  (adaptive)
       |
[Decode]              ~3-15ms   (HW: 2-5ms, SW: 5-15ms)
       |
[Render]              ~0-16ms   (vsync)
       |
[Display]             ~2-20ms   (pixel response)
```

### Total Latency
| Scenario | Latency |
|----------|---------|
| LAN | 50-100ms |
| Same city | 80-200ms |
| Cross-continent | 200-500ms |

---

## 4. Frame Types (I/P/B) -- The Delta Encoding Model

### I-frames (Keyframes)
- Self-contained, no references to other frames
- Spatial compression only
- 5-20x larger than P-frames
- Sync points -- decoder can start from any I-frame
- Sent every 1-10 seconds (GOP interval)

### P-frames (Predicted)
- Reference previous frames (forward prediction)
- Encode only **differences** (motion vectors + residual)
- 30-50% the size of I-frames
- Error propagation: if reference lost, all subsequent P-frames undecodable

### B-frames (Bi-directional)
- Reference past AND future frames
- Smallest: 10-25% of I-frame
- **Add encoder latency** (must buffer future frames)
- **Disabled in real-time** -- WebRTC uses only I + P frames

### Direct Mapping to State Protocol
| Video Concept | State Protocol Equivalent |
|--------------|--------------------------|
| I-frame | Full state snapshot |
| P-frame | State delta/diff |
| B-frame | N/A (adds latency, skip) |
| GOP interval | Snapshot frequency |
| Frame sequence number | State version number |
| Codec parameters | State schema |

---

## 5. Forward Error Correction (FEC)

### XOR-based FEC (ULPFEC, RFC 5109)
- XOR N data packets to produce 1 FEC packet
- Recovers any single packet loss within group
- Overhead: 1/N (e.g., 10% for 1:10 ratio)
- **Zero recovery latency** (vs NACK which costs 1 RTT)

### FlexFEC (RFC 8627)
- 2-D FEC: row + column parity packets
- Recovers from burst losses that defeat simple XOR
- Used in WebRTC

### Tradeoff: FEC vs NACK
- **FEC**: Zero latency recovery, constant bandwidth cost
- **NACK**: 1 RTT recovery latency, zero bandwidth cost until loss
- **Optimal strategy**: FEC for critical data (I-frames/snapshots), NACK for deltas

---

## 6. Congestion Control for Real-Time

### Google Congestion Control (GCC)
1. **Delay-based estimator**: Monitors inter-arrival time deltas
2. **Loss-based estimator**: Reduces bandwidth if loss > ~2%
3. **Overuse detector**: Kalman filter classifies network as underuse/normal/overuse
4. **Rate adaptation**: On overuse, multiply by 0.85. On underuse, additive increase.

**Key insight**: GCC prioritizes **latency stability over throughput**. Sacrifices bandwidth to avoid queue buildup. Opposite of BBR/CUBIC.

### SCReAM (RFC 8298)
- Self-clocked, window-based (like TCP)
- Targets ~50ms of queuing delay
- Coexists better with TCP traffic

---

## 7. The Seven Tricks for "Instant" Perception

1. **Prioritize freshness over completeness**: Drop stale-in-flight data
2. **Pipeline everything**: Don't wait for complete frames to start sending
3. **Minimize buffering at every stage**: Hardware encode, adaptive jitter buffer
4. **Temporal scalability (SVC)**: Base + enhancement layers, drop enhancements under pressure
5. **Speculative FEC**: Send redundancy proactively before loss detected
6. **Encoder-decoder cooperation**: Long-term reference frames, recovery without full keyframe
7. **Render before complete**: Show partial data, conceal errors

---

## 8. Predictive Streaming

### Existing Implementations
- **Game streaming** (Stadia/xCloud): Predict next input, pre-render frame. Saves ~16-33ms.
- **360/VR video**: Head tracking predicts viewport 200-500ms ahead, pre-load tiles. ~85-95% accuracy at 200ms.
- **HTTP/2 Server Push**: Pre-send anticipated resources (deprecated due to poor prediction accuracy).

### Principles for Our Protocol
1. **Priority queuing with deadline-based expiry**: Message has freshness deadline; if missed, skip it
2. **Delta encoding with periodic sync points**: Full state occasionally, deltas between
3. **Layered encoding**: Critical base + optional enhancement data
4. **Speculative pre-computation**: Predict and pre-send likely-needed state
5. **Receiver-driven adaptation**: Receiver signals state (buffer depth, loss rate), sender adapts
