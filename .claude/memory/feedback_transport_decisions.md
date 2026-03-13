---
name: Browser Transport Decisions
description: User decisions on browser transport strategy — WebTransport default, WebSocket fallback, no WebRTC.
type: feedback
---

WebTransport is the default/recommended browser transport. WebSocket is a compatibility fallback only (for Safari until mid-late 2026). Do NOT implement WebRTC.

**Why:** User explicitly chose this after discussion. WebTransport replaces WebSocket long-term (QUIC-based, unreliable datagrams). WebRTC adds complexity with no clear benefit for ROM's use case.

**How to apply:** When suggesting or implementing browser networking features, always default to WebTransport. Only mention WebSocket as a fallback option. Never propose WebRTC.
