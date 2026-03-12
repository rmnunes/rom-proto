"""
protocoll - Python bindings for the protocoll state streaming library.

Usage:
    from protocoll import Peer, Transport, KeyPair, CrdtType, Reliability

    keys = KeyPair.generate()
    transport = Transport.loopback(bus_id=1)
    transport.bind("127.0.0.1", 9000)

    with Peer(node_id=1, transport=transport, keys=keys) as peer:
        peer.declare("/game/pos", CrdtType.LWW_REGISTER, Reliability.RELIABLE)
        peer.set_lww("/game/pos", b"hello")
"""

from protocoll._ffi import ffi, lib
from protocoll.transport import Transport
from protocoll.peer import Peer
from protocoll.state import CrdtType, Reliability, ResolutionTier

__all__ = [
    "Transport",
    "Peer",
    "KeyPair",
    "CrdtType",
    "Reliability",
    "ResolutionTier",
    "api_version",
]


def api_version() -> int:
    """Get the packed API version (major<<16 | minor<<8 | patch)."""
    return lib.pcol_api_version()


class KeyPair:
    """Ed25519 key pair for signing state deltas."""

    def __init__(self, raw=None):
        if raw is None:
            self._raw = ffi.new("PcolKeyPair *")
            lib.pcol_generate_keypair(self._raw)
        else:
            self._raw = raw

    @classmethod
    def generate(cls):
        """Generate a new random Ed25519 key pair."""
        return cls()

    @property
    def public_key(self) -> bytes:
        """Get the public key as bytes."""
        return bytes(ffi.buffer(self._raw.public_key, 32))


class PublicKey:
    """Public key for verifying signatures from a remote peer."""

    def __init__(self, key_bytes: bytes):
        if len(key_bytes) != 32:
            raise ValueError("Public key must be exactly 32 bytes")
        self._raw = ffi.new("PcolPublicKey *")
        ffi.memmove(self._raw.bytes, key_bytes, 32)

    @property
    def bytes(self) -> bytes:
        return bytes(ffi.buffer(self._raw.bytes, 32))
