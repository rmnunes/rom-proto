namespace RMNunes.Rom;

/// <summary>CRDT types supported by ROM.</summary>
public enum CrdtType
{
    /// <summary>Last-Writer-Wins Register — stores arbitrary bytes, latest write wins.</summary>
    LwwRegister = 0,

    /// <summary>Grow-only Counter — monotonically increasing.</summary>
    GCounter = 1,

    /// <summary>Positive-Negative Counter — supports increment and decrement.</summary>
    PnCounter = 2,

    /// <summary>Observed-Remove Set — elements can be added and removed.</summary>
    OrSet = 3,
}
