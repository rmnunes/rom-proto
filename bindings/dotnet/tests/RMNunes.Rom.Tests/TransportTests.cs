using RMNunes.Rom;

namespace RMNunes.Rom.Tests;

public class TransportTests
{
    [Fact]
    public void CreateLoopback_Succeeds()
    {
        using var transport = Transport.CreateLoopback(100);
        Assert.NotNull(transport);
    }

    [Fact]
    public void CreateLoopback_DifferentBusIds()
    {
        using var t1 = Transport.CreateLoopback(200);
        using var t2 = Transport.CreateLoopback(201);
        Assert.NotNull(t1);
        Assert.NotNull(t2);
    }

    [Fact]
    public void Bind_Succeeds()
    {
        using var transport = Transport.CreateLoopback(300);
        transport.Bind("loopback", 1);
    }

    [Fact]
    public void Dispose_CanBeCalledMultipleTimes()
    {
        var transport = Transport.CreateLoopback(400);
        transport.Dispose();
        transport.Dispose(); // Should not throw
    }

    [Fact]
    public void Bind_ThrowsAfterDispose()
    {
        var transport = Transport.CreateLoopback(500);
        transport.Dispose();
        Assert.Throws<ObjectDisposedException>(() => transport.Bind("loopback", 1));
    }
}
