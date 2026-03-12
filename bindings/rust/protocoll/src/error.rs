//! Error types for the protocoll Rust API.

use std::fmt;

/// Error codes from the protocoll C library.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// Invalid argument.
    Invalid,
    /// Path not declared.
    NotFound,
    /// Not connected.
    NoConnection,
    /// Operation timed out.
    Timeout,
    /// Signature verification failed.
    Crypto,
    /// Unexpected internal error.
    Internal,
    /// Unknown error code.
    Unknown(i32),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Invalid => write!(f, "invalid argument"),
            Error::NotFound => write!(f, "path not declared"),
            Error::NoConnection => write!(f, "not connected"),
            Error::Timeout => write!(f, "operation timed out"),
            Error::Crypto => write!(f, "signature verification failed"),
            Error::Internal => write!(f, "internal error"),
            Error::Unknown(code) => write!(f, "unknown error ({})", code),
        }
    }
}

impl std::error::Error for Error {}

/// Result type alias.
pub type Result<T> = std::result::Result<T, Error>;

/// Convert a C API error code to a Rust Result.
pub(crate) fn check(code: i32) -> Result<()> {
    match code {
        0 => Ok(()),
        -1 => Err(Error::Invalid),
        -2 => Err(Error::NotFound),
        -3 => Err(Error::NoConnection),
        -4 => Err(Error::Timeout),
        -5 => Err(Error::Crypto),
        -99 => Err(Error::Internal),
        other => Err(Error::Unknown(other)),
    }
}
