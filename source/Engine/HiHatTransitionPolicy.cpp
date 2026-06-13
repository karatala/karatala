#include "HiHatTransitionPolicy.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace HiHatTransitionPolicy {
namespace {

int micPriority(KitLib::MicSpec::Type micType) noexcept {
    switch (micType) {
        case KitLib::MicSpec::Type::Direct: return 0;
        case KitLib::MicSpec::Type::Overhead: return 1;
        case KitLib::MicSpec::Type::Room: return 2;
        case KitLib::MicSpec::Type::Unknown: return 3;
    }
    return 3;
}

bool isCandidatePreferred(const HitGroupCandidate& candidate, const HitGroupCandidate& selected) noexcept {
    if (candidate.noteOnTime != selected.noteOnTime)
        return candidate.noteOnTime > selected.noteOnTime;
    if (candidate.audibleDb != selected.audibleDb)
        return candidate.audibleDb > selected.audibleDb;
    return candidate.groupId > selected.groupId;
}

} // namespace

int selectOpennessArticulation(std::span<const uint8_t, 128> articulationByCc,
                               std::span<const int> pocByArticulation,
                               int controllerValue,
                               int previousControllerValue,
                               int currentArticulation,
                               int boundaryHysteresisCc) noexcept {
    const int clampedControllerValue = std::clamp(controllerValue, 0, 127);
    const int candidateArticulation = articulationByCc[static_cast<size_t>(clampedControllerValue)];
    if (candidateArticulation < 0 || candidateArticulation >= static_cast<int>(pocByArticulation.size()))
        return currentArticulation;
    if (candidateArticulation == currentArticulation)
        return currentArticulation;
    if (boundaryHysteresisCc <= 0
        || currentArticulation < 0
        || currentArticulation >= static_cast<int>(pocByArticulation.size()))
        return candidateArticulation;

    const int direction = clampedControllerValue > previousControllerValue
        ? 1
        : (clampedControllerValue < previousControllerValue ? -1 : 0);
    if (direction == 0)
        return currentArticulation;

    const int clampedHysteresis = std::clamp(boundaryHysteresisCc, 0, 127);
    const int probeValue = std::clamp(clampedControllerValue - direction * clampedHysteresis, 0, 127);
    const int probeArticulation = articulationByCc[static_cast<size_t>(probeValue)];
    if (probeArticulation < 0 || probeArticulation >= static_cast<int>(pocByArticulation.size()))
        return currentArticulation;

    return pocByArticulation[static_cast<size_t>(probeArticulation)]
            == pocByArticulation[static_cast<size_t>(candidateArticulation)]
        ? candidateArticulation
        : currentArticulation;
}

Direction getDirection(int sourcePocValue, int targetPocValue) noexcept {
    if (sourcePocValue < 0 || targetPocValue < 0)
        return Direction::Unknown;
    if (targetPocValue < sourcePocValue)
        return Direction::Opening;
    if (targetPocValue > sourcePocValue)
        return Direction::Closing;
    return Direction::Unchanged;
}

size_t selectEligibleHitGroups(std::span<const HitGroupCandidate> candidates,
                               int maximumGroups,
                               float silenceFloorDb,
                               std::span<size_t> selectedCandidateIndices) noexcept {
    const size_t selectionLimit = std::min(
        selectedCandidateIndices.size(),
        static_cast<size_t>(std::max(0, maximumGroups)));
    size_t selectedCount = 0;

    for (size_t candidateIdx = 0; candidateIdx < candidates.size(); ++candidateIdx) {
        const auto& candidate = candidates[candidateIdx];
        if (candidate.state != HitGroupState::Playing || candidate.audibleDb < silenceFloorDb)
            continue;

        size_t insertionIdx = 0;
        while (insertionIdx < selectedCount
               && !isCandidatePreferred(candidate, candidates[selectedCandidateIndices[insertionIdx]])) {
            ++insertionIdx;
        }
        if (insertionIdx >= selectionLimit)
            continue;

        const size_t moveEnd = std::min(selectedCount, selectionLimit - 1);
        for (size_t moveIdx = moveEnd; moveIdx > insertionIdx; --moveIdx)
            selectedCandidateIndices[moveIdx] = selectedCandidateIndices[moveIdx - 1];

        selectedCandidateIndices[insertionIdx] = candidateIdx;
        if (selectedCount < selectionLimit)
            ++selectedCount;
    }

    return selectedCount;
}

