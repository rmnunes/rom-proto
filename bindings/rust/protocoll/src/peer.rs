//! Peer: high-level API for a protocoll endpoint.

use crate::error::{self, Result};
use crate::state::{CrdtType, Reliability, ResolutionTier};
use crate::{KeyPair, PublicKey, Transport};
use std::ffi::CString;

/// A protocoll peer endpoint that owns its transport.
///
/// RAII: when dropped, the peer is disconnected and destroyed,
/// then the transport is destroyed.
pub struct Peer {
    raw: *mut protocoll_sys::PcolPeer,
    // Keep transport alive — Peer borrows it in C API
    _transport: Transport,
}

// Safety: The underlying C API uses single-thread ownership per peer.
// Callers must ensure no concurrent access to the same Peer.
unsafe impl Send for Peer {}
unsafe impl Sync for Peer {}

impl Peer {
    /// Create a new peer with the given node ID, transport, and key pair.
    /// The transport is moved into the Peer and will be destroyed when the Peer is dropped.
    pub fn new(node_id: u16, transport: Transport, keys: &KeyPair) -> Self {
        let raw = unsafe {
            protocoll_sys::pcol_peer_create(node_id, transport.as_raw(), keys.as_raw())
        };
        Self {
            raw,
            _transport: transport,
        }
    }

    /// Get this peer's node ID.
    pub fn node_id(&self) -> u16 {
        unsafe { protocoll_sys::pcol_peer_node_id(self.raw) }
    }

    /// Get this peer's public key.
    pub fn public_key(&self) -> PublicKey {
        let mut pk = protocoll_sys::PcolPublicKey { bytes: [0u8; 32] };
        unsafe { protocoll_sys::pcol_peer_public_key(self.raw, &mut pk) };
        PublicKey::from_bytes(pk.bytes)
    }

    /// Register a remote peer's public key for signature verification.
    pub fn register_peer_key(&self, remote_node_id: u16, pk: &PublicKey) {
        unsafe {
            protocoll_sys::pcol_peer_register_key(self.raw, remote_node_id, pk.as_raw());
        }
    }

    /// Connect to a remote peer.
    pub fn connect(&self, address: &str, port: u16) -> Result<()> {
        let addr_c = CString::new(address).map_err(|_| crate::Error::Invalid)?;
        let ep = protocoll_sys::PcolEndpoint {
            address: addr_c.as_ptr(),
            port,
        };
        let code = unsafe { protocoll_sys::pcol_peer_connect(self.raw, ep) };
        error::check(code)
    }

    /// Accept a connection from a remote peer.
    pub fn accept(&self, address: &str, port: u16, timeout_ms: i32) -> Result<()> {
        let addr_c = CString::new(address).map_err(|_| crate::Error::Invalid)?;
        let ep = protocoll_sys::PcolEndpoint {
            address: addr_c.as_ptr(),
            port,
        };
        let code = unsafe { protocoll_sys::pcol_peer_accept(self.raw, ep, timeout_ms) };
        error::check(code)
    }

    /// Check if connected to a remote peer.
    pub fn is_connected(&self) -> bool {
        unsafe { protocoll_sys::pcol_peer_is_connected(self.raw) != 0 }
    }

    /// Disconnect from the remote peer.
    pub fn disconnect(&self) {
        unsafe { protocoll_sys::pcol_peer_disconnect(self.raw) };
    }

    /// Set the local endpoint for this peer.
    pub fn set_local_endpoint(&self, address: &str, port: u16) -> Result<()> {
        let addr_c = CString::new(address).map_err(|_| crate::Error::Invalid)?;
        let ep = protocoll_sys::PcolEndpoint {
            address: addr_c.as_ptr(),
            port,
        };
        unsafe { protocoll_sys::pcol_peer_set_local_endpoint(self.raw, ep) };
        Ok(())
    }

    /// Declare a state region with a CRDT type.
    pub fn declare(&self, path: &str, crdt_type: CrdtType, reliability: Reliability) -> Result<()> {
        let path_c = CString::new(path).map_err(|_| crate::Error::Invalid)?;
        let code = unsafe {
            protocoll_sys::pcol_declare(
                self.raw,
                path_c.as_ptr(),
                crdt_type.to_raw(),
                reliability.to_raw(),
            )
        };
        error::check(code)
    }

    /// Set an LWW register value.
    pub fn set_lww(&self, path: &str, data: &[u8]) -> Result<()> {
        let path_c = CString::new(path).map_err(|_| crate::Error::Invalid)?;
        let code = unsafe {
            protocoll_sys::pcol_set_lww(self.raw, path_c.as_ptr(), data.as_ptr(), data.len())
        };
        error::check(code)
    }

    /// Increment a counter.
    pub fn increment_counter(&self, path: &str, amount: u64) -> Result<()> {
        let path_c = CString::new(path).map_err(|_| crate::Error::Invalid)?;
        let code = unsafe {
            protocoll_sys::pcol_increment_counter(self.raw, path_c.as_ptr(), amount)
        };
        error::check(code)
    }

