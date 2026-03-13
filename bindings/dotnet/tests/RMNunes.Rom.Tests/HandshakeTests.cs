using System.Text;
using RMNunes.Rom;

namespace RMNunes.Rom.Tests;

public class HandshakeTests
{
    [Fact]
    public async Task ConnectAndAccept_ViaLoopback()
    {
        var keysA = KeyPair.Generate();
        var keysB = KeyPair.Generate();

        using var tA = Transport.CreateLoopback(2000);
        using var tB = Transport.CreateLoopback(2000);
        tA.Bind("loopback", 1);
        tB.Bind("loopback", 2);

        using var peerA = new Peer(1, tA, keysA);
        using var peerB = new Peer(2, tB, keysB);

        // Exchange public keys
        peerA.RegisterPeerKey(2, keysB.PublicKey);
        peerB.RegisterPeerKey(1, keysA.PublicKey);

        // Connect in separate threads (accept blocks)
        var acceptTask = Task.Run(() => peerB.Accept("loopback", 1, timeoutMs: 5000));
        Thread.Sleep(50); // Give accept time to start
        peerA.Connect("loopback", 2);
        await acceptTask.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.True(peerA.IsConnected);
        Assert.True(peerB.IsConnected);
    }

    [Fact]
    public async Task StateExchange_ViaLoopback()
    {
        var keysA = KeyPair.Generate();
        var keysB = KeyPair.Generate();

        using var tA = Transport.CreateLoopback(2001);
        using var tB = Transport.CreateLoopback(2001);
        tA.Bind("loopback", 1);
        tB.Bind("loopback", 2);

        using var peerA = new Peer(1, tA, keysA);
        using var peerB = new Peer(2, tB, keysB);

        peerA.RegisterPeerKey(2, keysB.PublicKey);
        peerB.RegisterPeerKey(1, keysA.PublicKey);

        var acceptTask = Task.Run(() => peerB.Accept("loopback", 1, timeoutMs: 5000));
        Thread.Sleep(50);
        peerA.Connect("loopback", 2);
        await acceptTask.WaitAsync(TimeSpan.FromSeconds(5));

        // Both peers declare the same path
        peerA.Declare("/game/name", CrdtType.LwwRegister);
        peerB.Declare("/game/name", CrdtType.LwwRegister);

        // Peer A sets a value
        peerA.SetLww("/game/name", Encoding.UTF8.GetBytes("Alice"));
        peerA.Flush();

        // Peer B receives it
        peerB.Poll(100);
        var received = peerB.GetLww("/game/name");
        Assert.Equal("Alice", Encoding.UTF8.GetString(received));
    }

    [Fact]
    public async Task CounterConvergence_ViaLoopback()
    {
        var keysA = KeyPair.Generate();
        var keysB = KeyPair.Generate();

        using var tA = Transport.CreateLoopback(2002);
        using var tB = Transport.CreateLoopback(2002);
        tA.Bind("loopback", 1);
        tB.Bind("loopback", 2);

        using var peerA = new Peer(1, tA, keysA);
        using var peerB = new Peer(2, tB, keysB);

        peerA.RegisterPeerKey(2, keysB.PublicKey);
        peerB.RegisterPeerKey(1, keysA.PublicKey);

        var acceptTask = Task.Run(() => peerB.Accept("loopback", 1, timeoutMs: 5000));
        Thread.Sleep(50);
        peerA.Connect("loopback", 2);
        await acceptTask.WaitAsync(TimeSpan.FromSeconds(5));

        peerA.Declare("/app/clicks", CrdtType.GCounter);
        peerB.Declare("/app/clicks", CrdtType.GCounter);

        // Both peers increment
        peerA.IncrementCounter("/app/clicks", 3);
        peerB.IncrementCounter("/app/clicks", 7);

        // Exchange
        peerA.Flush();
        peerB.Flush();
        peerA.Poll(100);
        peerB.Poll(100);

        // Both should converge to 10
        Assert.Equal(10UL, peerA.GetCounter("/app/clicks"));
        Assert.Equal(10UL, peerB.GetCounter("/app/clicks"));
    }

    [Fact]
    public async Task Disconnect_ResetsConnectionState()
    {
        var keysA = KeyPair.Generate();
        var keysB = KeyPair.Generate();

        using var tA = Transport.CreateLoopback(2003);
        using var tB = Transport.CreateLoopback(2003);
        tA.Bind("loopback", 1);
        tB.Bind("loopback", 2);

        using var peerA = new Peer(1, tA, keysA);
        using var peerB = new Peer(2, tB, keysB);

        peerA.RegisterPeerKey(2, keysB.PublicKey);
        peerB.RegisterPeerKey(1, keysA.PublicKey);

        var acceptTask = Task.Run(() => peerB.Accept("loopback", 1, timeoutMs: 5000));
        Thread.Sleep(50);
        peerA.Connect("loopback", 2);
        await acceptTask.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.True(peerA.IsConnected);
        peerA.Disconnect();
        Assert.False(peerA.IsConnected);
    }
}
