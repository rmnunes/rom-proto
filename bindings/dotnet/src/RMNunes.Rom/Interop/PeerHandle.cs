using System.Runtime.InteropServices;

namespace RMNunes.Rom.Interop;

internal sealed class PeerHandle : SafeHandle
{
    public PeerHandle() : base(0, ownsHandle: true) { }

    public PeerHandle(nint ptr) : base(0, ownsHandle: true)
    {
        SetHandle(ptr);
    }

    public override bool IsInvalid => handle == 0;

    protected override bool ReleaseHandle()
    {
        NativeMethods.pcol_peer_destroy(handle);
        return true;
    }
}
