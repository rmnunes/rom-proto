using System.Runtime.InteropServices;
using System.Text;
using RMNunes.Rom.Interop;

namespace RMNunes.Rom;

/// <summary>ROM peer — high-level API for a protocoll endpoint.</summary>
public sealed class Peer : IDisposable
{
    private readonly PeerHandle _handle;
    private readonly Transport _transport;
    private bool _disposed;

    // Pin callback delegates to prevent GC collection
    private GCHandle _updateCallbackPin;
    private GCHandle _sigFailureCallbackPin;

    internal nint Ptr => _handle.DangerousGetHandle();

    /// <summary>Create a new peer with the given node ID, transport, and key pair.</summary>
    public Peer(ushort nodeId, Transport transport, KeyPair keys)
    {
        LibraryResolver.EnsureRegistered();
        _transport = transport;

        unsafe
        {
            PcolKeyPair native;
            fixed (byte* pk = keys.PublicKey, sk = keys.SecretKey)
            {
                Buffer.MemoryCopy(pk, native.PublicKey, 32, 32);
                Buffer.MemoryCopy(sk, native.SecretKey, 64, 64);
            }

            var ptr = NativeMethods.pcol_peer_create(nodeId, transport.Ptr, &native);
            if (ptr == 0) throw new ProtocollException(ErrorCode.Internal, "Failed to create peer");
            _handle = new PeerHandle(ptr);
        }
    }

    // --- Identity ---

    /// <summary>This peer's node ID.</summary>
    public ushort NodeId => NativeMethods.pcol_peer_node_id(Ptr);

    /// <summary>This peer's 32-byte Ed25519 public key.</summary>
    public byte[] PublicKey
    {
        get
        {
            unsafe
            {
                PcolPublicKey native;
                NativeMethods.pcol_peer_public_key(Ptr, &native);
                var result = new byte[32];
                fixed (byte* dst = result)
                    Buffer.MemoryCopy(native.Bytes, dst, 32, 32);
                return result;
            }
        }
    }

    /// <summary>Register a remote peer's public key for signature verification.</summary>
    public void RegisterPeerKey(ushort remoteNodeId, byte[] publicKey)
    {
        ArgumentNullException.ThrowIfNull(publicKey);
        if (publicKey.Length != 32)
            throw new ArgumentException("Public key must be 32 bytes", nameof(publicKey));

        unsafe
        {
            PcolPublicKey native;
            fixed (byte* src = publicKey)
                Buffer.MemoryCopy(src, native.Bytes, 32, 32);
            NativeMethods.pcol_peer_register_key(Ptr, remoteNodeId, &native);
        }
    }

    // --- Connection ---

    /// <summary>Connect to a remote peer (blocking).</summary>
    public void Connect(string address, ushort port)
    {
        var ep = NativeMethods.MakeEndpoint(address, port);
        try { ProtocollException.ThrowIfError(NativeMethods.pcol_peer_connect(Ptr, ep)); }
        finally { NativeMethods.FreeEndpoint(ref ep); }
    }

    /// <summary>Accept a connection from a remote peer (blocking).</summary>
    public void Accept(string address, ushort port, int timeoutMs = 5000)
    {
        var ep = NativeMethods.MakeEndpoint(address, port);
        try { ProtocollException.ThrowIfError(NativeMethods.pcol_peer_accept(Ptr, ep, timeoutMs)); }
        finally { NativeMethods.FreeEndpoint(ref ep); }
    }

    /// <summary>Whether this peer is connected.</summary>
    public bool IsConnected => NativeMethods.pcol_peer_is_connected(Ptr) != 0;

    /// <summary>Disconnect from the remote peer.</summary>
    public void Disconnect() => NativeMethods.pcol_peer_disconnect(Ptr);

    /// <summary>Set the local endpoint for this peer.</summary>
    public void SetLocalEndpoint(string address, ushort port)
    {
        var ep = NativeMethods.MakeEndpoint(address, port);
        try { NativeMethods.pcol_peer_set_local_endpoint(Ptr, ep); }
        finally { NativeMethods.FreeEndpoint(ref ep); }
    }

    // --- Non-blocking connection ---

    /// <summary>Start a non-blocking connection handshake.</summary>
    public void ConnectStart(string address, ushort port)
    {
        var ep = NativeMethods.MakeEndpoint(address, port);
        try { ProtocollException.ThrowIfError(NativeMethods.pcol_peer_connect_start(Ptr, ep)); }
        finally { NativeMethods.FreeEndpoint(ref ep); }
    }

