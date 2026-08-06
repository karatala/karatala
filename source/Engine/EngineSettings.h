#pragma once

#include <atomic>

enum class HiHatTransitionCurve {
    Linear,
    Smooth,
    EqualPower,
};

enum class HiHatFootSampleMode {
    Off,
    CloseOnly,
    Always,
};

enum class HiHatMissingDecayPolicy {
    KeepTail,
    SimpleFade,
};

template <typename ValueType>
class RealtimeSetting {
public:
    static_assert(std::atomic<ValueType>::is_always_lock_free, "RealtimeSetting requires a lock-free atomic value type");

    constexpr RealtimeSetting() noexcept = default;
    constexpr explicit RealtimeSetting(ValueType initialValue) noexcept
        : value(initialValue) {}

    RealtimeSetting(const RealtimeSetting& other) noexcept
        : value(other.load()) {}

    RealtimeSetting& operator=(const RealtimeSetting& other) noexcept {
        this->store(other.load());
        return *this;
    }

    RealtimeSetting& operator=(ValueType newValue) noexcept {
        this->store(newValue);
        return *this;
    }

    ValueType load() const noexcept {
        return this->value.load(std::memory_order_relaxed);
    }

    void store(ValueType newValue) noexcept {
        this->value.store(newValue, std::memory_order_relaxed);
    }

private:
    std::atomic<ValueType> value {};
};

struct EngineSettings {
    // -- Aftertouch Choke --
    RealtimeSetting<float> chokeMaxDuckDepth { 0.7f };          // max volume duck (0..1)
    RealtimeSetting<float> chokeDuckTimeSec { 0.15f };          // duck ramp duration (sec)
    RealtimeSetting<float> chokeReleaseTimeSec { 0.05f };       // release ramp duration (sec)
    RealtimeSetting<float> chokeMuteTailSec { 0.04f };          // mute tail duration (sec)
    RealtimeSetting<float> chokeReleaseBoost { 0.1f };          // boost on aftertouch release (0..0.5)
    RealtimeSetting<float> chokeMuteThresholdRatio { 0.5f };    // mute trigger threshold (% of maxPA)
    RealtimeSetting<int> polyAftertouchMinPressure { 10 };      // lower pressure is treated as release (1..20)

    // -- MIDI Crosstalk --
    RealtimeSetting<int> crosstalkWindowMs { 40 };               // correlation window for learned secondary hits

    // -- Tail Shortening --
    RealtimeSetting<float> tailShortenLoadThreshold { 0.5f };   // voice load % to start shortening
    RealtimeSetting<float> tailShortenAgeMultiplier { 0.02f };  // age factor multiplier
    RealtimeSetting<float> tailShortenAmpProtect { 0.6f };      // loud voice protection threshold (gain)
    RealtimeSetting<float> tailShortenMinFactor { 0.3f };       // min shortening factor at full load
    RealtimeSetting<float> voiceTailShortenMinClamp { 0.1f };   // voice-level min shortening factor
    RealtimeSetting<float> voiceCullThresholdDb { 12.0f };      // cull old voice if this much quieter (dB)

    // -- Hi-Hat / Polyphony --
    RealtimeSetting<float> polyphonyCrossfadeTimeoutSec { 0.3f };   // re-trigger crossfade duration
    RealtimeSetting<float> hihatHitChokeDbMargin { 3.0f };          // struck-hit cross-articulation choke margin
    RealtimeSetting<float> hihatOpenCrossfadeSec { 0.060f };
    RealtimeSetting<float> hihatCloseCrossfadeSec { 0.020f };
    RealtimeSetting<float> hihatControllerSmoothingSec { 0.015f };
    RealtimeSetting<int> hihatBoundaryHysteresisCc { 2 };
    RealtimeSetting<float> hihatTailMatchAmount { 0.75f };
    RealtimeSetting<float> hihatTailMatchBiasDb { -3.0f };
    RealtimeSetting<float> hihatTransitionAttackGuardSec { 0.025f };
    RealtimeSetting<int> hihatTransitionMaxActiveGroups { 1 };
    RealtimeSetting<HiHatTransitionCurve> hihatTransitionCurve { HiHatTransitionCurve::EqualPower };
    RealtimeSetting<HiHatFootSampleMode> hihatFootSampleMode { HiHatFootSampleMode::CloseOnly };
    RealtimeSetting<float> hihatTransitionMinRemainingSec { 0.050f };
    RealtimeSetting<float> hihatTransitionSilenceFloorDb { -72.0f };
    RealtimeSetting<HiHatMissingDecayPolicy> hihatMissingDecayPolicy { HiHatMissingDecayPolicy::KeepTail };

    // -- Round-Robin --
    RealtimeSetting<int> roundRobinLock { 0 };  // 0 = normal cycling, 1+ = force variant index (1-based)
    RealtimeSetting<int> floatingSelectionMinimumVelocityLevels { 2 };

    // -- UI / Grid --
    RealtimeSetting<int> padGridColumns { 0 };  // 0 = derive from active kit layout
    RealtimeSetting<int> padGridRows { 0 };     // 0 = derive from active kit layout
    RealtimeSetting<int> padGap { 0 };
};
