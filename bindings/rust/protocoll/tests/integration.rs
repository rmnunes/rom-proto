//! Integration tests for the safe protocoll Rust API.
//!
//! These mirror key C++ integration tests to validate the binding layer.

use protocoll::{CrdtType, KeyPair, Peer, PublicKey, Reliability, Transport};

#[test]
fn test_keypair_generation() {
    let keys = KeyPair::generate();
    // Public key should not be all zeros
    assert!(keys.public_key().iter().any(|&b| b != 0));
}

#[test]
fn test_public_key_roundtrip() {
    let keys = KeyPair::generate();
    let pk = PublicKey::from_bytes(*keys.public_key());
    assert_eq!(pk.as_bytes(), keys.public_key());
}

#[test]
fn test_loopback_transport_create() {
    let t = Transport::loopback(1);
    assert!(t.is_ok());
}

#[test]
fn test_udp_transport_create() {
    // NOTE: pcol_transport_udp_create is currently a stub returning null.
    // This test validates the error path.
    let t = Transport::udp();
    assert!(t.is_err());
}

#[test]
fn test_peer_create() {
    let keys = KeyPair::generate();
    let transport = Transport::loopback(100).unwrap();
    let peer = Peer::new(1, transport, &keys);
    assert_eq!(peer.node_id(), 1);
    assert!(!peer.is_connected());
}

#[test]
fn test_peer_declare_and_set_lww() {
    let keys = KeyPair::generate();
    let transport = Transport::loopback(101).unwrap();
    let peer = Peer::new(1, transport, &keys);

    peer.declare("/test/val", CrdtType::LwwRegister, Reliability::Reliable)
        .unwrap();

    peer.set_lww("/test/val", b"hello").unwrap();

    let val = peer.get_lww("/test/val").unwrap();
    assert_eq!(&val, b"hello");
}

#[test]
fn test_peer_declare_and_increment_counter() {
    let keys = KeyPair::generate();
    let transport = Transport::loopback(102).unwrap();
    let peer = Peer::new(1, transport, &keys);

    peer.declare("/test/count", CrdtType::GCounter, Reliability::Reliable)
        .unwrap();

    peer.increment_counter("/test/count", 5).unwrap();

    let val = peer.get_counter("/test/count").unwrap();
    assert_eq!(val, 5);
}

#[test]
fn test_peer_not_found_error() {
    let keys = KeyPair::generate();
    let transport = Transport::loopback(103).unwrap();
    let peer = Peer::new(1, transport, &keys);

    let result = peer.set_lww("/nonexistent", b"data");
    assert!(result.is_err());
}

#[test]
fn test_peer_public_key() {
    let keys = KeyPair::generate();
    let transport = Transport::loopback(104).unwrap();
    let peer = Peer::new(1, transport, &keys);

    let pk = peer.public_key();
    assert_eq!(pk.as_bytes(), keys.public_key());
}

#[test]
fn test_peer_access_control() {
    let keys = KeyPair::generate();
    let transport = Transport::loopback(105).unwrap();
    let peer = Peer::new(1, transport, &keys);

    // Should not panic
    peer.set_access_control(true);
    peer.set_access_control(false);
}

#[test]
fn test_loopback_handshake_and_state_exchange() {
    use std::sync::Arc;

    // Server setup
    let keys_s = KeyPair::generate();
    let keys_s_pub = *keys_s.public_key();
    let t_s = Transport::loopback(200).unwrap();
    t_s.bind("127.0.0.1", 9001).unwrap();
    let server = Arc::new(Peer::new(1, t_s, &keys_s));
    server.set_local_endpoint("127.0.0.1", 9001).unwrap();

    // Client setup
    let keys_c = KeyPair::generate();
    let keys_c_pub = *keys_c.public_key();
    let t_c = Transport::loopback(200).unwrap();
    t_c.bind("127.0.0.1", 9002).unwrap();
    let client = Arc::new(Peer::new(2, t_c, &keys_c));
    client.set_local_endpoint("127.0.0.1", 9002).unwrap();

    // Exchange keys
    server.register_peer_key(2, &PublicKey::from_bytes(keys_c_pub));
    client.register_peer_key(1, &PublicKey::from_bytes(keys_s_pub));

    // Declare state
    client.declare("/test/val", CrdtType::LwwRegister, Reliability::Reliable).unwrap();
    server.declare("/test/val", CrdtType::LwwRegister, Reliability::Reliable).unwrap();

    // Connect in separate thread (blocking call)
    let client_clone = Arc::clone(&client);
    let connect_handle = std::thread::spawn(move || {
        client_clone.connect("127.0.0.1", 9001).unwrap();
    });

    // Accept on main thread
    server.accept("127.0.0.1", 9002, 5000).unwrap();
    connect_handle.join().unwrap();

    assert!(client.is_connected());
    assert!(server.is_connected());

    // Client writes
    client.set_lww("/test/val", b"from_client").unwrap();
    client.flush().unwrap();

    // Server reads
    server.poll(100).unwrap();
    let val = server.get_lww("/test/val").unwrap();
    assert_eq!(&val, b"from_client");
}
