//! Transport abstraction for network communication.

use crate::error::{self, Result};
use std::ffi::CString;

/// Network transport handle (owns the underlying C resource).
pub struct Transport {
    raw: *mut protocoll_sys::PcolTransport,
}

// Safety: The underlying C API uses single-thread ownership. The Peer
// ensures exclusive access. Send+Sync are needed for Arc<Peer> in tests.
unsafe impl Send for Transport {}
unsafe impl Sync for Transport {}

impl Transport {
    /// Create a UDP transport.
    pub fn udp() -> Result<Self> {
        let raw = unsafe { protocoll_sys::pcol_transport_udp_create() };
        if raw.is_null() {
            return Err(crate::Error::Internal);
        }
        Ok(Self { raw })
    }

    /// Create a loopback transport for testing.
    /// Transports with the same `bus_id` can communicate with each other.
    pub fn loopback(bus_id: u32) -> Result<Self> {
        let raw = unsafe { protocoll_sys::pcol_transport_loopback_create(bus_id) };
        if raw.is_null() {
            return Err(crate::Error::Internal);
        }
        Ok(Self { raw })
    }

    /// Bind to a local address and port.
    pub fn bind(&self, address: &str, port: u16) -> Result<()> {
        let addr_c = CString::new(address).map_err(|_| crate::Error::Invalid)?;
        let ep = protocoll_sys::PcolEndpoint {
            address: addr_c.as_ptr(),
            port,
        };
        let code = unsafe { protocoll_sys::pcol_transport_bind(self.raw, ep) };
        error::check(code)
    }

    /// Get the raw pointer (for passing to Peer).
    pub(crate) fn as_raw(&self) -> *mut protocoll_sys::PcolTransport {
        self.raw
    }

    /// Consume self and return the raw pointer without destroying it.
    /// Used when transferring ownership to a Peer.
    pub(crate) fn into_raw(self) -> *mut protocoll_sys::PcolTransport {
        let raw = self.raw;
        std::mem::forget(self);
        raw
    }
}

impl Drop for Transport {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { protocoll_sys::pcol_transport_destroy(self.raw) };
        }
    }
}
