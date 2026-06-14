#include "InstrumentSynth.h"
#include "DrumKitEngine.h"
#include "HiHatTransitionPolicy.h"

namespace {

constexpr size_t maximumTransitionVoices = 128;
constexpr size_t maximumTransitionGroups = 32;

} // namespace

InstrumentSynth::ArtRtIdx InstrumentSynth::resolveHiHatTargetArticulation(ArtRtIdx sourceArtIdx, ArtRtIdx desiredArtIdx) const {
    if (sourceArtIdx < 0
        || desiredArtIdx < 0
        || sourceArtIdx >= static_cast<int>(this->runtimeArticulations.size())
        || desiredArtIdx >= static_cast<int>(this->runtimeArticulations.size())) {
        return -1;
    }

    const auto& sourceArt = this->runtimeArticulations[static_cast<size_t>(sourceArtIdx)];
    const auto& desiredArt = this->runtimeArticulations[static_cast<size_t>(desiredArtIdx)];
    if (desiredArt.pocValue < 0)
        return -1;
    if (sourceArt.isFootSplash)
        return -1;
    if (sourceArt.isFootEvent)
        return desiredArt.isFootEvent ? -1 : desiredArtIdx;

    for (size_t articulationIdx = 0; articulationIdx < this->runtimeArticulations.size(); ++articulationIdx) {
        const auto& candidateArt = this->runtimeArticulations[articulationIdx];
        if (!candidateArt.isFootEvent
            && candidateArt.nameId == sourceArt.nameId
            && candidateArt.pocValue == desiredArt.pocValue) {
            return static_cast<ArtRtIdx>(articulationIdx);
        }
    }
    return desiredArt.isFootEvent ? -1 : desiredArtIdx;
}

bool InstrumentSynth::triggerHiHatFootSample(int midiChannel, ArtRtIdx desiredArtIdx, HiHatTransitionPolicy::Direction direction, int controllerDelta) {
    if (desiredArtIdx < 0 || desiredArtIdx >= static_cast<int>(this->runtimeArticulations.size()))
        return false;

    int maximumNonFootPoc = -1;
    for (const auto& articulation : this->runtimeArticulations) {
        if (!articulation.isFootEvent)
            maximumNonFootPoc = juce::jmax(maximumNonFootPoc, articulation.pocValue);
    }
    const bool entersMostClosedArticulation = maximumNonFootPoc >= 0
        && this->runtimeArticulations[static_cast<size_t>(desiredArtIdx)].pocValue >= maximumNonFootPoc;
    if (!HiHatTransitionPolicy::shouldTriggerFootSample(
            this->parent->getEngineSettings().hihatFootSampleMode.load(),
            direction,
            entersMostClosedArticulation)) {
        return false;
    }

    ArtRtIdx footArtIdx = -1;
    int selectedFootPoc = direction == HiHatTransitionPolicy::Direction::Opening
        ? std::numeric_limits<int>::max()
        : std::numeric_limits<int>::min();
    for (size_t articulationIdx = 0; articulationIdx < this->runtimeArticulations.size(); ++articulationIdx) {
        const auto& articulation = this->runtimeArticulations[articulationIdx];
        if (!articulation.isFootEvent)
            continue;
        const bool preferred = footArtIdx < 0
            || (direction == HiHatTransitionPolicy::Direction::Opening && articulation.pocValue < selectedFootPoc)
            || (direction == HiHatTransitionPolicy::Direction::Closing && articulation.pocValue > selectedFootPoc);
        if (preferred) {
            footArtIdx = static_cast<ArtRtIdx>(articulationIdx);
            selectedFootPoc = articulation.pocValue;
        }
    }
    if (footArtIdx < 0)
        return false;

    int midiNoteNumber = -1;
    for (size_t noteIdx = 0; noteIdx < this->noteToArticulation.size(); ++noteIdx) {
        if (this->noteToArticulation[noteIdx] == footArtIdx) {
            midiNoteNumber = static_cast<int>(noteIdx);
            break;
        }
    }
    if (midiNoteNumber < 0)
        return false;

    auto& footArt = this->runtimeArticulations[static_cast<size_t>(footArtIdx)];
    const int velocity = juce::jlimit(1, 127, controllerDelta * 2);
    const int layerIdx = this->resolveVelocityLayer(footArt, static_cast<juce::uint8>(velocity));
    if (layerIdx < 0)
        return false;
    auto& layer = footArt.layers[static_cast<size_t>(layerIdx)];
    const uint8_t variantIdx = this->chooseVariantGroup(layer);
    return this->startLayerVoices(
        footArtIdx,
        footArt,
        layer,
        variantIdx,
        midiChannel,
        midiNoteNumber,
        static_cast<float>(velocity) / 127.0f,
        true);
}

