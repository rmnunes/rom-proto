"""Transport abstraction for network communication."""

from protocoll._ffi import ffi, lib
from protocoll.state import _check_error


class Transport:
    """Network transport handle (owns the underlying C resource)."""

    def __init__(self, raw):
        self._raw = raw
        self._closed = False

    @classmethod
    def loopback(cls, bus_id: int = 0):
        """Create a loopback transport for testing.

        Transports with the same bus_id can communicate with each other.
        """
        raw = lib.pcol_transport_loopback_create(bus_id)
        if raw == ffi.NULL:
            raise RuntimeError("Failed to create loopback transport")
        return cls(raw)

    @classmethod
    def udp(cls):
        """Create a UDP transport."""
        raw = lib.pcol_transport_udp_create()
        if raw == ffi.NULL:
            raise RuntimeError("Failed to create UDP transport")
        return cls(raw)

    def bind(self, address: str, port: int):
        """Bind to a local address and port."""
        ep = ffi.new("PcolEndpoint *")
        self._addr_buf = ffi.new("char[]", address.encode())
        ep.address = self._addr_buf
        ep.port = port
        _check_error(lib.pcol_transport_bind(self._raw, ep[0]))

    def close(self):
        """Close and destroy the transport."""
        if not self._closed and self._raw != ffi.NULL:
            lib.pcol_transport_destroy(self._raw)
            self._closed = True

    def __del__(self):
        # Don't destroy if owned by a Peer
        pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
