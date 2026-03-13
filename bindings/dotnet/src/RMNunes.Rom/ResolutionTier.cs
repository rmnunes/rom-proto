namespace RMNunes.Rom;

/// <summary>Multi-resolution propagation tiers.</summary>
public enum ResolutionTier
{
    /// <summary>All deltas — full fidelity.</summary>
    Full = 0,

    /// <summary>Normal resolution.</summary>
    Normal = 1,

    /// <summary>Reduced update frequency.</summary>
    Coarse = 2,

    /// <summary>Metadata only, no payload.</summary>
    Metadata = 3,
}
