using RMNunes.Rom.Interop;

namespace RMNunes.Rom;

/// <summary>Ed25519 key pair for signing CRDT deltas.</summary>
public sealed class KeyPair
{
    /// <summary>32-byte Ed25519 public key.</summary>
    public byte[] PublicKey { get; }

    /// <summary>64-byte Ed25519 secret key.</summary>
    public byte[] SecretKey { get; }

    internal KeyPair(byte[] publicKey, byte[] secretKey)
    {
        PublicKey = publicKey;
        SecretKey = secretKey;
    }

    /// <summary>Generate a new random Ed25519 key pair.</summary>
    public static KeyPair Generate()
    {
        LibraryResolver.EnsureRegistered();

        unsafe
        {
            PcolKeyPair native;
            NativeMethods.pcol_generate_keypair(&native);

            var pk = new byte[32];
            var sk = new byte[64];
            fixed (byte* pkDst = pk, skDst = sk)
            {
                Buffer.MemoryCopy(native.PublicKey, pkDst, 32, 32);
                Buffer.MemoryCopy(native.SecretKey, skDst, 64, 64);
            }

            return new KeyPair(pk, sk);
        }
    }
}
