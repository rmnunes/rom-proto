namespace RMNunes.Rom;

/// <summary>Reliability mode for state paths.</summary>
public enum Reliability
{
    /// <summary>Full ACK/retransmit — guaranteed delivery.</summary>
    Reliable = 0,

    /// <summary>Retransmit once, then drop — best effort delivery.</summary>
    BestEffort = 1,
}
