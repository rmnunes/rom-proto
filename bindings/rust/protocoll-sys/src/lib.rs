//! Raw FFI bindings for the protocoll C library.
//!
//! This crate provides bindings for the protocoll C API.
//! For a safe Rust API, use the `protocoll` crate instead.
//!
//! Bindings are pre-generated in `bindings.rs`. To regenerate from the C header:
//! ```sh
//! bindgen ../../../include/protocoll/protocoll.h \
//!   --allowlist-function "pcol_.*" --allowlist-type "Pcol.*" \
//!   > src/bindings.rs
//! ```

mod bindings;
pub use bindings::*;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_generate_keypair() {
        unsafe {
            let mut kp: PcolKeyPair = std::mem::zeroed();
            pcol_generate_keypair(&mut kp);
            // Key should not be all zeros after generation
            assert!(kp.public_key.iter().any(|&b| b != 0));
        }
    }

    #[test]
    fn test_error_codes() {
        assert_eq!(PcolError_PCOL_OK, 0);
        assert_eq!(PcolError_PCOL_ERR_INVALID, -1);
    }
}
