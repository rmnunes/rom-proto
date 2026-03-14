# ROM Python Demo — CRDT State Streaming

Two peers connected over loopback, streaming CRDT state updates.
No HTTP. No REST. No JSON. Just continuous state with automatic merge.

## Install

```bash
pip install -r requirements.txt
```

## Run

```bash
python state_streaming_demo.py
```

## What it demonstrates

- **LWW Register**: last-write-wins text field synced between peers
- **G-Counter**: grow-only counter that converges across peers
- **Ed25519 signing**: every delta is cryptographically signed
- **Bidirectional streaming**: both peers can push state to each other
