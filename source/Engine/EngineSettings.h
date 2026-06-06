#pragma once

struct EngineSettings {
    // -- Aftertouch Choke --
    float chokeMaxDuckDepth = 0.7f;          // max volume duck (0..1)
    float chokeDuckTimeSec = 0.15f;          // duck ramp duration (sec)
    float chokeReleaseTimeSec = 0.05f;       // release ramp duration (sec)
    float chokeMuteTailSec = 0.04f;          // mute tail duration (sec)
    float chokeReleaseBoost = 0.1f;          // boost on aftertouch release (0..0.5)
    float chokeMuteThresholdRatio = 0.5f;    // mute trigger threshold (% of maxPA)

    // -- Tail Shortening --
    float tailShortenLoadThreshold = 0.5f;   // voice load % to start shortening
    float tailShortenAgeMultiplier = 0.02f;  // age factor multiplier
    float tailShortenAmpProtect = 0.6f;      // loud voice protection threshold (gain)
    float tailShortenMinFactor = 0.3f;       // min shortening factor at full load
    float voiceTailShortenMinClamp = 0.1f;   // voice-level min shortening factor
    float voiceCullThresholdDb = 12.0f;      // cull old voice if this much quieter (dB)

    // -- Hi-Hat / Polyphony --
    float polyphonyCrossfadeTimeoutSec = 0.3f;   // re-trigger crossfade duration
    float hihatPedalCrossfadeSec = 0.20f;        // CC-driven hi-hat crossfade
    float hihatPedalMinRemainingSec = 0.050f;    // min remaining audio for replacement
    float hihatCrossArtChokeDbMargin = 3.0f;     // cross-articulation choke dB margin

    // -- Round-Robin --
    int roundRobinLock = 0;  // 0 = normal cycling, 1+ = force variant index (1-based)

    // -- UI / Grid --
    int padGridColumns = 0;  // 0 = derive from active kit layout
    int padGridRows = 0;     // 0 = derive from active kit layout
    int padGap = 0;
};
