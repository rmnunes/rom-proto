# 01 - Existing Real-Time Protocols: Landscape & Limitations

## Overview
Analysis of every major protocol in use today and why each falls short of continuous state streaming.

---

## 1. HTTP/REST

### Architecture
Strict request-response cycle. Stateless by design (RFC 7230-7235).

### Connection Flow
```
Client                          Server
  |--- TCP SYN ------------------>|     ~1 RTT
  |<-- TCP SYN-ACK ---------------|
  |--- TCP ACK ------------------->|
  |--- TLS ClientHello ----------->|     ~1-2 RTT (TLS 1.2: 2 RTT, TLS 1.3: 1 RTT)
  |<-- TLS ServerHello ------------|
  |--- HTTP Request -------------->|     ~1 RTT
  |<-- HTTP Response --------------|
  Total minimum: 3-4 RTTs before first byte of application data
```

### Latency
- Connection establishment: 2-3 RTTs minimum
- Per-request overhead: 1 full RTT even with keep-alive
- Typical RTTs: 50-200ms broadband, 200-600ms mobile
- Total first-request: 150-1200ms

### Overhead
- HTTP/1.1 headers: 200-800 bytes/request (uncompressed text)
- HTTP/2 HPACK: ~20-50 bytes for repeated requests
- Cookies: 1-4KB/request
- JSON: 30-100% overhead vs binary

### Why It Falls Short
- Polling is the only "real-time" option (10 updates/sec max)
- Long-polling: 1 RTT gap between events
- Head-of-line blocking (HTTP/1.1)
- No server push of arbitrary data
- Fundamentally half-duplex at application level

---

## 2. WebSockets (RFC 6455)

### Architecture
Full-duplex over single TCP connection. HTTP Upgrade handshake, then binary framing.

### Frame Format
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-------+-+-------------+-------------------------------+
|F|R|R|R| opcode|M| Payload len |    Extended payload length    |
|I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
|N|V|V|V|       |S|             |   (if payload len==126/127)   |
| |1|2|3|       |K|             |                               |
+-+-+-+-+-------+-+-------------+-------------------------------+
```

### Latency
- Initial: TCP + TLS + HTTP Upgrade = 3-4 RTTs
- Subsequent: Near-zero protocol overhead, 1-10ms LAN, 20-100ms internet

### Critical Limitation (from MDN)
**No backpressure support** in standard API. Messages faster than processing = unbounded memory growth or 100% CPU.

### Why It Falls Short
1. **TCP head-of-line blocking**: Single lost segment stalls ALL data (200-1000ms+ at 1% loss)
2. **Ordered delivery enforced by TCP**: Newer state can't leapfrog older lost packets
3. **No unreliable delivery mode**: Cannot skip retransmission of stale data
4. **No native multiplexing**: One WebSocket = one logical channel
5. **No built-in reconnection or state reconciliation**

---

## 3. gRPC Streaming

### Architecture
Built on HTTP/2 with Protocol Buffers. Four patterns: unary, server-stream, client-stream, bidirectional.

### Message Framing
```
+------------------+-------------------+------------------+
| Compressed flag  | Message length    | Protobuf payload |
| (1 byte)         | (4 bytes)         | (N bytes)        |
+------------------+-------------------+------------------+
Overhead: 5 bytes/message + HTTP/2 frame headers (9 bytes)
```

### Latency
- Connection: 2-3 RTTs
- Per-message: 0.5-5ms LAN
- Throughput: 10,000-100,000+ msg/sec

### Why It Falls Short
1. **HTTP/2 over TCP**: Lost TCP packet blocks ALL multiplexed gRPC streams
2. **No unreliable delivery**: Every message must be delivered
3. **No browser-native support**: Requires gRPC-Web proxy
4. **Strictly client-server**: No P2P

---

## 4. WebRTC

### Architecture
P2P protocol suite for real-time audio/video/data over UDP.

### Protocol Stack
```
+-------------------------------------------+
|  SRTP (media)    |  SCTP (data channels)  |
+-------------------------------------------+
|          DTLS (encryption)                |
+-------------------------------------------+
|          ICE (connectivity)               |
+-------------------------------------------+
|          UDP (transport)                  |
+-------------------------------------------+
```

### Data Channels -- Configurable Per Channel
- `ordered: true/false`
- `maxRetransmits: N` (0 = unreliable)
- `maxPacketLifeTime: ms` (discard if not delivered in time)

### Latency
- **Connection setup: 3-10 seconds** (ICE gathering + DTLS) -- major downside
- Media: 50-150ms glass-to-glass video
- Data channel: 1-50ms (unreliable unordered: sub-10ms LAN)

### Why It Falls Short (Despite Being Closest)
1. **Connection establishment is slow** (seconds, not milliseconds)
2. **Designed for media, not state**: No state graph concept
3. **Jitter buffers**: 20-200ms mandatory for media quality
4. **P2P scaling**: Mesh = O(n^2), SFU re-introduces server
5. **NAT traversal failures**: ~10-15% require TURN relay

---

## 5. QUIC / HTTP/3

### Architecture
UDP-based transport integrating TLS 1.3. Eliminates TCP's fundamental latency penalties.

### Key Innovations
```
Connection Setup:
  TCP + TLS 1.3: 2 RTTs minimum
  QUIC new:      1 RTT
  QUIC resumed:  0 RTT (data with first packet!)

