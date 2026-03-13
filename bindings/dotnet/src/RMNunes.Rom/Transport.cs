using System.Runtime.InteropServices;
using RMNunes.Rom.Interop;

namespace RMNunes.Rom;

/// <summary>Network transport for ROM peers.</summary>
public sealed class Transport : IDisposable
{
    internal readonly TransportHandle Handle;
    private bool _disposed;

    internal Transport(TransportHandle handle)
    {
        Handle = handle;
    }

    internal nint Ptr => Handle.DangerousGetHandle();

    /// <summary>Create a loopback transport for testing / in-process use.</summary>
    /// <param name="busId">Bus ID grouping transports that can communicate.</param>
    public static Transport CreateLoopback(uint busId = 0)
    {
        LibraryResolver.EnsureRegistered();
        var ptr = NativeMethods.pcol_transport_loopback_create(busId);
        if (ptr == 0) throw new ProtocollException(ErrorCode.Internal, "Failed to create loopback transport");
        return new Transport(new TransportHandle(ptr));
    }

    /// <summary>Create a UDP transport for network communication.</summary>
    public static Transport CreateUdp()
    {
        LibraryResolver.EnsureRegistered();
        var ptr = NativeMethods.pcol_transport_udp_create();
        if (ptr == 0) throw new ProtocollException(ErrorCode.Internal, "Failed to create UDP transport");
        return new Transport(new TransportHandle(ptr));
    }

    /// <summary>Create an external transport for bridging (e.g., browser transports).</summary>
    public static Transport CreateExternal()
    {
        LibraryResolver.EnsureRegistered();
        var ptr = NativeMethods.pcol_transport_external_create();
        if (ptr == 0) throw new ProtocollException(ErrorCode.Internal, "Failed to create external transport");
        return new Transport(new TransportHandle(ptr));
    }

    /// <summary>Bind the transport to a local address and port.</summary>
    public void Bind(string address, ushort port)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var ep = NativeMethods.MakeEndpoint(address, port);
        try
        {
            ProtocollException.ThrowIfError(
                NativeMethods.pcol_transport_bind(Ptr, ep));
        }
        finally
        {
            NativeMethods.FreeEndpoint(ref ep);
        }
    }

    public void Dispose()
    {
        if (!_disposed)
        {
            Handle.Dispose();
            _disposed = true;
        }
    }
}
