using RMNunes.Rom;

namespace RMNunes.Rom.Tests;

public class KeyPairTests
{
    [Fact]
    public void Generate_ReturnsValidKeyPair()
    {
        var kp = KeyPair.Generate();

        Assert.NotNull(kp.PublicKey);
        Assert.NotNull(kp.SecretKey);
        Assert.Equal(32, kp.PublicKey.Length);
        Assert.Equal(64, kp.SecretKey.Length);
    }

    [Fact]
    public void Generate_ProducesDifferentKeys()
    {
        var kp1 = KeyPair.Generate();
        var kp2 = KeyPair.Generate();

        // Public keys should differ (statistically guaranteed)
        Assert.False(kp1.PublicKey.AsSpan().SequenceEqual(kp2.PublicKey));
    }

    [Fact]
    public void Generate_KeysAreNonZero()
    {
        var kp = KeyPair.Generate();

        // At least some bytes should be non-zero
        Assert.Contains(kp.PublicKey, b => b != 0);
        Assert.Contains(kp.SecretKey, b => b != 0);
    }
}
