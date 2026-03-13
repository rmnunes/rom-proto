using System.Runtime.InteropServices;

namespace RMNunes.Rom.Interop;

internal sealed class TransportHandle : SafeHandle
{
    public TransportHandle() : base(0, ownsHandle: true) { }

    public TransportHandle(nint ptr) : base(0, ownsHandle: true)
    {
        SetHandle(ptr);
    }

    public override bool IsInvalid => handle == 0;

    protected override bool ReleaseHandle()
    {
        NativeMethods.pcol_transport_destroy(handle);
        return true;
    }
}