int selectReferenceMic(std::span<const ReferenceMicCandidate> candidates) noexcept {
    int selectedIdx = -1;
    int selectedPriority = 4;
    float selectedAudibleDb = -std::numeric_limits<float>::infinity();

    for (size_t candidateIdx = 0; candidateIdx < candidates.size(); ++candidateIdx) {
        const auto& candidate = candidates[candidateIdx];
        if (!candidate.valid)
            continue;

        const int candidatePriority = micPriority(candidate.micType);
        if (candidatePriority < selectedPriority
            || (candidatePriority == selectedPriority && candidate.audibleDb > selectedAudibleDb)) {
            selectedIdx = static_cast<int>(candidateIdx);
            selectedPriority = candidatePriority;
            selectedAudibleDb = candidate.audibleDb;
        }
    }

    return selectedIdx;
}

OffsetResult calculateDecayMatchedOffset(const OffsetInput& input) noexcept {
    OffsetResult result;
    if (input.elapsedMs < input.attackGuardMs) {
        result.status = OffsetStatus::AttackGuard;
        return result;
    }
    if (input.currentAudibleDb < input.silenceFloorDb) {
        result.status = OffsetStatus::BelowSilenceFloor;
        return result;
    }
    if (input.sampleLengthMs <= 0.0f) {
        result.status = OffsetStatus::InvalidSampleLength;
        return result;
    }

    const bool sourceEnvelopeValid = input.sourceEnvelope != nullptr && input.sourceEnvelope->isValid();
    const bool targetEnvelopeValid = input.targetEnvelope != nullptr && input.targetEnvelope->isValid();
    const bool hasDecayMatch = sourceEnvelopeValid && targetEnvelopeValid;
    if (!hasDecayMatch && input.missingDecayPolicy == HiHatMissingDecayPolicy::KeepTail) {
        result.status = OffsetStatus::MissingEnvelope;
        return result;
    }

    result.targetRelativeDb = input.currentAudibleDb + input.matchBiasDb - input.replacementGainDb;
    const float targetPeakMs = std::clamp(
        targetEnvelopeValid ? input.targetEnvelope->peakMs : 0.0f,
        0.0f,
        input.sampleLengthMs);
    const float fallbackMs = std::clamp(
        std::max(targetPeakMs, input.elapsedMs),
        targetPeakMs,
        input.sampleLengthMs);
    float targetMs = fallbackMs;

    if (hasDecayMatch) {
        const float matchedMs = input.targetEnvelope->getMsAtAbsoluteDb(result.targetRelativeDb);
        if (matchedMs < 0.0f) {
            result.status = OffsetStatus::NoDecayMatch;
            return result;
        }
        const float matchAmount = std::clamp(input.matchAmount, 0.0f, 1.0f);
        targetMs = fallbackMs + matchAmount * (matchedMs - fallbackMs);
    }

    result.offsetMs = std::clamp(targetMs, targetPeakMs, input.sampleLengthMs);
    if (input.sampleLengthMs - result.offsetMs < input.minimumRemainingMs) {
        result.status = OffsetStatus::TooLittleRemaining;
        return result;
    }

    result.status = OffsetStatus::Success;
    return result;
}

TransitionGains calculateTransitionGains(float progress, HiHatTransitionCurve curve) noexcept {
    const float clampedProgress = std::clamp(progress, 0.0f, 1.0f);
    float shapedProgress = clampedProgress;
    if (curve == HiHatTransitionCurve::Smooth)
        shapedProgress = clampedProgress * clampedProgress * (3.0f - 2.0f * clampedProgress);

    if (curve == HiHatTransitionCurve::EqualPower) {
        const float angle = clampedProgress * std::numbers::pi_v<float> * 0.5f;
        return { std::cos(angle), std::sin(angle) };
    }

    return { 1.0f - shapedProgress, shapedProgress };
}

bool shouldTriggerFootSample(HiHatFootSampleMode mode,
                             Direction direction,
                             bool entersMostClosedArticulation,
                             bool qualifyingTransition) noexcept {
    if (!qualifyingTransition || direction == Direction::Unknown || direction == Direction::Unchanged)
        return false;

    switch (mode) {
        case HiHatFootSampleMode::Off:
            return false;
        case HiHatFootSampleMode::CloseOnly:
            return direction == Direction::Closing && entersMostClosedArticulation;
        case HiHatFootSampleMode::Always:
            return true;
    }
    return false;
}

} // namespace HiHatTransitionPolicy
