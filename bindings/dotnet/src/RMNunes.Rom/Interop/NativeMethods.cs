using System.Runtime.InteropServices;

namespace RMNunes.Rom.Interop;

internal static partial class NativeMethods
{
    private const string LibName = "protocoll";

    // --- Key generation ---

    [LibraryImport(LibName)]
    internal static unsafe partial void pcol_generate_keypair(PcolKeyPair* @out);

    // --- Transport ---

    [LibraryImport(LibName)]
    internal static partial nint pcol_transport_loopback_create(uint busId);

    [LibraryImport(LibName)]
    internal static partial nint pcol_transport_udp_create();

    [LibraryImport(LibName)]
    internal static partial int pcol_transport_bind(nint transport, PcolEndpoint ep);

    [LibraryImport(LibName)]
    internal static partial void pcol_transport_destroy(nint transport);

    // --- External transport ---

    [LibraryImport(LibName)]
    internal static partial nint pcol_transport_external_create();

    [LibraryImport(LibName)]
    internal static unsafe partial int pcol_transport_external_push_recv(
        nint transport, byte* data, nuint len, nint fromAddr, ushort fromPort);

    [LibraryImport(LibName)]
    internal static unsafe partial int pcol_transport_external_pop_send(
        nint transport, byte* buf, nuint bufLen, nuint* outLen,
        byte* toAddrBuf, nuint toAddrBufLen, ushort* toPort);

    [LibraryImport(LibName)]
    internal static partial nuint pcol_transport_external_send_queue_size(nint transport);

    // --- Peer lifecycle ---

    [LibraryImport(LibName)]
    internal static unsafe partial nint pcol_peer_create(ushort nodeId, nint transport, PcolKeyPair* keys);

    [LibraryImport(LibName)]
    internal static partial void pcol_peer_destroy(nint peer);

    // --- Identity ---

    [LibraryImport(LibName)]
    internal static partial ushort pcol_peer_node_id(nint peer);

    [LibraryImport(LibName)]
    internal static unsafe partial void pcol_peer_public_key(nint peer, PcolPublicKey* @out);

    [LibraryImport(LibName)]
    internal static unsafe partial void pcol_peer_register_key(nint peer, ushort remoteNodeId, PcolPublicKey* pk);

    // --- Connection ---

    [LibraryImport(LibName)]
    internal static partial int pcol_peer_connect(nint peer, PcolEndpoint remote);

    [LibraryImport(LibName)]
    internal static partial int pcol_peer_accept(nint peer, PcolEndpoint expectedFrom, int timeoutMs);

    [LibraryImport(LibName)]
    internal static partial int pcol_peer_is_connected(nint peer);

    [LibraryImport(LibName)]
    internal static partial void pcol_peer_disconnect(nint peer);

    [LibraryImport(LibName)]
    internal static partial void pcol_peer_set_local_endpoint(nint peer, PcolEndpoint ep);

    // --- Non-blocking connection ---

    [LibraryImport(LibName)]
    internal static partial int pcol_peer_connect_start(nint peer, PcolEndpoint remote);

    [LibraryImport(LibName)]
    internal static partial int pcol_peer_connect_poll(nint peer);

    [LibraryImport(LibName)]
    internal static partial int pcol_peer_accept_start(nint peer);

    [LibraryImport(LibName)]
    internal static partial int pcol_peer_accept_poll(nint peer);

    // --- State declaration ---

    [LibraryImport(LibName, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pcol_declare(nint peer, string path, int crdtType, int reliability);

    // --- State mutation ---

    [LibraryImport(LibName, StringMarshalling = StringMarshalling.Utf8)]
    internal static unsafe partial int pcol_set_lww(nint peer, string path, byte* data, nuint len);

    [LibraryImport(LibName, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pcol_increment_counter(nint peer, string path, ulong amount);

    // --- State reading ---

    [LibraryImport(LibName, StringMarshalling = StringMarshalling.Utf8)]
    internal static unsafe partial int pcol_get_lww(nint peer, string path, byte* buf, nuint bufLen, nuint* outLen);

    [LibraryImport(LibName, StringMarshalling = StringMarshalling.Utf8)]
    internal static unsafe partial int pcol_get_counter(nint peer, string path, ulong* outValue);

    // --- Network I/O ---

    [LibraryImport(LibName)]
    internal static partial int pcol_flush(nint peer);

    [LibraryImport(LibName)]
    internal static partial int pcol_poll(nint peer, int timeoutMs);

    // --- Callbacks ---

    [LibraryImport(LibName)]
    internal static partial void pcol_on_update(nint peer, nint cb, nint userData);

    [LibraryImport(LibName)]
    internal static partial void pcol_on_signature_failure(nint peer, nint cb, nint userData);

    // --- Access control ---

    [LibraryImport(LibName)]
    internal static partial void pcol_set_access_control(nint peer, int enabled);

    // --- Multi-connection ---

    [LibraryImport(LibName)]
    internal static partial int pcol_peer_connect_to(nint peer, ushort remoteNodeId, PcolEndpoint remote);

    [LibraryImport(LibName)]
    internal static partial int pcol_peer_accept_node(nint peer, ushort remoteNodeId, PcolEndpoint expectedFrom, int timeoutMs);

    [LibraryImport(LibName)]
    internal static partial void pcol_peer_disconnect_node(nint peer, ushort remoteNodeId);

    [LibraryImport(LibName)]
    internal static partial int pcol_peer_is_connected_to(nint peer, ushort remoteNodeId);

    // --- Resolution tiers ---

    [LibraryImport(LibName)]
    internal static partial void pcol_peer_set_resolution(nint peer, ushort remoteNodeId, int tier);

    // --- Routing ---

    [LibraryImport(LibName, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial void pcol_peer_announce_route(nint peer, string path);

    [LibraryImport(LibName)]
    internal static partial void pcol_peer_learn_route(nint peer, uint pathHash, ushort viaNode);

    [LibraryImport(LibName)]
    internal static partial int pcol_peer_has_route(nint peer, uint pathHash);

    // --- Subscriptions ---

    [LibraryImport(LibName, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int pcol_subscribe_with_resolution(nint peer, string pattern, int tier, int initialCredits, uint freshnessUs);

    // --- API version ---

    [LibraryImport(LibName)]
    internal static partial uint pcol_api_version();

    // --- Helper: create PcolEndpoint with pinned string ---

    /// <summary>
    /// Allocates a UTF-8 string on the unmanaged heap and builds a PcolEndpoint.
    /// Caller must free the Address pointer via Marshal.FreeHGlobal().
    /// </summary>
    internal static PcolEndpoint MakeEndpoint(string address, ushort port)
    {
        return new PcolEndpoint
        {
            Address = Marshal.StringToHGlobalAnsi(address), // UTF-8 compatible for ASCII addresses
            Port = port
        };
    }

    internal static void FreeEndpoint(ref PcolEndpoint ep)
    {
        if (ep.Address != 0)
        {
            Marshal.FreeHGlobal(ep.Address);
            ep.Address = 0;
        }
    }
}