    /// <summary>Poll for connection completion. Returns true when connected.</summary>
    public bool ConnectPoll() => NativeMethods.pcol_peer_connect_poll(Ptr) == 0;

    /// <summary>Start accepting connections (non-blocking).</summary>
    public void AcceptStart()
    {
        ProtocollException.ThrowIfError(NativeMethods.pcol_peer_accept_start(Ptr));
    }

    /// <summary>Poll for accept completion. Returns true when connected.</summary>
    public bool AcceptPoll() => NativeMethods.pcol_peer_accept_poll(Ptr) == 0;

    // --- State declaration ---

    /// <summary>Declare a CRDT state path.</summary>
    public void Declare(string path, CrdtType crdtType, Reliability reliability = Reliability.Reliable)
    {
        ProtocollException.ThrowIfError(
            NativeMethods.pcol_declare(Ptr, path, (int)crdtType, (int)reliability));
    }

    // --- State mutation ---

    /// <summary>Set the value of an LWW register.</summary>
    public void SetLww(string path, ReadOnlySpan<byte> data)
    {
        unsafe
        {
            fixed (byte* ptr = data)
            {
                ProtocollException.ThrowIfError(
                    NativeMethods.pcol_set_lww(Ptr, path, ptr, (nuint)data.Length));
            }
        }
    }

    /// <summary>Increment a counter.</summary>
    public void IncrementCounter(string path, ulong amount = 1)
    {
        ProtocollException.ThrowIfError(
            NativeMethods.pcol_increment_counter(Ptr, path, amount));
    }

    // --- State reading ---

    /// <summary>Read the current value of an LWW register.</summary>
    public byte[] GetLww(string path)
    {
        unsafe
        {
            const int bufSize = 4096;
            var buf = stackalloc byte[bufSize];
            nuint outLen;
            ProtocollException.ThrowIfError(
                NativeMethods.pcol_get_lww(Ptr, path, buf, (nuint)bufSize, &outLen));

            var result = new byte[outLen];
            fixed (byte* dst = result)
                Buffer.MemoryCopy(buf, dst, (long)outLen, (long)outLen);
            return result;
        }
    }

    /// <summary>Read the current value of a counter.</summary>
    public ulong GetCounter(string path)
    {
        unsafe
        {
            ulong value;
            ProtocollException.ThrowIfError(
                NativeMethods.pcol_get_counter(Ptr, path, &value));
            return value;
        }
    }

    // --- Network I/O ---

    /// <summary>Flush pending deltas (sign + encode + send). Returns number of frames sent.</summary>
    public int Flush()
    {
        var result = NativeMethods.pcol_flush(Ptr);
        if (result < 0) ProtocollException.ThrowIfError(result);
        return result;
    }

    /// <summary>Poll for incoming data (receive + verify + decode + merge). Returns number of state changes applied.</summary>
    public int Poll(int timeoutMs = 0)
    {
        var result = NativeMethods.pcol_poll(Ptr, timeoutMs);
        if (result < 0) ProtocollException.ThrowIfError(result);
        return result;
    }

    // --- Access control ---

    /// <summary>Enable or disable access control.</summary>
    public void SetAccessControl(bool enabled)
    {
        NativeMethods.pcol_set_access_control(Ptr, enabled ? 1 : 0);
    }

    // --- Multi-connection (mesh topology) ---

    /// <summary>Connect to a specific remote node.</summary>
    public void ConnectTo(ushort remoteNodeId, string address, ushort port)
    {
        var ep = NativeMethods.MakeEndpoint(address, port);
        try { ProtocollException.ThrowIfError(NativeMethods.pcol_peer_connect_to(Ptr, remoteNodeId, ep)); }
        finally { NativeMethods.FreeEndpoint(ref ep); }
    }

    /// <summary>Accept a connection from a specific remote node.</summary>
    public void AcceptNode(ushort remoteNodeId, string address, ushort port, int timeoutMs = 5000)
    {
        var ep = NativeMethods.MakeEndpoint(address, port);
        try { ProtocollException.ThrowIfError(NativeMethods.pcol_peer_accept_node(Ptr, remoteNodeId, ep, timeoutMs)); }
        finally { NativeMethods.FreeEndpoint(ref ep); }
    }

    /// <summary>Disconnect from a specific remote node.</summary>
    public void DisconnectNode(ushort remoteNodeId) =>
        NativeMethods.pcol_peer_disconnect_node(Ptr, remoteNodeId);

