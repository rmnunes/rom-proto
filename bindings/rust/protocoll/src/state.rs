//! State-related types for CRDT and reliability configuration.

/// CRDT type for a state region.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CrdtType {
    /// Last-Writer-Wins Register.
    LwwRegister,
    /// Grow-only Counter.
    GCounter,
    /// Positive-Negative Counter.
    PnCounter,
    /// Observed-Remove Set.
    OrSet,
}

impl CrdtType {
    pub(crate) fn to_raw(self) -> protocoll_sys::PcolCrdtType {
        match self {
            CrdtType::LwwRegister => protocoll_sys::PcolCrdtType_PCOL_LWW_REGISTER,
            CrdtType::GCounter => protocoll_sys::PcolCrdtType_PCOL_G_COUNTER,
            CrdtType::PnCounter => protocoll_sys::PcolCrdtType_PCOL_PN_COUNTER,
            CrdtType::OrSet => protocoll_sys::PcolCrdtType_PCOL_OR_SET,
        }
    }
}

/// Reliability level for state propagation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Reliability {
    /// Full ACK/retransmit.
    Reliable,
    /// Retransmit once, then drop.
    BestEffort,
}

impl Reliability {
    pub(crate) fn to_raw(self) -> protocoll_sys::PcolReliability {
        match self {
            Reliability::Reliable => protocoll_sys::PcolReliability_PCOL_RELIABLE,
            Reliability::BestEffort => protocoll_sys::PcolReliability_PCOL_BEST_EFFORT,
        }
    }
}

/// Resolution tier controlling how much detail is synced to a remote node.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ResolutionTier {
    /// Full resolution — all deltas.
    Full,
    /// Normal resolution.
    Normal,
    /// Coarse resolution — reduced update frequency.
    Coarse,
    /// Metadata only — no payload data.
    Metadata,
}

impl ResolutionTier {
    pub(crate) fn to_raw(self) -> protocoll_sys::PcolResolutionTier {
        match self {
            ResolutionTier::Full => protocoll_sys::PcolResolutionTier_PCOL_RESOLUTION_FULL,
            ResolutionTier::Normal => protocoll_sys::PcolResolutionTier_PCOL_RESOLUTION_NORMAL,
            ResolutionTier::Coarse => protocoll_sys::PcolResolutionTier_PCOL_RESOLUTION_COARSE,
            ResolutionTier::Metadata => protocoll_sys::PcolResolutionTier_PCOL_RESOLUTION_METADATA,
        }
    }
}
