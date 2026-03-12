//! Pre-generated FFI bindings for protocoll.h
//!
//! These bindings mirror the C API declared in include/protocoll/protocoll.h.
//! Regenerate with: bindgen ../../../include/protocoll/protocoll.h --allowlist-function "pcol_.*" --allowlist-type "Pcol.*"

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

// --- Error codes ---

pub type PcolError = i32;
pub const PcolError_PCOL_OK: PcolError = 0;
pub const PcolError_PCOL_ERR_INVALID: PcolError = -1;
pub const PcolError_PCOL_ERR_NOT_FOUND: PcolError = -2;
pub const PcolError_PCOL_ERR_NO_CONNECT: PcolError = -3;
pub const PcolError_PCOL_ERR_TIMEOUT: PcolError = -4;
pub const PcolError_PCOL_ERR_CRYPTO: PcolError = -5;
pub const PcolError_PCOL_ERR_INTERNAL: PcolError = -99;

// --- Opaque handles ---

#[repr(C)]
pub struct PcolTransport {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct PcolPeer {
    _opaque: [u8; 0],
}

// --- CRDT types ---

pub type PcolCrdtType = i32;
pub const PcolCrdtType_PCOL_LWW_REGISTER: PcolCrdtType = 0;
pub const PcolCrdtType_PCOL_G_COUNTER: PcolCrdtType = 1;
pub const PcolCrdtType_PCOL_PN_COUNTER: PcolCrdtType = 2;
pub const PcolCrdtType_PCOL_OR_SET: PcolCrdtType = 3;

// --- Reliability ---

pub type PcolReliability = i32;
pub const PcolReliability_PCOL_RELIABLE: PcolReliability = 0;
pub const PcolReliability_PCOL_BEST_EFFORT: PcolReliability = 1;

// --- Resolution tiers ---

pub type PcolResolutionTier = i32;
pub const PcolResolutionTier_PCOL_RESOLUTION_FULL: PcolResolutionTier = 0;
pub const PcolResolutionTier_PCOL_RESOLUTION_NORMAL: PcolResolutionTier = 1;
pub const PcolResolutionTier_PCOL_RESOLUTION_COARSE: PcolResolutionTier = 2;
pub const PcolResolutionTier_PCOL_RESOLUTION_METADATA: PcolResolutionTier = 3;

// --- Key types ---

#[repr(C)]
#[derive(Debug, Default, Clone, Copy)]
pub struct PcolPublicKey {
    pub bytes: [u8; 32],
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct PcolKeyPair {
    pub public_key: [u8; 32],
    pub secret_key: [u8; 64],
}

impl Default for PcolKeyPair {
    fn default() -> Self {
        Self {
            public_key: [0u8; 32],
            secret_key: [0u8; 64],
        }
    }
}

// --- Endpoint ---

#[repr(C)]
#[derive(Debug)]
pub struct PcolEndpoint {
    pub address: *const std::os::raw::c_char,
    pub port: u16,
}

// --- Callbacks ---

pub type PcolUpdateCallback = Option<
    unsafe extern "C" fn(
        path: *const std::os::raw::c_char,
        data: *const u8,
        data_len: usize,
        user_data: *mut std::os::raw::c_void,
    ),
>;

pub type PcolSigFailureCallback = Option<
    unsafe extern "C" fn(
        author_id: u16,
        path_hash: u32,
        user_data: *mut std::os::raw::c_void,
    ),
>;

// --- DTLS config ---

#[repr(C)]
#[derive(Debug)]
pub struct PcolDtlsConfig {
    pub is_server: std::os::raw::c_int,
    pub cert_pem: *const std::os::raw::c_char,
    pub key_pem: *const std::os::raw::c_char,
    pub ca_pem: *const std::os::raw::c_char,
    pub verify_peer: std::os::raw::c_int,
    pub handshake_timeout_ms: u32,
}

// --- FFI functions ---

extern "C" {
    // Key generation
    pub fn pcol_generate_keypair(out: *mut PcolKeyPair);

    // Transport
    pub fn pcol_transport_loopback_create(bus_id: u32) -> *mut PcolTransport;
    pub fn pcol_transport_udp_create() -> *mut PcolTransport;
    pub fn pcol_transport_bind(t: *mut PcolTransport, ep: PcolEndpoint) -> PcolError;
    pub fn pcol_transport_destroy(t: *mut PcolTransport);

    // DTLS transport
    pub fn pcol_transport_dtls_create(
        inner: *mut PcolTransport,
        config: *const PcolDtlsConfig,
    ) -> *mut PcolTransport;
    pub fn pcol_transport_dtls_handshake(
        t: *mut PcolTransport,
        remote: PcolEndpoint,
        timeout_ms: std::os::raw::c_int,
    ) -> PcolError;

    // Peer lifecycle
    pub fn pcol_peer_create(
        node_id: u16,
        transport: *mut PcolTransport,
        keys: *const PcolKeyPair,
    ) -> *mut PcolPeer;
    pub fn pcol_peer_destroy(peer: *mut PcolPeer);

    // Identity
    pub fn pcol_peer_node_id(peer: *const PcolPeer) -> u16;
    pub fn pcol_peer_public_key(peer: *const PcolPeer, out: *mut PcolPublicKey);
    pub fn pcol_peer_register_key(
        peer: *mut PcolPeer,
        remote_node_id: u16,
        pk: *const PcolPublicKey,
    );

    // Connection
    pub fn pcol_peer_connect(peer: *mut PcolPeer, remote: PcolEndpoint) -> PcolError;
    pub fn pcol_peer_accept(
        peer: *mut PcolPeer,
        expected_from: PcolEndpoint,
        timeout_ms: std::os::raw::c_int,
    ) -> PcolError;
    pub fn pcol_peer_is_connected(peer: *const PcolPeer) -> std::os::raw::c_int;
    pub fn pcol_peer_disconnect(peer: *mut PcolPeer);
    pub fn pcol_peer_set_local_endpoint(peer: *mut PcolPeer, ep: PcolEndpoint);

    // State declaration
    pub fn pcol_declare(
        peer: *mut PcolPeer,
        path: *const std::os::raw::c_char,
        crdt_type: PcolCrdtType,
        reliability: PcolReliability,
    ) -> PcolError;

    // State mutation
    pub fn pcol_set_lww(
        peer: *mut PcolPeer,
        path: *const std::os::raw::c_char,
        data: *const u8,
        len: usize,
    ) -> PcolError;
    pub fn pcol_increment_counter(
        peer: *mut PcolPeer,
        path: *const std::os::raw::c_char,
        amount: u64,
    ) -> PcolError;

    // State reading
    pub fn pcol_get_lww(
        peer: *const PcolPeer,
        path: *const std::os::raw::c_char,
        buf: *mut u8,
        buf_len: usize,
        out_len: *mut usize,
    ) -> PcolError;
    pub fn pcol_get_counter(
        peer: *const PcolPeer,
        path: *const std::os::raw::c_char,
        out_value: *mut u64,
    ) -> PcolError;

    // Network I/O
    pub fn pcol_flush(peer: *mut PcolPeer) -> std::os::raw::c_int;
    pub fn pcol_poll(peer: *mut PcolPeer, timeout_ms: std::os::raw::c_int) -> std::os::raw::c_int;

    // Callbacks
    pub fn pcol_on_update(
        peer: *mut PcolPeer,
        cb: PcolUpdateCallback,
        user_data: *mut std::os::raw::c_void,
    );
    pub fn pcol_on_signature_failure(
        peer: *mut PcolPeer,
        cb: PcolSigFailureCallback,
        user_data: *mut std::os::raw::c_void,
    );

    // Access control
    pub fn pcol_set_access_control(peer: *mut PcolPeer, enabled: std::os::raw::c_int);

    // Multi-connection (mesh topology)
    pub fn pcol_peer_connect_to(
        peer: *mut PcolPeer,
        remote_node_id: u16,
        remote: PcolEndpoint,
    ) -> PcolError;
    pub fn pcol_peer_accept_node(
        peer: *mut PcolPeer,
        remote_node_id: u16,
        expected_from: PcolEndpoint,
        timeout_ms: std::os::raw::c_int,
    ) -> PcolError;
    pub fn pcol_peer_disconnect_node(peer: *mut PcolPeer, remote_node_id: u16);
    pub fn pcol_peer_is_connected_to(
        peer: *const PcolPeer,
        remote_node_id: u16,
    ) -> std::os::raw::c_int;

    // Resolution tiers
    pub fn pcol_peer_set_resolution(
        peer: *mut PcolPeer,
        remote_node_id: u16,
        tier: PcolResolutionTier,
    );

    // Routing
    pub fn pcol_peer_announce_route(
        peer: *mut PcolPeer,
        path: *const std::os::raw::c_char,
    );
    pub fn pcol_peer_learn_route(
        peer: *mut PcolPeer,
        path_hash: u32,
        via_node: u16,
    );
    pub fn pcol_peer_has_route(
        peer: *const PcolPeer,
        path_hash: u32,
    ) -> std::os::raw::c_int;

    // Subscription with resolution
    pub fn pcol_subscribe_with_resolution(
        peer: *mut PcolPeer,
        pattern: *const std::os::raw::c_char,
        tier: PcolResolutionTier,
        initial_credits: i32,
        freshness_us: u32,
    ) -> i32;

    // API version
    pub fn pcol_api_version() -> u32;
}