    /// Read an LWW register value.
    pub fn get_lww(&self, path: &str) -> Result<Vec<u8>> {
        let path_c = CString::new(path).map_err(|_| crate::Error::Invalid)?;
        let mut buf = vec![0u8; 4096];
        let mut out_len: usize = 0;
        let code = unsafe {
            protocoll_sys::pcol_get_lww(
                self.raw,
                path_c.as_ptr(),
                buf.as_mut_ptr(),
                buf.len(),
                &mut out_len,
            )
        };
        error::check(code)?;
        buf.truncate(out_len);
        Ok(buf)
    }

    /// Read a counter value.
    pub fn get_counter(&self, path: &str) -> Result<u64> {
        let path_c = CString::new(path).map_err(|_| crate::Error::Invalid)?;
        let mut value: u64 = 0;
        let code = unsafe {
            protocoll_sys::pcol_get_counter(self.raw, path_c.as_ptr(), &mut value)
        };
        error::check(code)?;
        Ok(value)
    }

    /// Flush pending deltas (sign + encode + send).
    /// Returns the number of frames sent.
    pub fn flush(&self) -> Result<i32> {
        let result = unsafe { protocoll_sys::pcol_flush(self.raw) };
        if result < 0 {
            error::check(result)?;
        }
        Ok(result)
    }

    /// Poll for incoming data (receive + verify + decode + merge).
    /// Returns the number of state changes applied.
    pub fn poll(&self, timeout_ms: i32) -> Result<i32> {
        let result = unsafe { protocoll_sys::pcol_poll(self.raw, timeout_ms) };
        if result < 0 {
            error::check(result)?;
        }
        Ok(result)
    }

    /// Enable or disable access control.
    pub fn set_access_control(&self, enabled: bool) {
        unsafe {
            protocoll_sys::pcol_set_access_control(self.raw, if enabled { 1 } else { 0 });
        }
    }

    // --- Multi-connection (mesh topology) ---

    /// Connect to a specific remote node by its node ID and endpoint.
    pub fn connect_to(&self, remote_node_id: u16, address: &str, port: u16) -> Result<()> {
        let addr_c = CString::new(address).map_err(|_| crate::Error::Invalid)?;
        let ep = protocoll_sys::PcolEndpoint {
            address: addr_c.as_ptr(),
            port,
        };
        let code = unsafe { protocoll_sys::pcol_peer_connect_to(self.raw, remote_node_id, ep) };
        error::check(code)
    }

    /// Accept a connection from a specific remote node.
    pub fn accept_node(
        &self,
        remote_node_id: u16,
        address: &str,
        port: u16,
        timeout_ms: i32,
    ) -> Result<()> {
        let addr_c = CString::new(address).map_err(|_| crate::Error::Invalid)?;
        let ep = protocoll_sys::PcolEndpoint {
            address: addr_c.as_ptr(),
            port,
        };
        let code = unsafe {
            protocoll_sys::pcol_peer_accept_node(self.raw, remote_node_id, ep, timeout_ms)
        };
        error::check(code)
    }

    /// Disconnect a specific remote node.
    pub fn disconnect_node(&self, remote_node_id: u16) {
        unsafe { protocoll_sys::pcol_peer_disconnect_node(self.raw, remote_node_id) };
    }

    /// Check if connected to a specific remote node.
    pub fn is_connected_to(&self, remote_node_id: u16) -> bool {
        unsafe { protocoll_sys::pcol_peer_is_connected_to(self.raw, remote_node_id) != 0 }
    }

    // --- Resolution tiers ---

    /// Set the resolution tier for a specific remote node.
    pub fn set_resolution(&self, remote_node_id: u16, tier: ResolutionTier) {
        unsafe {
            protocoll_sys::pcol_peer_set_resolution(self.raw, remote_node_id, tier.to_raw());
        }
    }

    // --- Routing ---

    /// Announce a route for the given path so other nodes can discover it.
    pub fn announce_route(&self, path: &str) -> Result<()> {
        let path_c = CString::new(path).map_err(|_| crate::Error::Invalid)?;
        unsafe { protocoll_sys::pcol_peer_announce_route(self.raw, path_c.as_ptr()) };
        Ok(())
    }

    /// Learn a route: associate a path hash with the node that can reach it.
    pub fn learn_route(&self, path_hash: u32, via_node: u16) {
        unsafe { protocoll_sys::pcol_peer_learn_route(self.raw, path_hash, via_node) };
    }

    /// Check whether a route is known for the given path hash.
    pub fn has_route(&self, path_hash: u32) -> bool {
        unsafe { protocoll_sys::pcol_peer_has_route(self.raw, path_hash) != 0 }
    }

    // --- Subscriptions ---

    /// Subscribe to a pattern with a specific resolution tier, initial credits, and freshness.
    /// Returns the subscription ID on success.
    pub fn subscribe_with_resolution(
        &self,
        pattern: &str,
        tier: ResolutionTier,
        initial_credits: i32,
        freshness_us: u32,
    ) -> Result<u32> {
        let pattern_c = CString::new(pattern).map_err(|_| crate::Error::Invalid)?;
        let result = unsafe {
            protocoll_sys::pcol_subscribe_with_resolution(
                self.raw,
                pattern_c.as_ptr(),
                tier.to_raw(),
                initial_credits,
                freshness_us,
            )
        };
        if result < 0 {
            error::check(result)?;
        }
        Ok(result as u32)
    }
}

impl Drop for Peer {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { protocoll_sys::pcol_peer_destroy(self.raw) };
        }
    }
}
