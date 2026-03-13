using System.Reflection;
using System.Runtime.InteropServices;

namespace RMNunes.Rom.Interop;

internal static class LibraryResolver
{
    private static int _initialized;

    internal static void EnsureRegistered()
    {
        if (Interlocked.CompareExchange(ref _initialized, 1, 0) != 0)
            return;

        NativeLibrary.SetDllImportResolver(typeof(LibraryResolver).Assembly, Resolve);
    }

    private static nint Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (libraryName != "protocoll")
            return 0;

        // Check environment variable override (consistent with Python binding)
        var envPath = Environment.GetEnvironmentVariable("PROTOCOLL_LIB_PATH");
        if (!string.IsNullOrEmpty(envPath) && NativeLibrary.TryLoad(envPath, out var handle))
            return handle;

        // Fall back to default resolution (runtimes/{rid}/native/ from NuGet)
        return 0;
    }
}
