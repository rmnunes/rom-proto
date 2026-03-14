"""
State Streaming Demo (Python)

Demonstrates the core ROM paradigm: two peers continuously
stream state updates through CRDT-backed state regions.
No request/response. No explicit fetch. Just mutate locally,
flush, and the other side sees it instantly.

This is the "video frame" model applied to application state.
"""

import threading
from protocoll import KeyPair, Transport, Peer, CrdtType, Reliability


def main():
    print("=== ROM State Streaming Demo (Python) ===\n")

    # Create in-process transports (simulates network)
    bus_id = 1
    t_server = Transport.loopback(bus_id=bus_id)
    t_server.bind("127.0.0.1", 9001)

    t_client = Transport.loopback(bus_id=bus_id)
    t_client.bind("127.0.0.1", 9002)

    # Generate Ed25519 key pairs — every peer signs its state deltas
    keys_server = KeyPair.generate()
    keys_client = KeyPair.generate()

    # Create peers with mandatory identity
    server = Peer(node_id=1, transport=t_server, keys=keys_server)
    server.set_local_endpoint("127.0.0.1", 9001)

    client = Peer(node_id=2, transport=t_client, keys=keys_client)
    client.set_local_endpoint("127.0.0.1", 9002)

    # Exchange public keys so each side can verify the other's signatures
    server.register_peer_key(2, keys_client.public_key)
    client.register_peer_key(1, keys_server.public_key)

    # Declare shared state regions
    for peer in (server, client):
        peer.declare("/game/player/1/position", CrdtType.LWW_REGISTER)
        peer.declare("/game/player/1/health", CrdtType.LWW_REGISTER)
        peer.declare("/game/score", CrdtType.G_COUNTER)

    # --- Handshake ---
    print("1. Connecting...")

    def do_connect():
        client.connect("127.0.0.1", 9001)

    t = threading.Thread(target=do_connect)
    t.start()
    server.accept("127.0.0.1", 9002, timeout_ms=5000)
    t.join(timeout=10)

    print(f"   Connected: client={client.is_connected}, server={server.is_connected}\n")

    # --- Stream state updates (like video frames) ---
    print("2. Streaming position updates (simulating 10 'frames')...")
    for i in range(10):
        # Client moves player
        pos = f"x:{i * 10},y:{i * 5}"
        client.set_lww("/game/player/1/position", pos.encode())

        # Client scores points
        client.increment_counter("/game/score", 100)

        # Flush: sign + encode deltas -> send over transport
        client.flush()

        # Server receives: decode -> verify signature -> CRDT merge
        server.poll(timeout_ms=100)

        print(f"   frame {i}: position={pos}")

    # --- Read final state on server ---
    print("\n3. Final state on server:")
    position = server.get_lww("/game/player/1/position").decode()
    score = server.get_counter("/game/score")
    print(f"   Position: {position}")
    print(f"   Score: {score}")

    # --- Server pushes health update back to client ---
    print("\n4. Server sets player health, client receives:")
    server.set_lww("/game/player/1/health", b"hp:85/100")
    server.flush()
    client.poll(timeout_ms=100)

    health = client.get_lww("/game/player/1/health").decode()
    print(f"   Client sees health: {health}")

    # --- Disconnect ---
    print("\n5. Disconnecting...")
    client.disconnect()
    server.destroy()
    client.destroy()
    print("   Done.")

    print("\n=== Demo Complete ===")
    print("No HTTP. No REST. No JSON. No request/response.")
    print("Just continuous state streaming with automatic CRDT merge.")
    print("Every delta is Ed25519-signed. Trust the data, not the channel.")


if __name__ == "__main__":
    main()