Head-of-Line Blocking:
  TCP/HTTP/2:  Lost packet on stream 1 blocks streams 2, 3, 4...
  QUIC/HTTP/3: Lost packet on stream 1 blocks ONLY stream 1
```

### QUIC Datagrams (RFC 9221)
Unreliable delivery extension -- not retransmitted. Perfect for real-time state.

### Why It Falls Short
1. Individual streams still ordered and reliable (unless using datagrams)
2. Congestion control still applies (Cubic/BBR)
3. ~3-5% of networks block/rate-limit UDP
4. **No built-in state semantics**: Moves bytes, not application state

---

## 6. MQTT

### Architecture
Pub/sub with broker. Designed for constrained IoT devices.

### QoS Levels
| Level | Semantics | Messages | Latency |
|-------|-----------|----------|---------|
| QoS 0 | At most once | 1 (fire-and-forget) | Minimum |
| QoS 1 | At least once | 2 | +1 RTT |
| QoS 2 | Exactly once | 4 | +2 RTTs |

### Why It Falls Short
1. Broker bottleneck (every message traverses broker)
2. TCP-based
3. No delta/diff support
4. No "deliver latest, skip stale" option

---

## 7. Server-Sent Events (SSE)

### Architecture
Unidirectional server-to-client over HTTP. `text/event-stream` MIME type.

### Critical Limitation
**6 concurrent SSE connections per browser per domain on HTTP/1.1** (all tabs combined). "Won't fix" in Chrome/Firefox.

### Why It Falls Short
1. Unidirectional only
2. Text-only (binary requires Base64 = 33% overhead)
3. Connection limit
4. TCP-based

---

## 8. Emerging: WebTransport
Built on QUIC. Unreliable datagrams + reliable streams + bidirectional. **Closest standard protocol to what we need** but still lacks state awareness.

## 9. Emerging: Media over QUIC (MoQ)
IETF draft. Pub/sub over QUIC with priorities and group-based delivery. Can skip old groups. Potentially adaptable for state synchronization.

---

## Comparative Summary

| Protocol | Transport | Direction | Setup | Per-Msg | Unreliable | State-Aware |
|----------|-----------|-----------|-------|---------|------------|-------------|
| HTTP/REST | TCP+TLS | Half-duplex | 2-4 RTT | 1 RTT/req | No | No |
| WebSocket | TCP+TLS | Full-duplex | 3-4 RTT | <1ms+net | No | No |
| gRPC | TCP+TLS/H2 | Bidirectional | 2-3 RTT | <1ms+net | No | No |
| WebRTC | UDP/DTLS | P2P Bidir | 3-10s | <10ms | Yes | No |
| QUIC/HTTP3 | UDP | Multiplexed | 0-1 RTT | <1ms+net | Datagrams | No |
| MQTT | TCP+TLS | Pub/Sub | 2-4 RTT | 1-10ms | QoS 0 | Retained only |
| SSE | TCP+TLS | Server->Client | 2-3 RTT | 5-50ms | No | No |

---

## The Universal Gap

Every existing protocol shares these shortcomings:

1. **Message-oriented, not state-oriented**: All transmit messages/packets. None understand structured, versioned, diffable application state.
2. **No "latest state wins" primitive**: TCP delivers in order; UDP requires app logic for staleness.
3. **No binary delta built in**: Differential state transmission always left to application.
4. **Serialization overhead**: Serialize -> transmit -> deserialize every time.
5. **No "this supersedes all previous data on this topic" semantics**.
