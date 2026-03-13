namespace RMNunes.Rom;

/// <summary>Error codes returned by the ROM C API.</summary>
public enum ErrorCode
{
    Ok = 0,
    Invalid = -1,
    NotFound = -2,
    NoConnection = -3,
    Timeout = -4,
    Crypto = -5,
    Internal = -99,
}

/// <summary>Exception thrown when a ROM C API call fails.</summary>
public class ProtocollException : Exception
{
    public ErrorCode Code { get; }

    public ProtocollException(ErrorCode code)
        : base(GetMessage(code))
    {
        Code = code;
    }

    public ProtocollException(ErrorCode code, string message)
        : base(message)
    {
        Code = code;
    }

    internal static void ThrowIfError(int code)
    {
        if (code != 0)
            throw new ProtocollException((ErrorCode)code);
    }

    private static string GetMessage(ErrorCode code) => code switch
    {
        ErrorCode.Invalid => "Invalid argument",
        ErrorCode.NotFound => "Path not declared",
        ErrorCode.NoConnection => "Not connected",
        ErrorCode.Timeout => "Operation timed out",
        ErrorCode.Crypto => "Signature verification failed",
        ErrorCode.Internal => "Unexpected internal error",
        _ => $"ROM error: {(int)code}",
    };
}
