#pragma once

#include "EngineSettings.h"
#include "KitLibModel.h"
#include <cstddef>
#include <cstdint>
#include <span>

namespace HiHatTransitionPolicy {

enum class Direction {
    Unknown,
    Unchanged,
    Opening,
    Closing,
};

enum class HitGroupState {
    Playing,
    FadingOut,
    Releasing,
    Dying,
};

struct HitGroupCandidate {
    uint32_t groupId = 0;
    uint32_t noteOnTime = 0;
    float audibleDb = -100.0f;
    HitGroupState state = HitGroupState::Playing;
};

struct ReferenceMicCandidate {
    KitLib::MicSpec::Type micType = KitLib::MicSpec::Type::Unknown;
    float audibleDb = -100.0f;
    bool valid = false;
};

enum class OffsetStatus {
    Success,
    AttackGuard,
    BelowSilenceFloor,
    InvalidSampleLength,
    MissingEnvelope,
    NoDecayMatch,
    TooLittleRemaining,
};

struct OffsetInput {
    const KitLib::DecayEnvelope* sourceEnvelope = nullptr;
    const KitLib::DecayEnvelope* targetEnvelope = nullptr;
    float elapsedMs = 0.0f;
    float currentAudibleDb = -100.0f;
    float replacementGainDb = 0.0f;
    float sampleLengthMs = 0.0f;
    float attackGuardMs = 0.0f;
    float silenceFloorDb = -72.0f;
    float matchAmount = 0.75f;
    float matchBiasDb = -3.0f;
    float minimumRemainingMs = 50.0f;
    HiHatMissingDecayPolicy missingDecayPolicy = HiHatMissingDecayPolicy::KeepTail;
};

struct OffsetResult {
    OffsetStatus status = OffsetStatus::MissingEnvelope;
    float offsetMs = 0.0f;
    float targetRelativeDb = -100.0f;

    bool succeeded() const noexcept { return this->status == OffsetStatus::Success; }
};

struct TransitionGains {
    float sourceGain = 1.0f;
    float targetGain = 0.0f;
};

int selectOpennessArticulation(std::span<const uint8_t, 128> articulationByCc,
                               std::span<const int> pocByArticulation,
                               int controllerValue,
                               int previousControllerValue,
                               int currentArticulation,
                               int boundaryHysteresisCc) noexcept;

Direction getDirection(int sourcePocValue, int targetPocValue) noexcept;

size_t selectEligibleHitGroups(std::span<const HitGroupCandidate> candidates,
                               int maximumGroups,
                               float silenceFloorDb,
                               std::span<size_t> selectedCandidateIndices) noexcept;

int selectReferenceMic(std::span<const ReferenceMicCandidate> candidates) noexcept;

OffsetResult calculateDecayMatchedOffset(const OffsetInput& input) noexcept;

TransitionGains calculateTransitionGains(float progress, HiHatTransitionCurve curve) noexcept;

bool shouldTriggerFootSample(HiHatFootSampleMode mode,
                             Direction direction,
                             bool entersMostClosedArticulation,
                             bool qualifyingTransition = true) noexcept;

} // namespace HiHatTransitionPolicy