InstrumentSynth::HiHatTransitionResult InstrumentSynth::transitionHiHatGroups(int midiChannel, ArtRtIdx desiredArtIdx) {
    HiHatTransitionResult result;
    if (desiredArtIdx < 0 || desiredArtIdx >= static_cast<int>(this->runtimeArticulations.size()))
        return { HiHatTransitionStatus::Skipped, 0, 0 };

    const auto& settings = this->parent->getEngineSettings();
    const float openCrossfadeSec = settings.hihatOpenCrossfadeSec.load();
    const float closeCrossfadeSec = settings.hihatCloseCrossfadeSec.load();
    const int maximumActiveGroups = settings.hihatTransitionMaxActiveGroups.load();
    const float tailMatchAmount = settings.hihatTailMatchAmount.load();
    const float tailMatchBiasDb = settings.hihatTailMatchBiasDb.load();
    const float attackGuardMs = settings.hihatTransitionAttackGuardSec.load() * 1000.0f;
    const float minimumRemainingMs = settings.hihatTransitionMinRemainingSec.load() * 1000.0f;
    const float silenceFloorDb = settings.hihatTransitionSilenceFloorDb.load();
    const auto transitionCurve = settings.hihatTransitionCurve.load();
    const auto missingDecayPolicy = settings.hihatMissingDecayPolicy.load();

    std::array<HiHatTransitionPolicy::HitGroupCandidate, maximumTransitionGroups> groupCandidates {};
    size_t groupCandidateCount = 0;

    for (auto* voice : this->voices) {
        if (voice == nullptr
            || !voice->isVoiceActive()
            || voice->getEnvelopeState() != InstrumentSynthVoice::EnvelopeState::Playing
            || voice->getRuntimeArticulationIndex() < 0
            || (midiChannel > 0 && !voice->isPlayingChannel(midiChannel))) {
            continue;
        }
        const auto sourceArtIdx = voice->getRuntimeArticulationIndex();
        const auto& sourceArt = this->runtimeArticulations[static_cast<size_t>(sourceArtIdx)];
        if (sourceArt.isFootSplash)
            continue;
        const auto targetArtIdx = this->resolveHiHatTargetArticulation(
            static_cast<ArtRtIdx>(sourceArtIdx),
            desiredArtIdx);
        if (targetArtIdx < 0 || targetArtIdx == sourceArtIdx)
            continue;
        if (sourceArt.isFootEvent
            && HiHatTransitionPolicy::getDirection(
                sourceArt.pocValue,
                this->runtimeArticulations[static_cast<size_t>(targetArtIdx)].pocValue)
                != HiHatTransitionPolicy::Direction::Opening) {
            continue;
        }

        size_t groupIdx = 0;
        while (groupIdx < groupCandidateCount
               && groupCandidates[groupIdx].groupId != voice->getEventId()) {
            ++groupIdx;
        }
        if (groupIdx == groupCandidateCount) {
            if (groupCandidateCount >= groupCandidates.size())
                continue;
            groupCandidates[groupIdx] = {
                voice->getEventId(),
                voice->getEventId(),
                voice->getEstimatedCurrentDb(),
                HiHatTransitionPolicy::HitGroupState::Playing,
            };
            ++groupCandidateCount;
        } else {
            groupCandidates[groupIdx].audibleDb = juce::jmax(
                groupCandidates[groupIdx].audibleDb,
                voice->getEstimatedCurrentDb());
        }
    }

    std::array<size_t, 4> selectedGroupIndices {};
    const size_t selectedGroupCount = HiHatTransitionPolicy::selectEligibleHitGroups(
        std::span<const HiHatTransitionPolicy::HitGroupCandidate>(groupCandidates.data(), groupCandidateCount),
        maximumActiveGroups,
        silenceFloorDb,
        selectedGroupIndices);
    result.selectedGroupCount = selectedGroupCount;
    if (selectedGroupCount == 0)
        return result;
    size_t completedGroupCount = 0;

    for (size_t selectedGroupIdx = 0; selectedGroupIdx < selectedGroupCount; ++selectedGroupIdx) {
        const auto eventId = groupCandidates[selectedGroupIndices[selectedGroupIdx]].groupId;
        std::array<InstrumentSynthVoice*, maximumTransitionVoices> sourceVoices {};
        std::array<InstrumentSynthVoice*, maximumTransitionVoices> reservedVoices {};
        std::array<const SampleEntry*, maximumTransitionVoices> targetEntries {};
        std::array<float, maximumTransitionVoices> targetOffsetMs {};
        std::array<HiHatTransitionPolicy::ReferenceMicCandidate, maximumTransitionVoices> referenceCandidates {};
        size_t sourceVoiceCount = 0;
        int sourceArtIdx = -1;
        bool sourceGroupValid = true;

        for (auto* voice : this->voices) {
            if (voice == nullptr || voice->getEventId() != eventId)
                continue;
            if (!voice->isVoiceActive()
                || voice->getEnvelopeState() != InstrumentSynthVoice::EnvelopeState::Playing
                || voice->getRuntimeArticulationIndex() < 0
                || sourceVoiceCount >= sourceVoices.size()) {
                sourceGroupValid = false;
                break;
            }
            if (sourceArtIdx < 0)
                sourceArtIdx = voice->getRuntimeArticulationIndex();
            if (sourceArtIdx != voice->getRuntimeArticulationIndex()) {
                sourceGroupValid = false;
                break;
            }

            auto* sourceSound = voice->getCurrentlyPlayingSound();
            if (sourceSound == nullptr || sourceSound->sampleVariantSpec == nullptr) {
                sourceGroupValid = false;
                break;
            }
            sourceVoices[sourceVoiceCount] = voice;
            referenceCandidates[sourceVoiceCount] = {
                sourceSound->sampleVariantSpec->micSpec.type,
                voice->getEstimatedCurrentDb(),
                true,
            };
            ++sourceVoiceCount;
        }

        if (!sourceGroupValid
            || sourceVoiceCount == 0
            || sourceArtIdx >= static_cast<int>(this->runtimeArticulations.size())) {
            continue;
        }

        const ArtRtIdx targetArtIdx = this->resolveHiHatTargetArticulation(
            static_cast<ArtRtIdx>(sourceArtIdx),
            desiredArtIdx);
        if (targetArtIdx < 0 || sourceArtIdx == targetArtIdx)
            continue;
        const auto& targetArt = this->runtimeArticulations[static_cast<size_t>(targetArtIdx)];
        const int referenceSourceIdx = HiHatTransitionPolicy::selectReferenceMic(
            std::span<const HiHatTransitionPolicy::ReferenceMicCandidate>(referenceCandidates.data(), sourceVoiceCount));
        if (referenceSourceIdx < 0)
            continue;

        auto* referenceVoice = sourceVoices[static_cast<size_t>(referenceSourceIdx)];
        const int midiVelocity = static_cast<int>(
            juce::jlimit(0.0f, 1.0f, referenceVoice->getVelocityGain()) * 127.0f);
        const int layerIdx = targetArt.velocityToLayer[static_cast<size_t>(midiVelocity)];
        if (layerIdx < 0 || layerIdx >= static_cast<int>(targetArt.layers.size()))
            continue;
        const auto& targetLayer = targetArt.layers[static_cast<size_t>(layerIdx)];

        const auto resolveVariant = [&](uint8_t variantIdx) {
            size_t resolvedTargetCount = 0;
            for (size_t sourceIdx = 0; sourceIdx < sourceVoiceCount; ++sourceIdx) {
                auto* sourceSound = sourceVoices[sourceIdx]->getCurrentlyPlayingSound();
                const auto* targetEntry = targetLayer.findTransitionEntry(
                    variantIdx,
                    sourceSound->micBufferChannelStart);
                if (targetEntry == nullptr)
                    return size_t { 0 };
                targetEntries[sourceIdx] = targetEntry;
                ++resolvedTargetCount;
            }
            return resolvedTargetCount;
        };

        const uint8_t preferredVariantIdx = KitLib::SampleVariantSpec::letterToIdx(
            referenceVoice->getCurrentlyPlayingSound()->sampleVariantSpec->letter);
        size_t resolvedTargetCount = resolveVariant(preferredVariantIdx);
        if (resolvedTargetCount == 0) {
            for (uint8_t fallbackVariantIdx = 0; fallbackVariantIdx < VelocityLayerRuntime::maximumVariantCount; ++fallbackVariantIdx) {
                if (fallbackVariantIdx == preferredVariantIdx)
                    continue;
                resolvedTargetCount = resolveVariant(fallbackVariantIdx);
                if (resolvedTargetCount == sourceVoiceCount)
                    break;
            }
        }

        size_t reservedVoiceCount = 0;
        if (resolvedTargetCount == sourceVoiceCount) {
            for (auto* candidateVoice : this->voices) {
                if (candidateVoice == nullptr || candidateVoice->isVoiceActive())
                    continue;
                reservedVoices[reservedVoiceCount++] = candidateVoice;
                if (reservedVoiceCount == sourceVoiceCount)
                    break;
            }
        }

        if (!HiHatTransitionPolicy::canCommitGroupTransition(
                sourceVoiceCount, resolvedTargetCount, reservedVoiceCount)) {
            continue;
        }

        const auto& sourceArt = this->runtimeArticulations[static_cast<size_t>(sourceArtIdx)];
        bool offsetsValid = true;
        for (size_t sourceIdx = 0; sourceIdx < sourceVoiceCount; ++sourceIdx) {
            auto* sourceVoice = sourceVoices[sourceIdx];
            auto* sourceSound = sourceVoice->getCurrentlyPlayingSound();
            auto* targetSound = targetEntries[sourceIdx]->sound;
            const double sourceRate = sourceSound->sampleRate > 0.0 ? sourceSound->sampleRate : 44100.0;
            const float elapsedMs = static_cast<float>((sourceVoice->sourceSamplePosition / sourceRate) * 1000.0);
            const float velocityDb = juce::Decibels::gainToDecibels(
                juce::jmax(0.001f, sourceVoice->getVelocityGain()),
                -100.0f);
            const float currentAudibleDb = sourceSound->decayEnvelope.isValid()
                ? sourceSound->decayEnvelope.getAbsoluteDbAtMs(elapsedMs) + velocityDb
                : sourceVoice->getEstimatedCurrentDb();
            const auto offsetResult = HiHatTransitionPolicy::calculateDecayMatchedOffset({
                &sourceSound->decayEnvelope,
                &targetSound->decayEnvelope,
                elapsedMs,
                currentAudibleDb,
                velocityDb,
                targetSound->sampleLengthMs,
                attackGuardMs,
                silenceFloorDb,
                tailMatchAmount,
                tailMatchBiasDb,
                minimumRemainingMs,
                sourceArt.isFootEvent ? HiHatMissingDecayPolicy::SimpleFade : missingDecayPolicy,
            });
            if (!offsetResult.succeeded()) {
                offsetsValid = false;
                break;
            }
            targetOffsetMs[sourceIdx] = offsetResult.offsetMs;
        }
        if (!offsetsValid)
            continue;

        const auto transitionDirection = HiHatTransitionPolicy::getDirection(
            sourceArt.pocValue,
            targetArt.pocValue);
        const float transitionCrossfadeSec = transitionDirection == HiHatTransitionPolicy::Direction::Closing
            ? closeCrossfadeSec
            : openCrossfadeSec;

        for (size_t sourceIdx = 0; sourceIdx < sourceVoiceCount; ++sourceIdx) {
            auto* sourceVoice = sourceVoices[sourceIdx];
            auto* targetVoice = reservedVoices[sourceIdx];
            auto* targetSound = targetEntries[sourceIdx]->sound;
            const float velocity = sourceVoice->getVelocityGain();
            this->startVoice(
                targetVoice,
                targetSound,
                sourceVoice->currentPlayingMidiChannel,
                sourceVoice->getCurrentlyPlayingNote(),
                velocity,
                eventId);
            targetVoice->hitGroupId = targetSound->micBufferChannelStart;
            targetVoice->runtimeArticulationIdx = targetArtIdx;
            targetVoice->artNameId = targetArt.nameId;
            targetVoice->artExprId = targetArt.exprId;
            const double targetRate = targetSound->sampleRate > 0.0 ? targetSound->sampleRate : 44100.0;
            targetVoice->sourceSamplePosition = juce::jlimit(
                0.0,
                juce::jmax(0.0, static_cast<double>(targetSound->sampleLengthSamples - 1)),
                (static_cast<double>(targetOffsetMs[sourceIdx]) / 1000.0) * targetRate);
            targetVoice->setVelocityGain(velocity);
            if (transitionDirection == HiHatTransitionPolicy::Direction::Closing)
                targetVoice->requestTransitionFadeIn(transitionCrossfadeSec, transitionCurve);
        }
        for (size_t sourceIdx = 0; sourceIdx < sourceVoiceCount; ++sourceIdx) {
            if (transitionDirection == HiHatTransitionPolicy::Direction::Closing)
                sourceVoices[sourceIdx]->requestTransitionFadeOut(transitionCrossfadeSec, transitionCurve);
            else
                sourceVoices[sourceIdx]->requestFadeOut(transitionCrossfadeSec);
        }

        ++completedGroupCount;
        this->completedHiHatTransitions.fetch_add(1, std::memory_order_relaxed);
    }

    result.completedGroupCount = completedGroupCount;
    this->skippedHiHatTransitions.fetch_add(
        static_cast<uint64_t>(selectedGroupCount - completedGroupCount),
        std::memory_order_relaxed);
    result.status = completedGroupCount == selectedGroupCount
        ? HiHatTransitionStatus::Completed
        : HiHatTransitionStatus::Skipped;
    return result;
}