    /// <summary>Check if connected to a specific remote node.</summary>
    public bool IsConnectedTo(ushort remoteNodeId) =>
        NativeMethods.pcol_peer_is_connected_to(Ptr, remoteNodeId) != 0;

    // --- Resolution tiers ---

    /// <summary>Set the resolution tier for synchronization with a specific node.</summary>
    public void SetResolution(ushort remoteNodeId, ResolutionTier tier) =>
        NativeMethods.pcol_peer_set_resolution(Ptr, remoteNodeId, (int)tier);

    // --- Routing ---

    /// <summary>Announce a route so other peers can discover this path through us.</summary>
    public void AnnounceRoute(string path) =>
        NativeMethods.pcol_peer_announce_route(Ptr, path);

    /// <summary>Learn a route to a path via a specific node.</summary>
    public void LearnRoute(uint pathHash, ushort viaNodeId) =>
        NativeMethods.pcol_peer_learn_route(Ptr, pathHash, viaNodeId);

    /// <summary>Check if a route exists for a given path hash.</summary>
    public bool HasRoute(uint pathHash) =>
        NativeMethods.pcol_peer_has_route(Ptr, pathHash) != 0;

    // --- Subscriptions ---

    /// <summary>Subscribe to a pattern with a specific resolution tier. Returns subscription ID.</summary>
    public int SubscribeWithResolution(string pattern, ResolutionTier tier, int initialCredits = -1, uint freshnessUs = 0)
    {
        var result = NativeMethods.pcol_subscribe_with_resolution(Ptr, pattern, (int)tier, initialCredits, freshnessUs);
        if (result < 0) ProtocollException.ThrowIfError(result);
        return result;
    }

    // --- Callbacks ---

    [UnmanagedCallersOnly]
    private static unsafe void UpdateCallbackTrampoline(nint path, byte* data, nuint dataLen, nint userData)
    {
        var handle = GCHandle.FromIntPtr(userData);
        if (handle.Target is Action<string, byte[]> callback)
        {
            var pathStr = Marshal.PtrToStringUTF8(path) ?? "";
            var dataArr = new byte[dataLen];
            fixed (byte* dst = dataArr)
                Buffer.MemoryCopy(data, dst, (long)dataLen, (long)dataLen);
            callback(pathStr, dataArr);
        }
    }

    /// <summary>Register a callback for state updates.</summary>
    public void OnUpdate(Action<string, byte[]> callback)
    {
        if (_updateCallbackPin.IsAllocated) _updateCallbackPin.Free();
        _updateCallbackPin = GCHandle.Alloc(callback);

        unsafe
        {
            NativeMethods.pcol_on_update(Ptr,
                (nint)(delegate* unmanaged<nint, byte*, nuint, nint, void>)&UpdateCallbackTrampoline,
                GCHandle.ToIntPtr(_updateCallbackPin));
        }
    }

    [UnmanagedCallersOnly]
    private static void SigFailureCallbackTrampoline(ushort authorId, uint pathHash, nint userData)
    {
        var handle = GCHandle.FromIntPtr(userData);
        if (handle.Target is Action<ushort, uint> callback)
        {
            callback(authorId, pathHash);
        }
    }

    /// <summary>Register a callback for signature verification failures.</summary>
    public void OnSignatureFailure(Action<ushort, uint> callback)
    {
        if (_sigFailureCallbackPin.IsAllocated) _sigFailureCallbackPin.Free();
        _sigFailureCallbackPin = GCHandle.Alloc(callback);

        unsafe
        {
            NativeMethods.pcol_on_signature_failure(Ptr,
                (nint)(delegate* unmanaged<ushort, uint, nint, void>)&SigFailureCallbackTrampoline,
                GCHandle.ToIntPtr(_sigFailureCallbackPin));
        }
    }

    // --- API version ---

    /// <summary>Get the packed API version: (major &lt;&lt; 16) | (minor &lt;&lt; 8) | patch.</summary>
    public static uint ApiVersion
    {
        get
        {
            LibraryResolver.EnsureRegistered();
            return NativeMethods.pcol_api_version();
        }
    }

    public void Dispose()
    {
        if (!_disposed)
        {
            if (_updateCallbackPin.IsAllocated) _updateCallbackPin.Free();
            if (_sigFailureCallbackPin.IsAllocated) _sigFailureCallbackPin.Free();
            _handle.Dispose();
            _disposed = true;
        }
    }
}
