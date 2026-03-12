"""Basic tests for protocoll Python bindings."""

import pytest
import threading
import os
import sys

# Add parent dir to path so we can import protocoll
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from protocoll import KeyPair, Transport, Peer, CrdtType, Reliability
from protocoll import PublicKey
from protocoll.state import NotFoundError


class TestKeyPair:
    def test_generate(self):
        keys = KeyPair.generate()
        assert len(keys.public_key) == 32
        assert keys.public_key != b"\x00" * 32

    def test_two_keys_differ(self):
        k1 = KeyPair.generate()
        k2 = KeyPair.generate()
        assert k1.public_key != k2.public_key


class TestPublicKey:
    def test_roundtrip(self):
        keys = KeyPair.generate()
        pk = PublicKey(keys.public_key)
        assert pk.bytes == keys.public_key

    def test_invalid_length(self):
        with pytest.raises(ValueError):
            PublicKey(b"too_short")


class TestTransport:
    def test_loopback_create(self):
        t = Transport.loopback(bus_id=42)
        assert t._raw is not None

    def test_loopback_bind(self):
        t = Transport.loopback(bus_id=43)
        t.bind("127.0.0.1", 9100)

    def test_context_manager(self):
        with Transport.loopback(bus_id=44) as t:
            t.bind("127.0.0.1", 9101)


class TestPeer:
    def test_create(self):
        keys = KeyPair.generate()
        t = Transport.loopback(bus_id=50)
        peer = Peer(node_id=1, transport=t, keys=keys)
        assert peer.node_id == 1
        assert not peer.is_connected
        peer.destroy()

    def test_context_manager(self):
        keys = KeyPair.generate()
        t = Transport.loopback(bus_id=51)
        with Peer(node_id=2, transport=t, keys=keys) as peer:
            assert peer.node_id == 2

    def test_public_key(self):
        keys = KeyPair.generate()
        t = Transport.loopback(bus_id=52)
        with Peer(node_id=1, transport=t, keys=keys) as peer:
            assert peer.public_key == keys.public_key

    def test_declare_and_set_lww(self):
        keys = KeyPair.generate()
        t = Transport.loopback(bus_id=53)
        with Peer(node_id=1, transport=t, keys=keys) as peer:
            peer.declare("/test/val", CrdtType.LWW_REGISTER)
            peer.set_lww("/test/val", b"hello_python")
            val = peer.get_lww("/test/val")
            assert val == b"hello_python"

    def test_declare_and_increment_counter(self):
        keys = KeyPair.generate()
        t = Transport.loopback(bus_id=54)
        with Peer(node_id=1, transport=t, keys=keys) as peer:
            peer.declare("/test/count", CrdtType.G_COUNTER)
            peer.increment_counter("/test/count", 7)
            val = peer.get_counter("/test/count")
            assert val == 7

    def test_not_found_error(self):
        keys = KeyPair.generate()
        t = Transport.loopback(bus_id=55)
        with Peer(node_id=1, transport=t, keys=keys) as peer:
            with pytest.raises(NotFoundError):
                peer.set_lww("/nonexistent", b"data")

    def test_access_control(self):
        keys = KeyPair.generate()
        t = Transport.loopback(bus_id=56)
        with Peer(node_id=1, transport=t, keys=keys) as peer:
            peer.set_access_control(True)
            peer.set_access_control(False)


class TestHandshake:
    def test_loopback_handshake_and_state_exchange(self):
        """Full integration: handshake + state exchange over loopback."""
        bus_id = 300

        # Server
        keys_s = KeyPair.generate()
        t_s = Transport.loopback(bus_id)
        t_s.bind("127.0.0.1", 9200)
        server = Peer(node_id=1, transport=t_s, keys=keys_s)
        server.set_local_endpoint("127.0.0.1", 9200)

        # Client
        keys_c = KeyPair.generate()
        t_c = Transport.loopback(bus_id)
        t_c.bind("127.0.0.1", 9201)
        client = Peer(node_id=2, transport=t_c, keys=keys_c)
        client.set_local_endpoint("127.0.0.1", 9201)

        # Exchange keys
        server.register_peer_key(2, keys_c.public_key)
        client.register_peer_key(1, keys_s.public_key)

        # Declare state
        client.declare("/test/val", CrdtType.LWW_REGISTER)
        server.declare("/test/val", CrdtType.LWW_REGISTER)

        # Connect in thread (blocking)
        def do_connect():
            client.connect("127.0.0.1", 9200)

        t = threading.Thread(target=do_connect)
        t.start()

        server.accept("127.0.0.1", 9201, timeout_ms=5000)
        t.join(timeout=10)

        assert client.is_connected
        assert server.is_connected

        # Client writes, server reads
        client.set_lww("/test/val", b"from_python")
        client.flush()

        server.poll(timeout_ms=100)
        val = server.get_lww("/test/val")
        assert val == b"from_python"

        # Cleanup
        client.destroy()
        server.destroy()
