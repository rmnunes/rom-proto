//! Safe Rust bindings for the protocoll state streaming library.
//!
//! # Example
//! ```no_run
//! use protocoll::{Peer, Transport, KeyPair, CrdtType, Reliability};
//!
//! let keys = KeyPair::generate();
//! let transport = Transport::udp().unwrap();
//! transport.bind("127.0.0.1", 9000).unwrap();
//! let peer = Peer::new(1, transport, &keys);
//! peer.declare("/game/pos", CrdtType::LwwRegister, Reliability::Reliable).unwrap();
//! ```

pub mod error;
pub mod transport;
pub mod peer;
pub mod state;

pub use error::{Error, Result};
pub use transport::Transport;
pub use peer::Peer;
pub use state::{CrdtType, Reliability, ResolutionTier};

/// Ed25519 key pair for signing state deltas.
pub struct KeyPair {
    inner: protocoll_sys::PcolKeyPair,
}

impl KeyPair {
    /// Generate a new random Ed25519 key pair.
    pub fn generate() -> Self {
        let mut inner = protocoll_sys::PcolKeyPair {
            public_key: [0u8; 32],
            secret_key: [0u8; 64],
        };
        unsafe {
            protocoll_sys::pcol_generate_keypair(&mut inner);
        }
        Self { inner }
    }

    /// Get the public key bytes.
    pub fn public_key(&self) -> &[u8; 32] {
        &self.inner.public_key
    }

    pub(crate) fn as_raw(&self) -> &protocoll_sys::PcolKeyPair {
        &self.inner
    }
}

/// Public key for verifying signatures from a remote peer.
#[derive(Clone)]
pub struct PublicKey {
    inner: protocoll_sys::PcolPublicKey,
}

impl PublicKey {
    /// Create from raw bytes.
    pub fn from_bytes(bytes: [u8; 32]) -> Self {
        Self {
            inner: protocoll_sys::PcolPublicKey { bytes },
        }
    }

    /// Get the raw bytes.
    pub fn as_bytes(&self) -> &[u8; 32] {
        &self.inner.bytes
    }

    pub(crate) fn as_raw(&self) -> &protocoll_sys::PcolPublicKey {
        &self.inner
    }
}

/// Get the packed API version (major<<16 | minor<<8 | patch).
pub fn api_version() -> u32 {
    unsafe { protocoll_sys::pcol_api_version() }
}
