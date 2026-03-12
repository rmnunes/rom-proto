"""State-related types and error handling."""

from enum import IntEnum


class CrdtType(IntEnum):
    """CRDT type for a state region."""
    LWW_REGISTER = 0
    G_COUNTER = 1
    PN_COUNTER = 2
    OR_SET = 3


class Reliability(IntEnum):
    """Reliability level for state propagation."""
    RELIABLE = 0
    BEST_EFFORT = 1


class ResolutionTier(IntEnum):
    """Resolution tier controlling how much detail is synced for a node."""
    FULL = 0
    NORMAL = 1
    COARSE = 2
    METADATA = 3


class ProtocollError(Exception):
    """Base exception for protocoll errors."""
    pass


class InvalidError(ProtocollError):
    """Invalid argument."""
    pass


class NotFoundError(ProtocollError):
    """Path not declared."""
    pass


class NoConnectionError(ProtocollError):
    """Not connected."""
    pass


class TimeoutError(ProtocollError):
    """Operation timed out."""
    pass


class CryptoError(ProtocollError):
    """Signature verification failed."""
    pass


_ERROR_MAP = {
    -1: InvalidError,
    -2: NotFoundError,
    -3: NoConnectionError,
    -4: TimeoutError,
    -5: CryptoError,
}


def _check_error(code: int):
    """Convert a C API error code to a Python exception."""
    if code == 0:
        return
    exc_class = _ERROR_MAP.get(code, ProtocollError)
    raise exc_class(f"protocoll error code: {code}")
