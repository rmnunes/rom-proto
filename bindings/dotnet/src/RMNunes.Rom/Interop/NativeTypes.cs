using System.Runtime.InteropServices;

namespace RMNunes.Rom.Interop;

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct PcolKeyPair
{
    public fixed byte PublicKey[32];
    public fixed byte SecretKey[64];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct PcolPublicKey
{
    public fixed byte Bytes[32];
}

/// <summary>
/// Matches C struct: { const char* address; uint16_t port; }
/// The address pointer must be pinned or allocated from unmanaged memory
/// for the duration of the native call.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
internal struct PcolEndpoint
{
    public nint Address; // const char*
    public ushort Port;
}
