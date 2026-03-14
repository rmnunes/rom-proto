//! State Streaming Demo (Rust)
//!
//! Demonstrates the core ROM paradigm: two peers continuously
//! stream state updates through CRDT-backed state regions.
//! No request/response. No explicit fetch. Just mutate locally,
//! flush, and the other side sees it instantly.
//!
//! This is the "video frame" model applied to application state.

use std::sync::Arc;

use rmnunes_rom::{CrdtType, KeyPair, Peer, PublicKey, Reliability, Transport};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("=== ROM State Streaming Demo (Rust) ===\n");

    // Create in-process transports (simulates network)
    let t_server = Transport::loopback(1)?;
    t_server.bind("127.0.0.1", 9001)?;

    let t_client = Transport::loopback(1)?;
    t_client.bind("127.0.0.1", 9002)?;

    // Generate Ed25519 key pairs — every peer signs its state deltas
    let keys_server = KeyPair::generate();
    let keys_client = KeyPair::generate();

    // Copy public keys before moving keys into peers
    let server_pub = PublicKey::from_bytes(*keys_server.public_key());
    let client_pub = PublicKey::from_bytes(*keys_client.public_key());

    // Create peers with mandatory identity
    let server = Arc::new(Peer::new(1, t_server, &keys_server));
    server.set_local_endpoint("127.0.0.1", 9001)?;

    let client = Arc::new(Peer::new(2, t_client, &keys_client));
    client.set_local_endpoint("127.0.0.1", 9002)?;

    // Exchange public keys so each side can verify the other's signatures
    server.register_peer_key(2, &client_pub);
    client.register_peer_key(1, &server_pub);

    // Declare shared state regions on both peers
    for peer in [&server, &client] {
        peer.declare("/game/player/1/position", CrdtType::LwwRegister, Reliability::Reliable)?;
        peer.declare("/game/player/1/health", CrdtType::LwwRegister, Reliability::Reliable)?;
        peer.declare("/game/score", CrdtType::GCounter, Reliability::Reliable)?;
    }

    // --- Handshake ---
    println!("1. Connecting...");
    let client_clone = Arc::clone(&client);
    let connect_handle = std::thread::spawn(move || {
        client_clone.connect("127.0.0.1", 9001).unwrap();
    });

    server.accept("127.0.0.1", 9002, 5000)?;
    connect_handle.join().unwrap();

    println!(
        "   Connected: client={}, server={}\n",
        client.is_connected(),
        server.is_connected()
    );

    // --- Stream state updates (like video frames) ---
    println!("2. Streaming position updates (simulating 10 'frames')...");
    for i in 0..10 {
        // Client moves player
        let pos = format!("x:{},y:{}", i * 10, i * 5);
        client.set_lww("/game/player/1/position", pos.as_bytes())?;

        // Client scores points
        client.increment_counter("/game/score", 100)?;

        // Flush: sign + encode deltas -> send over transport
        client.flush()?;

        // Server receives: decode -> verify signature -> CRDT merge
        server.poll(100)?;

        println!("   frame {i}: position={pos}");
    }

    // --- Read final state on server ---
    println!("\n3. Final state on server:");
    let position = server.get_lww("/game/player/1/position")?;
    let position = String::from_utf8_lossy(&position);
    let score = server.get_counter("/game/score")?;
    println!("   Position: {position}");
    println!("   Score: {score}");

    // --- Server pushes health update back to client ---
    println!("\n4. Server sets player health, client receives:");
    server.set_lww("/game/player/1/health", b"hp:85/100")?;
    server.flush()?;
    client.poll(100)?;

    let health = client.get_lww("/game/player/1/health")?;
    let health = String::from_utf8_lossy(&health);
    println!("   Client sees health: {health}");

    // --- Disconnect ---
    println!("\n5. Disconnecting...");
    client.disconnect();
    println!("   Done.");

    println!("\n=== Demo Complete ===");
    println!("No HTTP. No REST. No JSON. No request/response.");
    println!("Just continuous state streaming with automatic CRDT merge.");
    println!("Every delta is Ed25519-signed. Trust the data, not the channel.");

    Ok(())
}
