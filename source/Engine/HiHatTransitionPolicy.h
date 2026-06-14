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

int selectOpennessArticulation(std::span<const uint8_t, 128> articulationByCc,
                               std::span<const int> pocByArticulation,
                               int controllerValue,
                               int previousControllerValue,
                               int currentArticulation,
                               int boundaryHysteresisCc) noexcept;

Direction getDirection(int sourcePocValue, int targetPocValue) noexcept;

OffsetResult calculateDecayMatchedOffset(const OffsetInput& input) noexcept;

} // namespace HiHatTransitionPolicy
