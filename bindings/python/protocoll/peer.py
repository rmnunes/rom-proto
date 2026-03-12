"""Peer: high-level API for a protocoll endpoint."""

from protocoll._ffi import ffi, lib
from protocoll.state import CrdtType, Reliability, ResolutionTier, _check_error
from protocoll.transport import Transport


class Peer:
    """A protocoll peer endpoint.

    Usage as context manager:
        with Peer(node_id=1, transport=t, keys=keys) as peer:
            peer.declare("/path", CrdtType.LWW_REGISTER)
            ...
    """

    def __init__(self, node_id: int, transport: Transport, keys):
        self._transport = transport
        self._raw = lib.pcol_peer_create(node_id, transport._raw, keys._raw)
        if self._raw == ffi.NULL:
            raise RuntimeError("Failed to create peer")
        # Prevent transport from being destroyed independently
        self._owns_transport = True

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.destroy()

    def destroy(self):
        """Destroy the peer (disconnects if connected)."""
        if self._raw != ffi.NULL:
            lib.pcol_peer_destroy(self._raw)
            self._raw = ffi.NULL
        if self._owns_transport:
            self._transport.close()

    @property
    def node_id(self) -> int:
        """Get this peer's node ID."""
        return lib.pcol_peer_node_id(self._raw)

    @property
    def public_key(self) -> bytes:
        """Get this peer's public key."""
        pk = ffi.new("PcolPublicKey *")
        lib.pcol_peer_public_key(self._raw, pk)
        return bytes(ffi.buffer(pk.bytes, 32))

    def register_peer_key(self, remote_node_id: int, public_key_bytes: bytes):
        """Register a remote peer's public key for signature verification."""
        pk = ffi.new("PcolPublicKey *")
        ffi.memmove(pk.bytes, public_key_bytes, 32)
        lib.pcol_peer_register_key(self._raw, remote_node_id, pk)

    def set_local_endpoint(self, address: str, port: int):
        """Set the local endpoint for this peer."""
        ep = ffi.new("PcolEndpoint *")
        self._local_addr = ffi.new("char[]", address.encode())
        ep.address = self._local_addr
        ep.port = port
        lib.pcol_peer_set_local_endpoint(self._raw, ep[0])

    def connect(self, address: str, port: int):
        """Connect to a remote peer."""
        ep = ffi.new("PcolEndpoint *")
        addr_buf = ffi.new("char[]", address.encode())
        ep.address = addr_buf
        ep.port = port
        _check_error(lib.pcol_peer_connect(self._raw, ep[0]))

    def accept(self, address: str, port: int, timeout_ms: int = 5000):
        """Accept a connection from a remote peer."""
        ep = ffi.new("PcolEndpoint *")
        addr_buf = ffi.new("char[]", address.encode())
        ep.address = addr_buf
        ep.port = port
        _check_error(lib.pcol_peer_accept(self._raw, ep[0], timeout_ms))

    @property
    def is_connected(self) -> bool:
        """Check if connected to a remote peer."""
        return lib.pcol_peer_is_connected(self._raw) != 0

    def disconnect(self):
        """Disconnect from the remote peer."""
        lib.pcol_peer_disconnect(self._raw)

    def declare(self, path: str, crdt_type: CrdtType,
                reliability: Reliability = Reliability.RELIABLE):
        """Declare a state region with a CRDT type."""
        path_bytes = path.encode()
        _check_error(lib.pcol_declare(
            self._raw, path_bytes, int(crdt_type), int(reliability)))

    def set_lww(self, path: str, data: bytes):
        """Set an LWW register value."""
        path_bytes = path.encode()
        _check_error(lib.pcol_set_lww(self._raw, path_bytes, data, len(data)))

    def get_lww(self, path: str) -> bytes:
        """Read an LWW register value."""
        path_bytes = path.encode()
        buf = ffi.new("uint8_t[4096]")
        out_len = ffi.new("size_t *")
        _check_error(lib.pcol_get_lww(self._raw, path_bytes, buf, 4096, out_len))
        return bytes(ffi.buffer(buf, out_len[0]))

    def increment_counter(self, path: str, amount: int = 1):
        """Increment a counter."""
        path_bytes = path.encode()
        _check_error(lib.pcol_increment_counter(self._raw, path_bytes, amount))

    def get_counter(self, path: str) -> int:
        """Read a counter value."""
        path_bytes = path.encode()
        out = ffi.new("uint64_t *")
        _check_error(lib.pcol_get_counter(self._raw, path_bytes, out))
        return out[0]

    def flush(self) -> int:
        """Flush pending deltas (sign + encode + send). Returns frames sent."""
        result = lib.pcol_flush(self._raw)
        if result < 0:
            _check_error(result)
        return result

    def poll(self, timeout_ms: int = 0) -> int:
        """Poll for incoming data. Returns state changes applied."""
        result = lib.pcol_poll(self._raw, timeout_ms)
        if result < 0:
            _check_error(result)
        return result

    def set_access_control(self, enabled: bool):
        """Enable or disable access control."""
        lib.pcol_set_access_control(self._raw, 1 if enabled else 0)

    # --- Multi-connection ---

    def connect_to(self, remote_node_id: int, address: str, port: int):
        """Connect to a specific remote node by its node ID."""
        ep = ffi.new("PcolEndpoint *")
        addr_buf = ffi.new("char[]", address.encode())
        ep.address = addr_buf
        ep.port = port
        _check_error(lib.pcol_peer_connect_to(self._raw, remote_node_id, ep[0]))

    def accept_node(self, remote_node_id: int, address: str, port: int,
                    timeout_ms: int = 5000):
        """Accept a connection from a specific remote node."""
        ep = ffi.new("PcolEndpoint *")
        addr_buf = ffi.new("char[]", address.encode())
        ep.address = addr_buf
        ep.port = port
        _check_error(lib.pcol_peer_accept_node(
            self._raw, remote_node_id, ep[0], timeout_ms))

    def disconnect_node(self, remote_node_id: int):
        """Disconnect a specific remote node."""
        lib.pcol_peer_disconnect_node(self._raw, remote_node_id)

    def is_connected_to(self, remote_node_id: int) -> bool:
        """Check if connected to a specific remote node."""
        return lib.pcol_peer_is_connected_to(self._raw, remote_node_id) != 0

    # --- Resolution ---

    def set_resolution(self, remote_node_id: int, tier: ResolutionTier):
        """Set the resolution tier for a specific remote node."""
        lib.pcol_peer_set_resolution(self._raw, remote_node_id, int(tier))

    # --- Routing ---

    def announce_route(self, prefix: str):
        """Announce a route prefix to connected peers."""
        lib.pcol_peer_announce_route(self._raw, prefix.encode())

    def learn_route(self, path_hash: int, next_hop: int):
        """Manually add a routing entry mapping a path hash to a next-hop node."""
        lib.pcol_peer_learn_route(self._raw, path_hash, next_hop)

    def has_route(self, path_hash: int) -> bool:
        """Check whether a route exists for the given path hash."""
        return lib.pcol_peer_has_route(self._raw, path_hash) != 0

    # --- Subscriptions ---

    def subscribe_with_resolution(self, pattern: str, tier: ResolutionTier,
                                  initial_credits: int = -1,
                                  freshness_us: int = 0) -> int:
        """Subscribe to a pattern with a specific resolution tier.

        Returns the subscription ID.
        """
        pattern_bytes = pattern.encode()
        result = lib.pcol_subscribe_with_resolution(
            self._raw, pattern_bytes, int(tier), initial_credits, freshness_us)
        if result < 0:
            _check_error(result)
        return result
