using System.Text;
using RMNunes.Rom;

namespace RMNunes.Rom.Tests;

public class PeerTests
{
    private static (Peer peer, Transport transport) CreateTestPeer(ushort nodeId, uint busId)
    {
        var keys = KeyPair.Generate();
        var transport = Transport.CreateLoopback(busId);
        transport.Bind("loopback", nodeId);
        var peer = new Peer(nodeId, transport, keys);
        return (peer, transport);
    }

    [Fact]
    public void NodeId_ReturnsCorrectValue()
    {
        var (peer, transport) = CreateTestPeer(42, 1000);
        using (peer) using (transport)
        {
            Assert.Equal(42, peer.NodeId);
        }
    }

    [Fact]
    public void PublicKey_Returns32Bytes()
    {
        var (peer, transport) = CreateTestPeer(1, 1001);
        using (peer) using (transport)
        {
            var pk = peer.PublicKey;
            Assert.Equal(32, pk.Length);
            Assert.Contains(pk, b => b != 0);
        }
    }

    [Fact]
    public void Declare_LwwRegister_Succeeds()
    {
        var (peer, transport) = CreateTestPeer(1, 1002);
        using (peer) using (transport)
        {
            peer.Declare("/test/value", CrdtType.LwwRegister);
        }
    }

    [Fact]
    public void Declare_GCounter_Succeeds()
    {
        var (peer, transport) = CreateTestPeer(1, 1003);
        using (peer) using (transport)
        {
            peer.Declare("/test/counter", CrdtType.GCounter);
        }
    }

    [Fact]
    public void SetLww_And_GetLww_RoundTrip()
    {
        var (peer, transport) = CreateTestPeer(1, 1004);
        using (peer) using (transport)
        {
            peer.Declare("/test/name", CrdtType.LwwRegister);

            var data = Encoding.UTF8.GetBytes("hello world");
            peer.SetLww("/test/name", data);

            var result = peer.GetLww("/test/name");
            Assert.Equal("hello world", Encoding.UTF8.GetString(result));
        }
    }

    [Fact]
    public void IncrementCounter_And_GetCounter()
    {
        var (peer, transport) = CreateTestPeer(1, 1005);
        using (peer) using (transport)
        {
            peer.Declare("/test/clicks", CrdtType.GCounter);

            peer.IncrementCounter("/test/clicks", 5);
            Assert.Equal(5UL, peer.GetCounter("/test/clicks"));

            peer.IncrementCounter("/test/clicks", 3);
            Assert.Equal(8UL, peer.GetCounter("/test/clicks"));
        }
    }

    [Fact]
    public void GetLww_UndeclaredPath_Throws()
    {
        var (peer, transport) = CreateTestPeer(1, 1006);
        using (peer) using (transport)
        {
            var ex = Assert.Throws<ProtocollException>(() => peer.GetLww("/nonexistent"));
            Assert.Equal(ErrorCode.NotFound, ex.Code);
        }
    }

    [Fact]
    public void IsConnected_InitiallyFalse()
    {
        var (peer, transport) = CreateTestPeer(1, 1007);
        using (peer) using (transport)
        {
            Assert.False(peer.IsConnected);
        }
    }

    [Fact]
    public void ApiVersion_ReturnsNonZero()
    {
        var version = Peer.ApiVersion;
        Assert.NotEqual(0u, version);
    }
}
